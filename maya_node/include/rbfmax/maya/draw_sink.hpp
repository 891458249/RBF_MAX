// =============================================================================
// rbfmax/maya/draw_sink.hpp — Phase 2B Slice 13
// -----------------------------------------------------------------------------
// Maya-free abstraction for accumulating Viewport 2.0 draw primitives.
// Lets the orchestration logic (which centers to draw, in what colour,
// at what radius) stay pure C++ and GTest-covered.  Slice 13 ships the
// abstraction + one orchestrator (emit_centers_draw_calls) but the
// production mRBFDrawOverride does not yet route through it — we call
// MUIDrawManager directly from addUIDrawables because the single draw
// topology (one sphere per center) does not need an intermediate layer.
// Slice 14 / 15 may introduce a RealDrawSink wrapper once heat-map and
// X-ray variants need to share orchestration.
//
// C++ standard discipline: C++14-compliant so this header compiles in
// both the plugin target (C++14 for Maya 2022 ABI) and the adapter test
// target (C++17).  No std::optional, no if constexpr, no concepts.
// =============================================================================
#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include <Eigen/Core>

#include <rbfmax/types.hpp>

namespace rbfmax {
namespace maya {

/// POD describing a single primitive the DrawOverride wants rendered.
/// Fields are ordered roughly by frequency of use; defaults match the
/// "no-op point at origin" state so zero-initialisation is well-defined.
struct DrawCall {
    enum class Type : std::uint8_t {
        kPoint  = 0,
        kLine   = 1,
        kSphere = 2,
        kText   = 3,
    };
    Type                  type   = Type::kPoint;
    Eigen::Vector3d       p0     = Eigen::Vector3d::Zero();  // point / sphere / line-start
    Eigen::Vector3d       p1     = Eigen::Vector3d::Zero();  // line-end (kLine) or unused
    double                radius = 0.0;                       // kSphere
    std::array<float, 4>  color  = {1.0f, 1.0f, 1.0f, 1.0f};  // RGBA
};

/// Sink receiving DrawCalls.  Two concrete implementations ship in the
/// project:
///
///   * MockDrawSink (tests/test_draw_sink.cpp) — records calls for
///     GTest assertion.
///   * A real Maya-backed sink may appear in a later slice; Slice 13
///     deliberately does not ship one because the current DrawOverride
///     topology does not need the indirection.
///
/// Contract: begin() and end() are always called in matched pairs.
/// emit() is called zero or more times between them.  All three methods
/// must be noexcept — sinks that need dynamic allocation should catch
/// and silently drop on bad_alloc.
class IDrawSink {
public:
    virtual ~IDrawSink() = default;
    virtual void begin() noexcept = 0;
    virtual void emit(const DrawCall& c) noexcept = 0;
    virtual void end()   noexcept = 0;
};

/// Orchestration helper: given a list of 3D center positions, emit one
/// filled-sphere DrawCall per center, all sharing the same color and
/// radius.  Always calls sink.begin() and sink.end() exactly once, even
/// if `centers` is empty (so sinks relying on begin/end for transaction
/// boundaries get a well-defined lifecycle).
void emit_centers_draw_calls(const std::vector<Eigen::Vector3d>& centers,
                             const std::array<float, 4>& color,
                             double sphere_radius,
                             IDrawSink& sink) noexcept;

}  // namespace maya
}  // namespace rbfmax
