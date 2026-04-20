// =============================================================================
// test_io_json.cpp — Unit tests for rbfmax::io_json (schema v1)
// -----------------------------------------------------------------------------
// 14 TEST blocks across 5 categories (A-E):
//   A — round-trip bit-identity (5)
//   B — schema version + missing-field rejection (3)
//   C — file-system + corrupt input (3)
//   D — numerical fidelity + NaN/Inf lossy contract (2)
//   E — RBFInterpolator convenience methods (1)
//
// Random seed 0xF5BFA7u (sequential after Slice 07's 0xF5BFA6u).
// Tolerances: A/D1/E1 use exact equality (EXPECT_EQ), per the design's
// full double precision round-trip claim.  D2 verifies the documented
// NaN/Inf → null lossy conversion.
// =============================================================================
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <random>
#include <sstream>
#include <string>

#if defined(_WIN32)
#  include <process.h>
#else
#  include <unistd.h>
#endif

#include <Eigen/Core>
#include <gtest/gtest.h>

#include "rbfmax/interpolator.hpp"
#include "rbfmax/io_json.hpp"
#include "rbfmax/kernel_functions.hpp"
#include "rbfmax/solver.hpp"
#include "rbfmax/types.hpp"

namespace {

using rbfmax::Index;
using rbfmax::InterpolatorOptions;
using rbfmax::KernelParams;
using rbfmax::KernelType;
using rbfmax::MatrixX;
using rbfmax::RBFInterpolator;
using rbfmax::Scalar;
using rbfmax::VectorX;
using rbfmax::solver::FitResult;
using rbfmax::solver::FitStatus;

constexpr std::uint32_t kSeed = 0xF5BFA7u;

MatrixX random_matrix(std::mt19937& rng, Index r, Index c, Scalar lo = -1.0,
                      Scalar hi = 1.0) {
    std::uniform_real_distribution<Scalar> u(lo, hi);
    MatrixX m(r, c);
    for (Index i = 0; i < r; ++i)
        for (Index j = 0; j < c; ++j) m(i, j) = u(rng);
    return m;
}

// RAII temp-file path.  Generates a unique filename in the OS temp dir
// and removes the file on destruction.  Avoids std::tmpnam's deprecation
// warnings on MSVC by combining test name + counter + pid.
class TempFile {
public:
    TempFile(const std::string& tag) {
        static std::atomic<int> counter{0};
        std::ostringstream oss;
#if defined(_WIN32)
        // _dupenv_s avoids the C4996 deprecation on getenv under /WX.
        char* tmp = nullptr;
        std::size_t tmp_len = 0;
        if (_dupenv_s(&tmp, &tmp_len, "TEMP") != 0 || tmp == nullptr) {
            if (_dupenv_s(&tmp, &tmp_len, "TMP") != 0 || tmp == nullptr) {
                tmp = nullptr;
            }
        }
        const char* tmp_dir = (tmp != nullptr) ? tmp : ".";
        oss << tmp_dir << "\\rbfmax_test_" << tag << "_"
            << static_cast<int>(::_getpid()) << "_"
            << counter.fetch_add(1) << ".json";
        if (tmp != nullptr) std::free(tmp);
#else
        oss << "/tmp/rbfmax_test_" << tag << "_"
            << static_cast<int>(::getpid()) << "_"
            << counter.fetch_add(1) << ".json";
#endif
        path_ = oss.str();
    }
    ~TempFile() { std::remove(path_.c_str()); }
    const std::string& path() const noexcept { return path_; }

private:
    std::string path_;
};

// Synth helper: fit a small interpolator, return the (opts, FitResult) pair
// used internally.  Encapsulates the extra-include verbosity.
struct FitFixture {
    InterpolatorOptions opts;
    FitResult           fr;
};

FitFixture make_gaussian_fixture(std::mt19937& rng, Index N = 20, Index D = 3,
                                 Index M = 1) {
    MatrixX C = random_matrix(rng, N, D);
    MatrixX Y = random_matrix(rng, N, M);
    InterpolatorOptions opts(KernelParams(KernelType::kGaussian, 1.0));
    rbfmax::solver::FitOptions fit_opts(opts.kernel, opts.poly_degree);
    FitResult fr = rbfmax::solver::fit(C, Y, fit_opts, 1e-8);
    return FitFixture{opts, fr};
}

}  // namespace

// =============================================================================
//  A — Round-trip bit-identity (5)
// =============================================================================

