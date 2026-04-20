// =============================================================================
// rbfmax/io_json.hpp
// -----------------------------------------------------------------------------
// JSON serialization layer for trained RBF interpolators.  Persists an
// (InterpolatorOptions, FitResult) pair under a versioned schema so that
// trained interpolators survive process restarts and cross-host transfer.
//
// Schema documentation: docs/schema_v1.md.
//
// Core contracts
// --------------
//   1. Schema version is locked as a string tag "rbfmax/v<N>" written to
//      the top-level "schema" field.  Future breaking changes ship a new
//      "rbfmax/v2" + dispatch branch in load(); the v1 branch is
//      permanent and never modified — old files always remain readable.
//   2. All public functions are noexcept and report failure via bool
//      return.  On load failure (file I/O, parse error, schema unknown,
//      missing required field, size mismatch, ...) the out_* parameters
//      are guaranteed unchanged from their pre-call state — load() only
//      mutates outputs after the entire parse succeeds (atomic update).
//   3. NaN / +Inf / -Inf values in Scalar fields are serialized as JSON
//      `null` (JSON spec has no IEEE special values) and deserialized
//      back as NaN — a documented lossy conversion.  Inf is NOT round-
//      tripped.
//
// The convenience methods RBFInterpolator::save / RBFInterpolator::load
// delegate to the free functions below.
// =============================================================================
#ifndef RBFMAX_IO_JSON_HPP
#define RBFMAX_IO_JSON_HPP

#include <string>

#include "rbfmax/interpolator.hpp"
#include "rbfmax/solver.hpp"
#include "rbfmax/types.hpp"

namespace rbfmax {
namespace io_json {

/// Schema tag written to / required by every save / load.  Bumped when a
/// breaking change is introduced; the prior parse_v<N>_json branch must
/// remain intact for legacy file compatibility.
constexpr const char* kCurrentSchema = "rbfmax/v1";

/// Serialize an (options, fit_result) pair to a JSON file.
///
/// Returns true on success.  Returns false on any I/O failure or if the
/// inputs are inconsistent at the JSON level (e.g. matrices that fail
/// to round-trip through nlohmann/json).  Never throws.
bool save(const InterpolatorOptions& opts,
          const solver::FitResult& fit_result,
          const std::string& path) noexcept;

/// Load an (options, fit_result) pair from a JSON file written by save().
///
/// Returns true on success.  On failure (file missing, parse error,
/// unknown schema, missing required field, matrix shape mismatch, ...),
/// returns false and out_opts / out_fit_result are guaranteed unchanged
/// from their pre-call state.  Never throws.
bool load(InterpolatorOptions& out_opts,
          solver::FitResult& out_fit_result,
          const std::string& path) noexcept;

}  // namespace io_json
}  // namespace rbfmax

#endif  // RBFMAX_IO_JSON_HPP
