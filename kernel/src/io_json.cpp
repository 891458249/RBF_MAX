// =============================================================================
// kernel/src/io_json.cpp
// -----------------------------------------------------------------------------
// Implementation of rbfmax::io_json — see io_json.hpp + docs/spec/schema_v1.md.
//
// All file I/O and JSON traversal is wrapped in try/catch ⇒ outer noexcept
// boundary preserved.  parse_v1_json builds into local temporaries and
// only commits to out_* after the full parse succeeds (atomic update,
// contract 2).
// =============================================================================
#include "rbfmax/io_json.hpp"

#include <cmath>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <limits>
#include <string>

#include <nlohmann/json.hpp>

#include "rbfmax/kernel_functions.hpp"

namespace rbfmax {
namespace io_json {

namespace {

using json = nlohmann::json;

// -----------------------------------------------------------------------------
//  Enum ↔ string helpers
// -----------------------------------------------------------------------------

const char* solver_path_to_string(solver::SolverPath p) noexcept {
    switch (p) {
        case solver::SolverPath::LLT:    return "LLT";
        case solver::SolverPath::LDLT:   return "LDLT";
        case solver::SolverPath::BDCSVD: return "BDCSVD";
        case solver::SolverPath::FAILED: return "FAILED";
    }
    return "Unknown";
}

bool solver_path_from_string(const std::string& s,
                             solver::SolverPath& out) noexcept {
    if (s == "LLT")    { out = solver::SolverPath::LLT;    return true; }
    if (s == "LDLT")   { out = solver::SolverPath::LDLT;   return true; }
    if (s == "BDCSVD") { out = solver::SolverPath::BDCSVD; return true; }
    if (s == "FAILED") { out = solver::SolverPath::FAILED; return true; }
    return false;
}

const char* fit_status_to_string(solver::FitStatus s) noexcept {
    switch (s) {
        case solver::FitStatus::OK:                   return "OK";
        case solver::FitStatus::INSUFFICIENT_SAMPLES: return "INSUFFICIENT_SAMPLES";
        case solver::FitStatus::SINGULAR_MATRIX:      return "SINGULAR_MATRIX";
        case solver::FitStatus::INVALID_INPUT:        return "INVALID_INPUT";
    }
    return "Unknown";
}

bool fit_status_from_string(const std::string& s,
                            solver::FitStatus& out) noexcept {
    if (s == "OK") {
        out = solver::FitStatus::OK; return true;
    }
    if (s == "INSUFFICIENT_SAMPLES") {
        out = solver::FitStatus::INSUFFICIENT_SAMPLES; return true;
    }
    if (s == "SINGULAR_MATRIX") {
        out = solver::FitStatus::SINGULAR_MATRIX; return true;
    }
    if (s == "INVALID_INPUT") {
        out = solver::FitStatus::INVALID_INPUT; return true;
    }
    return false;
}

// -----------------------------------------------------------------------------
//  Scalar / Matrix ↔ JSON
// -----------------------------------------------------------------------------
//  NaN / Inf become JSON null on write (JSON spec lacks IEEE special
//  values).  On read, null → NaN (Inf is NOT round-tripped — see
//  schema_v1.md "Known limitations").
// -----------------------------------------------------------------------------

json scalar_to_json(Scalar x) noexcept {
    if (std::isnan(x) || std::isinf(x)) {
        return nullptr;
    }
    return json(x);
}

bool scalar_from_json(const json& j, Scalar& out) noexcept {
    if (j.is_null()) {
        out = std::numeric_limits<Scalar>::quiet_NaN();
        return true;
    }
    if (!j.is_number()) {
        return false;
    }
    out = j.get<Scalar>();
    return true;
}

json matrix_to_json(const MatrixX& m) noexcept {
    json j;
    j["rows"] = static_cast<long long>(m.rows());
    j["cols"] = static_cast<long long>(m.cols());
    json values = json::array();
    for (Index i = 0; i < m.rows(); ++i) {
        json row = json::array();
        for (Index k = 0; k < m.cols(); ++k) {
            row.push_back(scalar_to_json(m(i, k)));
        }
        values.push_back(std::move(row));
    }
    j["values"] = std::move(values);
    return j;
}

bool matrix_from_json(const json& j, MatrixX& out) noexcept {
    if (!j.is_object()) return false;
    if (!j.contains("rows") || !j.contains("cols") || !j.contains("values")) {
        return false;
    }
    if (!j["rows"].is_number_integer() || !j["cols"].is_number_integer()) {
        return false;
    }
    const long long rows_ll = j["rows"].get<long long>();
    const long long cols_ll = j["cols"].get<long long>();
    if (rows_ll < 0 || cols_ll < 0) return false;
    const Index rows = static_cast<Index>(rows_ll);
    const Index cols = static_cast<Index>(cols_ll);
    if (!j["values"].is_array()) return false;
    if (static_cast<long long>(j["values"].size()) != rows_ll) return false;

    MatrixX m(rows, cols);
    for (Index i = 0; i < rows; ++i) {
        const json& row = j["values"][static_cast<std::size_t>(i)];
        if (!row.is_array()) return false;
        if (static_cast<long long>(row.size()) != cols_ll) return false;
        for (Index k = 0; k < cols; ++k) {
            Scalar v;
            if (!scalar_from_json(row[static_cast<std::size_t>(k)], v)) {
                return false;
            }
            m(i, k) = v;
        }
    }
    out = std::move(m);
    return true;
}

// -----------------------------------------------------------------------------
//  Diagnostic helpers
// -----------------------------------------------------------------------------

std::string iso8601_now() noexcept {
    std::time_t t = std::time(nullptr);
    std::tm tm_utc;
#if defined(_WIN32)
    if (gmtime_s(&tm_utc, &t) != 0) return "1970-01-01T00:00:00Z";
#else
    if (gmtime_r(&t, &tm_utc) == nullptr) return "1970-01-01T00:00:00Z";
#endif
    char buf[32];
    if (std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm_utc) == 0) {
        return "1970-01-01T00:00:00Z";
    }
    return std::string(buf);
}

std::string library_version_string() noexcept {
#if defined(RBFMAX_VERSION_MAJOR) && defined(RBFMAX_VERSION_MINOR) && \
    defined(RBFMAX_VERSION_PATCH)
    return std::to_string(RBFMAX_VERSION_MAJOR) + "." +
           std::to_string(RBFMAX_VERSION_MINOR) + "." +
           std::to_string(RBFMAX_VERSION_PATCH);
#else
    return "unknown";
#endif
}

// -----------------------------------------------------------------------------
//  v1 build / parse
// -----------------------------------------------------------------------------

json build_v1_json(const InterpolatorOptions& opts,
                   const solver::FitResult& fr) noexcept {
    json j;
    j["schema"] = kCurrentSchema;

    json meta;
    meta["library"]    = "rbfmax";
    meta["version"]    = library_version_string();
    meta["created_at"] = iso8601_now();
    j["meta"] = std::move(meta);

    json config;
    json kernel;
    kernel["type"] = kernel_type_to_string(opts.kernel.type);
    kernel["eps"]  = scalar_to_json(opts.kernel.eps);
    config["kernel"]           = std::move(kernel);
    config["poly_degree"]      = opts.poly_degree;
    config["kdtree_threshold"] = static_cast<long long>(opts.kdtree_threshold);
    config["knn_neighbors"]    = static_cast<long long>(opts.knn_neighbors);
    config["force_dense"]      = opts.force_dense;
    j["config"] = std::move(config);

    json training;
    training["lambda_used"]      = scalar_to_json(fr.lambda_used);
    training["solver_path"]      = solver_path_to_string(fr.solver_path);
    training["status"]           = fit_status_to_string(fr.status);
    training["condition_number"] = scalar_to_json(fr.condition_number);
    training["residual_norm"]    = scalar_to_json(fr.residual_norm);
    j["training"] = std::move(training);

    json data;
    data["centers"]     = matrix_to_json(fr.centers);
    data["weights"]     = matrix_to_json(fr.weights);
    data["poly_coeffs"] = matrix_to_json(fr.poly_coeffs);
    j["data"] = std::move(data);

    return j;
}

bool parse_v1_json(const json& j,
                   InterpolatorOptions& out_opts,
                   solver::FitResult& out_fr) noexcept {
    // -------- config --------
    if (!j.contains("config") || !j["config"].is_object()) return false;
    const json& cfg = j["config"];

    if (!cfg.contains("kernel") || !cfg["kernel"].is_object()) return false;
    const json& kernel_j = cfg["kernel"];
    if (!kernel_j.contains("type") || !kernel_j["type"].is_string()) {
        return false;
    }
    KernelType ktype;
    if (!kernel_type_from_string(
            kernel_j["type"].get<std::string>().c_str(), ktype)) {
        return false;
    }
    if (!kernel_j.contains("eps")) return false;
    Scalar keps;
    if (!scalar_from_json(kernel_j["eps"], keps)) return false;

    if (!cfg.contains("poly_degree") || !cfg["poly_degree"].is_number_integer()) {
        return false;
    }
    if (!cfg.contains("kdtree_threshold") ||
        !cfg["kdtree_threshold"].is_number_integer()) {
        return false;
    }
    if (!cfg.contains("knn_neighbors") ||
        !cfg["knn_neighbors"].is_number_integer()) {
        return false;
    }
    if (!cfg.contains("force_dense") || !cfg["force_dense"].is_boolean()) {
        return false;
    }

    InterpolatorOptions opts;
    opts.kernel           = KernelParams(ktype, keps);
    opts.poly_degree      = cfg["poly_degree"].get<int>();
    opts.kdtree_threshold = static_cast<Index>(
        cfg["kdtree_threshold"].get<long long>());
    opts.knn_neighbors    = static_cast<Index>(
        cfg["knn_neighbors"].get<long long>());
    opts.force_dense      = cfg["force_dense"].get<bool>();

    // -------- training --------
    if (!j.contains("training") || !j["training"].is_object()) return false;
    const json& tr = j["training"];
    Scalar lambda_used, condition_number, residual_norm;
    if (!tr.contains("lambda_used") ||
        !scalar_from_json(tr["lambda_used"], lambda_used)) return false;
    if (!tr.contains("solver_path") || !tr["solver_path"].is_string()) {
        return false;
    }
    solver::SolverPath sp;
    if (!solver_path_from_string(tr["solver_path"].get<std::string>(), sp)) {
        return false;
    }
    if (!tr.contains("status") || !tr["status"].is_string()) return false;
    solver::FitStatus st;
    if (!fit_status_from_string(tr["status"].get<std::string>(), st)) {
        return false;
    }
    if (!tr.contains("condition_number") ||
        !scalar_from_json(tr["condition_number"], condition_number)) {
        return false;
    }
    if (!tr.contains("residual_norm") ||
        !scalar_from_json(tr["residual_norm"], residual_norm)) {
        return false;
    }

    // -------- data --------
    if (!j.contains("data") || !j["data"].is_object()) return false;
    const json& data = j["data"];
    if (!data.contains("centers") || !data.contains("weights") ||
        !data.contains("poly_coeffs")) {
        return false;
    }
    MatrixX centers, weights, poly_coeffs;
    if (!matrix_from_json(data["centers"],     centers))     return false;
    if (!matrix_from_json(data["weights"],     weights))     return false;
    if (!matrix_from_json(data["poly_coeffs"], poly_coeffs)) return false;

    // Build the FitResult.  Copy KernelParams from opts since they share the
    // same kernel choice — solver::FitResult also stores kernel.
    solver::FitResult fr;
    fr.kernel           = opts.kernel;
    fr.poly_degree      = opts.poly_degree;
    fr.lambda_used      = lambda_used;
    fr.solver_path      = sp;
    fr.status           = st;
    fr.condition_number = condition_number;
    fr.residual_norm    = residual_norm;
    fr.centers          = std::move(centers);
    fr.weights          = std::move(weights);
    fr.poly_coeffs      = std::move(poly_coeffs);

    // Atomic commit.
    out_opts = std::move(opts);
    out_fr   = std::move(fr);
    return true;
}

}  // namespace

// =============================================================================
//  Public API
// =============================================================================

bool save(const InterpolatorOptions& opts,
          const solver::FitResult& fit_result,
          const std::string& path) noexcept {
    try {
        const json j = build_v1_json(opts, fit_result);
        std::ofstream fs(path);
        if (!fs.is_open()) return false;
        fs << j.dump(2);
        if (!fs.good()) return false;
        return true;
    } catch (...) {
        return false;
    }
}

bool load(InterpolatorOptions& out_opts,
          solver::FitResult& out_fit_result,
          const std::string& path) noexcept {
    try {
        std::ifstream fs(path);
        if (!fs.is_open()) return false;
        json j;
        fs >> j;
        // After successful parse, the eofbit may or may not be set; only
        // a hard read failure (failbit/badbit without eof) is fatal.
        if (fs.bad()) return false;

        if (!j.contains("schema") || !j["schema"].is_string()) return false;
        const std::string schema = j["schema"].get<std::string>();

        if (schema == "rbfmax/v1") {
            InterpolatorOptions tmp_opts;
            solver::FitResult   tmp_fr;
            if (!parse_v1_json(j, tmp_opts, tmp_fr)) return false;
            out_opts       = std::move(tmp_opts);
            out_fit_result = std::move(tmp_fr);
            return true;
        }
        return false;  // unknown schema
    } catch (...) {
        return false;
    }
}

}  // namespace io_json
}  // namespace rbfmax