TEST(IoJsonRoundTrip, GaussianBitIdenticalWeights) {
    std::mt19937 rng(kSeed);
    auto fix = make_gaussian_fixture(rng);
    ASSERT_EQ(fix.fr.status, FitStatus::OK);

    TempFile tf("gauss_roundtrip");
    ASSERT_TRUE(rbfmax::io_json::save(fix.opts, fix.fr, tf.path()));

    InterpolatorOptions out_opts;
    FitResult           out_fr;
    ASSERT_TRUE(rbfmax::io_json::load(out_opts, out_fr, tf.path()));

    EXPECT_EQ(fix.fr.weights,     out_fr.weights);
    EXPECT_EQ(fix.fr.centers,     out_fr.centers);
    EXPECT_EQ(fix.fr.poly_coeffs, out_fr.poly_coeffs);
    EXPECT_EQ(out_opts.kernel.type, KernelType::kGaussian);
    EXPECT_EQ(out_opts.kernel.eps, fix.opts.kernel.eps);
}

TEST(IoJsonRoundTrip, CubicWithPolyDegree1Preserved) {
    std::mt19937 rng(kSeed);
    const Index N = 25, D = 3;
    MatrixX C = random_matrix(rng, N, D);
    MatrixX Y = random_matrix(rng, N, 1);

    InterpolatorOptions opts(KernelParams(KernelType::kCubic, 1.0));
    opts.poly_degree = 1;
    rbfmax::solver::FitOptions fo(opts.kernel, opts.poly_degree);
    FitResult fr = rbfmax::solver::fit(C, Y, fo, 1e-8);
    ASSERT_EQ(fr.status, FitStatus::OK);
    ASSERT_GT(fr.poly_coeffs.rows(), 0);

    TempFile tf("cubic_roundtrip");
    ASSERT_TRUE(rbfmax::io_json::save(opts, fr, tf.path()));

    InterpolatorOptions out_opts;
    FitResult           out_fr;
    ASSERT_TRUE(rbfmax::io_json::load(out_opts, out_fr, tf.path()));

    EXPECT_EQ(fr.weights,     out_fr.weights);
    EXPECT_EQ(fr.poly_coeffs, out_fr.poly_coeffs);
    EXPECT_EQ(out_opts.poly_degree, 1);
    EXPECT_EQ(out_opts.kernel.type, KernelType::kCubic);
}

TEST(IoJsonRoundTrip, ThinPlateSplinePreserved) {
    std::mt19937 rng(kSeed);
    const Index N = 20, D = 2;
    MatrixX C = random_matrix(rng, N, D);
    MatrixX Y = random_matrix(rng, N, 1);

    InterpolatorOptions opts(KernelParams(KernelType::kThinPlateSpline, 1.0));
    rbfmax::solver::FitOptions fo(opts.kernel, opts.poly_degree);
    FitResult fr = rbfmax::solver::fit(C, Y, fo, 1e-8);
    ASSERT_EQ(fr.status, FitStatus::OK);

    TempFile tf("tps_roundtrip");
    ASSERT_TRUE(rbfmax::io_json::save(opts, fr, tf.path()));

    InterpolatorOptions out_opts;
    FitResult           out_fr;
    ASSERT_TRUE(rbfmax::io_json::load(out_opts, out_fr, tf.path()));

    EXPECT_EQ(fr.weights,     out_fr.weights);
    EXPECT_EQ(fr.centers,     out_fr.centers);
    EXPECT_EQ(fr.poly_coeffs, out_fr.poly_coeffs);
    EXPECT_EQ(out_opts.kernel.type, KernelType::kThinPlateSpline);
}

TEST(IoJsonRoundTrip, InverseMultiquadricPreserved) {
    std::mt19937 rng(kSeed);
    const Index N = 20, D = 3;
    MatrixX C = random_matrix(rng, N, D);
    MatrixX Y = random_matrix(rng, N, 1);

    InterpolatorOptions opts(KernelParams(KernelType::kInverseMultiquadric, 0.5));
    rbfmax::solver::FitOptions fo(opts.kernel, opts.poly_degree);
    FitResult fr = rbfmax::solver::fit(C, Y, fo, 1e-8);
    ASSERT_EQ(fr.status, FitStatus::OK);

    TempFile tf("imq_roundtrip");
    ASSERT_TRUE(rbfmax::io_json::save(opts, fr, tf.path()));

    InterpolatorOptions out_opts;
    FitResult           out_fr;
    ASSERT_TRUE(rbfmax::io_json::load(out_opts, out_fr, tf.path()));

    EXPECT_EQ(fr.weights, out_fr.weights);
    EXPECT_EQ(out_opts.kernel.type, KernelType::kInverseMultiquadric);
    EXPECT_EQ(out_opts.kernel.eps, 0.5);
}

