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

#include <rbfmax/kernel_functions.hpp>
#include <rbfmax/types.hpp>

namespace rbfmax {
namespace maya {

// Slice 10A HelloNode transform.  Evaluates the Phase 1 Gaussian kernel
// at r = |x| with eps = 1.0.
//
// API verified against kernel/include/rbfmax/kernel_functions.hpp:195:
//   Scalar rbfmax::evaluate_kernel(KernelType, Scalar r, Scalar eps)
// (flat namespace, r-before-eps — confirmed by grep before writing).
inline Scalar hello_transform(Scalar x) noexcept {
    const Scalar r = std::abs(x);
    return rbfmax::evaluate_kernel(KernelType::kGaussian, r, Scalar(1.0));
}

}  // namespace maya
}  // namespace rbfmax
