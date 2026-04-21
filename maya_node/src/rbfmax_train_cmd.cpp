// =============================================================================
// maya_node/src/rbfmax_train_cmd.cpp — Phase 2A Slice 12
// -----------------------------------------------------------------------------
// Implementation of rbfmaxTrainAndSave.  See rbfmax_train_cmd.hpp for
// contract / usage; see DEVLOG Slice 12 for design-decision rationale.
// =============================================================================
#include "rbfmax/maya/rbfmax_train_cmd.hpp"

#include <exception>
#include <memory>
#include <string>
#include <vector>

#include <maya/MArgDatabase.h>
#include <maya/MArgList.h>
#include <maya/MGlobal.h>
#include <maya/MString.h>
#include <maya/MSyntax.h>

#include "rbfmax/interpolator.hpp"
#include "rbfmax/kernel_functions.hpp"
#include "rbfmax/maya/adapter_core.hpp"
#include "rbfmax/solver.hpp"
#include "rbfmax/types.hpp"

namespace rbfmax {
namespace maya {

// -----------------------------------------------------------------------------
//  Flag spellings (short / long)
// -----------------------------------------------------------------------------

namespace {

const char* const kF_centers       = "-c";
const char* const kF_centersL      = "-centers";
const char* const kF_targets       = "-t";
const char* const kF_targetsL      = "-targets";

const char* const kF_centersFile   = "-cf";
const char* const kF_centersFileL  = "-centersFile";
const char* const kF_targetsFile   = "-tf";
const char* const kF_targetsFileL  = "-targetsFile";

// F5 findings (Slice 12 executor run): Maya's MSyntax::addFlag silently
// rejects short names whose first character matches a Maya-reserved
// global-command prefix: -d (debug), -e (edit), -h (help), -q (query).
// Observed: -di and -ep both returned kFailure "Unexpected Internal
// Failure" despite looking like valid non-reserved abbreviations.
// Workaround: start every short name with a letter that is not in
// {d,e,h,q}.  We use "ki" (kernel input dim), "ks" (kernel scale=eps),
// etc., to stay mnemonic while avoiding the reserved first letters.
// F5 root cause (Slice 12): Maya's addFlag silently rejects long names
// shorter than 4 characters — "dim" (3) and "eps" (3) both failed.
// All working long names in the canonical Autodesk samples and in
// this command are >= 4 chars.  Renamed to inputDim / epsilon.
const char* const kF_dim           = "-idm";
const char* const kF_dimL          = "-inputDim";
const char* const kF_outputDim     = "-od";
const char* const kF_outputDimL    = "-outputDim";

const char* const kF_jsonPath      = "-jp";
const char* const kF_jsonPathL     = "-jsonPath";

const char* const kF_kernel        = "-kn";
const char* const kF_kernelL       = "-kernel";
const char* const kF_eps           = "-ep";
const char* const kF_epsL          = "-epsilon";
const char* const kF_polyDegree    = "-pd";
const char* const kF_polyDegreeL   = "-polyDegree";
const char* const kF_lambda        = "-lm";
const char* const kF_lambdaL       = "-lambda";
const char* const kF_force         = "-fo";
const char* const kF_forceL        = "-force";

// Emit descriptive MGlobal error + convert to MS::kFailure.
MStatus fail(const std::string& msg) {
    MGlobal::displayError(
        MString("rbfmaxTrainAndSave: ") + MString(msg.c_str()));
    return MS::kFailure;
}

const char* fit_status_name(solver::FitStatus s) noexcept {
    switch (s) {
        case solver::FitStatus::OK:                    return "OK";
        case solver::FitStatus::INSUFFICIENT_SAMPLES:  return "INSUFFICIENT_SAMPLES";
        case solver::FitStatus::SINGULAR_MATRIX:       return "SINGULAR_MATRIX";
        case solver::FitStatus::INVALID_INPUT:         return "INVALID_INPUT";
    }
    return "Unknown";
}

}  // namespace

const MString RbfmaxTrainCmd::kCommandName{"rbfmaxTrainAndSave"};

// -----------------------------------------------------------------------------
//  MSyntax
// -----------------------------------------------------------------------------

MSyntax RbfmaxTrainCmd::newSyntax() {
    MSyntax s;

    // Inline-mode flags: -centers / -targets are multi-use kDouble so a
    // Python list is accepted per Maya's script binding conventions.
    s.addFlag(kF_centers,     kF_centersL,     MSyntax::kDouble);
    s.makeFlagMultiUse(kF_centers);
    s.addFlag(kF_targets,     kF_targetsL,     MSyntax::kDouble);
    s.makeFlagMultiUse(kF_targets);

    // CSV-mode flags.
    s.addFlag(kF_centersFile, kF_centersFileL, MSyntax::kString);
    s.addFlag(kF_targetsFile, kF_targetsFileL, MSyntax::kString);

    // Inline-mode shape.  NOTE: long names must be >= 4 chars — Maya's
    // MSyntax::addFlag silently rejects 3-char long names.  "dim" and
    // "eps" were caught by F5; renamed to "inputDim" / "epsilon".
    s.addFlag(kF_dim,         kF_dimL,         MSyntax::kLong);
    s.addFlag(kF_outputDim,   kF_outputDimL,   MSyntax::kLong);

    // Shared flags.
    s.addFlag(kF_jsonPath,    kF_jsonPathL,    MSyntax::kString);
    s.addFlag(kF_kernel,      kF_kernelL,      MSyntax::kString);
    s.addFlag(kF_eps,         kF_epsL,         MSyntax::kDouble);
    s.addFlag(kF_polyDegree,  kF_polyDegreeL,  MSyntax::kLong);
    s.addFlag(kF_lambda,      kF_lambdaL,      MSyntax::kString);
    s.addFlag(kF_force,       kF_forceL,       MSyntax::kBoolean);

    return s;
}

// -----------------------------------------------------------------------------
//  Multi-use flag reader
// -----------------------------------------------------------------------------

namespace {

// Read every use of a multi-use kDouble flag into out.  Returns kSuccess
// even if the flag was not supplied (out stays empty).  Maya's
// MArgParser::getFlagArgument reads ONE double from each flag use; for
// a Python list arg Maya translates every element into its own flag use.
MStatus read_multi_double(const MArgDatabase& adb,
                          const char*         flag_short,
                          std::vector<double>& out) {
    out.clear();
    const unsigned int n = adb.numberOfFlagUses(flag_short);
    out.reserve(n);
    for (unsigned int i = 0; i < n; ++i) {
        MArgList argList;
        MStatus st = adb.getFlagArgumentList(flag_short, i, argList);
        if (!st) return st;
        // argList for a kDouble flag has one double at index 0.
        double v = 0.0;
        st = argList.get(0u, v);
        if (!st) return st;
        out.push_back(v);
    }
    return MS::kSuccess;
}

}  // namespace

// -----------------------------------------------------------------------------
//  doIt — the whole pipeline
// -----------------------------------------------------------------------------

MStatus RbfmaxTrainCmd::doIt(const MArgList& args) {
    MStatus st;
    MArgDatabase adb(syntax(), args, &st);
    if (!st) {
        return fail(std::string("cannot parse arguments: ") + st.errorString().asChar());
    }

    // ---- Required: jsonPath -------------------------------------------------
    if (!adb.isFlagSet(kF_jsonPath)) {
        return fail("missing required flag -jsonPath / -jp");
    }
    MString mJsonPath;
    adb.getFlagArgument(kF_jsonPath, 0, mJsonPath);
    const std::string jsonPath = mJsonPath.asChar();
    if (jsonPath.empty()) {
        return fail("-jsonPath must not be empty");
    }

    // ---- Shared defaults ----------------------------------------------------
    std::string kernel_str  = "Gaussian";
    double      eps         = 1.0;
    int         poly_degree = -1;
    std::string lambda_str  = "auto";
    bool        force       = false;

    if (adb.isFlagSet(kF_kernel)) {
        MString v; adb.getFlagArgument(kF_kernel, 0, v);
        kernel_str = v.asChar();
    }
    if (adb.isFlagSet(kF_eps)) {
        adb.getFlagArgument(kF_eps, 0, eps);
    }
    if (adb.isFlagSet(kF_polyDegree)) {
        adb.getFlagArgument(kF_polyDegree, 0, poly_degree);
    }
    if (adb.isFlagSet(kF_lambda)) {
        MString v; adb.getFlagArgument(kF_lambda, 0, v);
        lambda_str = v.asChar();
    }
    if (adb.isFlagSet(kF_force)) {
        adb.getFlagArgument(kF_force, 0, force);
    }

    // ---- Mode detection -----------------------------------------------------
    const bool has_inline =
        adb.isFlagSet(kF_centers) || adb.isFlagSet(kF_targets);
    const bool has_csv =
        adb.isFlagSet(kF_centersFile) || adb.isFlagSet(kF_targetsFile);
    if (has_inline && has_csv) {
        return fail("modes are mutually exclusive: pass either "
                    "-centers/-targets or -centersFile/-targetsFile, "
                    "not both");
    }
    if (!has_inline && !has_csv) {
        return fail("must supply either -centers and -targets (inline "
                    "mode) or -centersFile and -targetsFile (csv mode)");
    }

    // ---- Load data ----------------------------------------------------------
    MatrixX centers;
    MatrixX targets;

    try {
        if (has_csv) {
            if (!adb.isFlagSet(kF_centersFile) || !adb.isFlagSet(kF_targetsFile)) {
                return fail("csv mode requires both -centersFile and "
                            "-targetsFile");
            }
            MString mC, mT;
            adb.getFlagArgument(kF_centersFile, 0, mC);
            adb.getFlagArgument(kF_targetsFile, 0, mT);
            std::string err_reason;
            if (!parse_csv_matrix(mC.asChar(), centers, err_reason)) {
                return fail("centers csv parse failed: " + err_reason);
            }
            if (!parse_csv_matrix(mT.asChar(), targets, err_reason)) {
                return fail("targets csv parse failed: " + err_reason);
            }
            if (centers.rows() != targets.rows()) {
                return fail("csv row count mismatch: centers has " +
                            std::to_string(static_cast<long long>(centers.rows())) +
                            " rows, targets has " +
                            std::to_string(static_cast<long long>(targets.rows())));
            }
        } else {
            // Inline mode.
            if (!adb.isFlagSet(kF_dim) || !adb.isFlagSet(kF_outputDim)) {
                return fail("inline mode requires both -dim and -outputDim");
            }
            int dim = 0, odim = 0;
            adb.getFlagArgument(kF_dim, 0, dim);
            adb.getFlagArgument(kF_outputDim, 0, odim);
            if (dim <= 0 || odim <= 0) {
                return fail("-dim and -outputDim must be positive");
            }

            std::vector<double> c_flat, t_flat;
            st = read_multi_double(adb, kF_centers, c_flat);
            if (!st) return fail(std::string("cannot read -centers: ") +
                                  st.errorString().asChar());
            st = read_multi_double(adb, kF_targets, t_flat);
            if (!st) return fail(std::string("cannot read -targets: ") +
                                  st.errorString().asChar());

            if (!unflatten_double_array(c_flat,
                    static_cast<Eigen::Index>(dim), centers)) {
                return fail("-centers flat length " +
                            std::to_string(c_flat.size()) +
                            " is not a multiple of -dim " +
                            std::to_string(dim));
            }
            if (!unflatten_double_array(t_flat,
                    static_cast<Eigen::Index>(odim), targets)) {
                return fail("-targets flat length " +
                            std::to_string(t_flat.size()) +
                            " is not a multiple of -outputDim " +
                            std::to_string(odim));
            }
            if (centers.rows() != targets.rows()) {
                return fail("row count mismatch: centers has " +
                            std::to_string(static_cast<long long>(centers.rows())) +
                            " samples, targets has " +
                            std::to_string(static_cast<long long>(targets.rows())));
            }
        }
    } catch (const std::exception& ex) {
        return fail(std::string("data load exception: ") + ex.what());
    }

    // ---- Validate kernel string --------------------------------------------
    KernelType ktype;
    if (!kernel_type_from_string(kernel_str.c_str(), ktype)) {
        return fail("unknown kernel: \"" + kernel_str + "\" (valid: "
                    "Linear | Cubic | Quintic | ThinPlateSpline | "
                    "Gaussian | InverseMultiquadric)");
    }

    // ---- --force gate on existing file -------------------------------------
    if (!force && file_exists(jsonPath)) {
        return fail("file exists; pass -force true to overwrite: " + jsonPath);
    }

    // ---- Parse lambda -------------------------------------------------------
    bool   lambda_auto  = false;
    Scalar lambda_value = 0;
    try {
        if (!parse_lambda_arg(lambda_str, lambda_auto, lambda_value)) {
            return fail("cannot parse -lambda: \"" + lambda_str +
                        "\" (expected \"auto\" or a numeric like \"1e-6\")");
        }
    } catch (const std::exception& ex) {
        return fail(std::string("lambda parse exception: ") + ex.what());
    }

    // ---- Construct + fit + save --------------------------------------------
    try {
        InterpolatorOptions opts(KernelParams(ktype, static_cast<Scalar>(eps)));
        opts.poly_degree = poly_degree;

        RBFInterpolator rbf(opts);
        solver::FitStatus fs = lambda_auto
            ? rbf.fit(centers, targets, solver::kLambdaAuto)
            : rbf.fit(centers, targets, lambda_value);

        if (fs != solver::FitStatus::OK) {
            return fail(std::string("fit failed: ") + fit_status_name(fs));
        }

        if (!rbf.save(jsonPath)) {
            return fail("save failed (could not write schema-v1 JSON to): "
                        + jsonPath);
        }
    } catch (const std::exception& ex) {
        return fail(std::string("fit/save exception: ") + ex.what());
    }

    setResult(MString(jsonPath.c_str()));
    return MS::kSuccess;
}

}  // namespace maya
}  // namespace rbfmax