TEST(IoJsonRoundTrip, MultiOutputTargetsPreserved) {
    std::mt19937 rng(kSeed);
    auto fix = make_gaussian_fixture(rng, /*N*/ 15, /*D*/ 3, /*M*/ 3);
    ASSERT_EQ(fix.fr.status, FitStatus::OK);
    ASSERT_EQ(fix.fr.weights.cols(), 3);

    TempFile tf("multi_roundtrip");
    ASSERT_TRUE(rbfmax::io_json::save(fix.opts, fix.fr, tf.path()));

    InterpolatorOptions out_opts;
    FitResult           out_fr;
    ASSERT_TRUE(rbfmax::io_json::load(out_opts, out_fr, tf.path()));

    EXPECT_EQ(out_fr.weights.cols(), 3);
    EXPECT_EQ(fix.fr.weights, out_fr.weights);
}

// =============================================================================
//  B — Schema version + missing-field rejection (3)
// =============================================================================

TEST(IoJsonSchema, SavedFileContainsSchemaField) {
    std::mt19937 rng(kSeed);
    auto fix = make_gaussian_fixture(rng);
    TempFile tf("schema_field");
    ASSERT_TRUE(rbfmax::io_json::save(fix.opts, fix.fr, tf.path()));

    std::ifstream fs(tf.path());
    ASSERT_TRUE(fs.is_open());
    std::stringstream buf;
    buf << fs.rdbuf();
    const std::string text = buf.str();
    EXPECT_NE(text.find("\"schema\""),       std::string::npos);
    EXPECT_NE(text.find("\"rbfmax/v1\""),    std::string::npos);
}

TEST(IoJsonSchema, UnknownSchemaLoadFails) {
    TempFile tf("unknown_schema");
    {
        std::ofstream fs(tf.path());
        ASSERT_TRUE(fs.is_open());
        fs << R"({"schema":"rbfmax/v999","data":{}})";
    }
    InterpolatorOptions sentinel_opts(KernelParams(KernelType::kCubic, 7.5));
    sentinel_opts.poly_degree = 42;
    FitResult sentinel_fr;
    sentinel_fr.status = FitStatus::SINGULAR_MATRIX;

    InterpolatorOptions out_opts = sentinel_opts;
    FitResult           out_fr   = sentinel_fr;
    EXPECT_FALSE(rbfmax::io_json::load(out_opts, out_fr, tf.path()));

    // out_* must be untouched (atomic-update contract).
    EXPECT_EQ(out_opts.kernel.type, KernelType::kCubic);
    EXPECT_EQ(out_opts.poly_degree, 42);
    EXPECT_EQ(out_fr.status, FitStatus::SINGULAR_MATRIX);
}

TEST(IoJsonSchema, MissingRequiredFieldLoadFails) {
    // v1 schema but no "data" object.
    TempFile tf("missing_data");
    {
        std::ofstream fs(tf.path());
        ASSERT_TRUE(fs.is_open());
        fs << R"({
          "schema": "rbfmax/v1",
          "config": {
            "kernel": {"type": "Gaussian", "eps": 1.0},
            "poly_degree": -1,
            "kdtree_threshold": 256,
            "knn_neighbors": 0,
            "force_dense": false
          },
          "training": {
            "lambda_used": 1e-8,
            "solver_path": "LLT",
            "status": "OK",
            "condition_number": null,
            "residual_norm": 0.0
          }
        })";
    }
    InterpolatorOptions sentinel_opts(KernelParams(KernelType::kQuintic, 2.5));
    FitResult           sentinel_fr;

    InterpolatorOptions out_opts = sentinel_opts;
    FitResult           out_fr   = sentinel_fr;
    EXPECT_FALSE(rbfmax::io_json::load(out_opts, out_fr, tf.path()));

    EXPECT_EQ(out_opts.kernel.type, KernelType::kQuintic);
}

// =============================================================================
//  C — File-system + corrupt input (3)
// =============================================================================

TEST(IoJsonFile, NonExistentPathLoadReturnsFalse) {
    InterpolatorOptions out_opts;
    FitResult           out_fr;
#if defined(_WIN32)
    const std::string bogus = "Z:\\definitely_does_not_exist\\rbfmax.json";
#else
    const std::string bogus = "/tmp/__rbfmax_definitely_does_not_exist_xyz.json";
#endif
    EXPECT_FALSE(rbfmax::io_json::load(out_opts, out_fr, bogus));
}

TEST(IoJsonFile, ValidSavedFileIsReadable) {
    std::mt19937 rng(kSeed);
    auto fix = make_gaussian_fixture(rng);
    TempFile tf("readable");
    ASSERT_TRUE(rbfmax::io_json::save(fix.opts, fix.fr, tf.path()));

    std::ifstream fs(tf.path(), std::ios::binary | std::ios::ate);
    ASSERT_TRUE(fs.is_open());
    const auto size = fs.tellg();
    EXPECT_GT(static_cast<long long>(size), 0);
}

