// =============================================================================
// rbfmax/maya/adapter_core.hpp — Phase 2A Slice 10A
// -----------------------------------------------------------------------------
// Pure-C++ adapter logic — testable without the Maya runtime.
//
// Slice 10A intentionally ships exactly one function, hello_transform,
// as the minimum-surface proof that the Phase 1 kernel library links
// and runs from inside a Maya plugin translation unit.  Slice 11 will
// expand this header with real kernel-tap helpers (fit / predict
// forwarding + dynamic array attribute marshalling).
//
// C++ standard discipline
// -----------------------
// This header is included from two targets with different C++ levels:
//   * rbfmax_maya_node   — C++14 (Maya 2022 ABI)
//   * test_adapter_core  — C++17 (CI compatible with Phase 1 kernel)
// Therefore this header MUST remain C++14-compliant: no if constexpr,
// no structured bindings, no std::optional, no inline variables, no
// fold expressions.
// =============================================================================
#pragma once

#include <cmath>
#include <cstddef>
#include <fstream>
#include <string>
#include <vector>

#include <Eigen/Core>

#include <rbfmax/kernel_functions.hpp>
#include <rbfmax/types.hpp>

namespace rbfmax {
namespace maya {

// =========================================================================
// Slice 10A HelloNode transform (retained for legacy aInputValue path).
// =========================================================================
//
// API verified against kernel/include/rbfmax/kernel_functions.hpp:195:
//   Scalar rbfmax::evaluate_kernel(KernelType, Scalar r, Scalar eps)
// (flat namespace, r-before-eps).
inline Scalar hello_transform(Scalar x) noexcept {
    const Scalar r = std::abs(x);
    return rbfmax::evaluate_kernel(KernelType::kGaussian, r, Scalar(1.0));
}

// =========================================================================
// Slice 11 attribute-adapter helpers.
// =========================================================================
// These three functions live on the pure-C++ side so the GTest suite can
// cover the attribute marshalling without pulling in the Maya runtime.
// They MUST remain C++14-compliant (same rationale as hello_transform):
// the plugin target builds at C++14 (Maya 2022 ABI), the adapter tests
// build at C++17 — the shared header has to compile in both.

/// Copy a plain double vector (usually sourced from Maya's
/// MFnDoubleArrayData) into a fresh Eigen column vector.  size is
/// preserved; no allocator games, just a straight element-wise copy.
/// Returned by value; callers relying on the no-allocation path pass
/// through Slice 06's ScratchPool at the RBFInterpolator level.
inline VectorX double_vector_to_eigen(const std::vector<double>& in) noexcept {
    VectorX out(static_cast<Eigen::Index>(in.size()));
    for (Eigen::Index i = 0; i < out.size(); ++i) {
        out(i) = static_cast<Scalar>(in[static_cast<std::size_t>(i)]);
    }
    return out;
}

/// Inverse of double_vector_to_eigen.  For writing MFnDoubleArrayData
/// from an Eigen VectorX.  Not noexcept because std::vector may throw
/// std::bad_alloc; callers needing a hard noexcept boundary catch at
/// their layer (mRBFNode::compute wraps the whole body in try/catch).
inline std::vector<double> eigen_to_double_vector(const VectorX& v) {
    std::vector<double> out(static_cast<std::size_t>(v.size()));
    for (Eigen::Index i = 0; i < v.size(); ++i) {
        out[static_cast<std::size_t>(i)] = static_cast<double>(v(i));
    }
    return out;
}

/// Cheap existence + read-bit probe for a JSON path.  Returns false on
/// empty string or unreadable file.  Does NOT open-parse the content —
/// full schema validation happens later inside RBFInterpolator::load.
/// This exists so the Maya node can distinguish "no path set" from
/// "path set but broken" without paying for the JSON parse.
inline bool validate_json_path(const std::string& path) noexcept {
    if (path.empty()) return false;
    std::ifstream f(path.c_str());
    return f.is_open();
}

}  // namespace maya
}  // namespace rbfmax