TEST(IoJsonFile, CorruptedJsonLoadReturnsFalse) {
    TempFile tf("corrupted");
    {
        std::ofstream fs(tf.path());
        ASSERT_TRUE(fs.is_open());
        fs << "not a valid json {";
    }
    InterpolatorOptions out_opts;
    FitResult           out_fr;
    EXPECT_FALSE(rbfmax::io_json::load(out_opts, out_fr, tf.path()));
}

// =============================================================================
//  D — Numerical fidelity + NaN/Inf lossy contract (2)
// =============================================================================

TEST(IoJsonFidelity, FullDoublePrecisionRoundTrip) {
    // Hand-craft a FitResult with values whose binary IEEE representation
    // requires the full 17 significant digits to recover uniquely.
    InterpolatorOptions opts(KernelParams(KernelType::kGaussian, 3.141592653589793));
    opts.poly_degree = -1;

    FitResult fr;
    fr.kernel      = opts.kernel;
    fr.poly_degree = -1;
    fr.lambda_used = 2.718281828459045;
    fr.solver_path = rbfmax::solver::SolverPath::LLT;
    fr.status      = FitStatus::OK;
    fr.condition_number = 0.3333333333333333;  // 1/3
    fr.residual_norm    = 0.1234567890123456;

    fr.centers.resize(2, 2);
    fr.centers << 3.141592653589793, 2.718281828459045,
                  1.4142135623730951, 1.7320508075688772;
    fr.weights.resize(2, 1);
    fr.weights << 0.3333333333333333, 0.6666666666666666;
    fr.poly_coeffs.resize(0, 1);

    TempFile tf("precision");
    ASSERT_TRUE(rbfmax::io_json::save(opts, fr, tf.path()));

    InterpolatorOptions out_opts;
    FitResult           out_fr;
    ASSERT_TRUE(rbfmax::io_json::load(out_opts, out_fr, tf.path()));

    EXPECT_EQ(out_opts.kernel.eps,       opts.kernel.eps);
    EXPECT_EQ(out_fr.lambda_used,        fr.lambda_used);
    EXPECT_EQ(out_fr.condition_number,   fr.condition_number);
    EXPECT_EQ(out_fr.residual_norm,      fr.residual_norm);
    EXPECT_EQ(out_fr.centers,            fr.centers);
    EXPECT_EQ(out_fr.weights,            fr.weights);
}

TEST(IoJsonFidelity, NanAndInfAreLossyConvertedToNaN) {
    std::mt19937 rng(kSeed);
    auto fix = make_gaussian_fixture(rng);
    fix.fr.condition_number = std::numeric_limits<Scalar>::quiet_NaN();
    fix.fr.residual_norm    = std::numeric_limits<Scalar>::infinity();

    TempFile tf("naninf");
    ASSERT_TRUE(rbfmax::io_json::save(fix.opts, fix.fr, tf.path()));

    InterpolatorOptions out_opts;
    FitResult           out_fr;
    ASSERT_TRUE(rbfmax::io_json::load(out_opts, out_fr, tf.path()));

    EXPECT_TRUE(std::isnan(out_fr.condition_number));
    // Per the documented lossy contract: Inf → null → NaN.
    EXPECT_TRUE(std::isnan(out_fr.residual_norm));
}

// =============================================================================
//  E — RBFInterpolator convenience methods (1)
// =============================================================================

TEST(RBFInterpolatorIo, SaveLoadPredictConsistent) {
    std::mt19937 rng(kSeed);
    const Index N = 30, D = 3;
    MatrixX C = random_matrix(rng, N, D);
    MatrixX Y = random_matrix(rng, N, 1);

    RBFInterpolator src(InterpolatorOptions(KernelParams(KernelType::kGaussian, 1.0)));
    ASSERT_EQ(src.fit(C, Y, 1e-8), FitStatus::OK);

    TempFile tf("interp_save");
    ASSERT_TRUE(src.save(tf.path()));

    RBFInterpolator dst;
    ASSERT_TRUE(dst.load(tf.path()));
    EXPECT_TRUE(dst.is_fitted());
    EXPECT_EQ(dst.n_centers(), N);
    EXPECT_EQ(dst.dim(), D);

    MatrixX X = random_matrix(rng, 20, D);
    for (Index i = 0; i < X.rows(); ++i) {
        VectorX q = X.row(i).transpose();
        Scalar  a = src.predict_scalar(q);
        Scalar  b = dst.predict_scalar(q);
        EXPECT_NEAR(a, b, 1e-14) << "row=" << i;
    }
}
