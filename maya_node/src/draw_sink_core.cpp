// =============================================================================
// maya_node/src/draw_sink_core.cpp — Phase 2B Slice 13
// -----------------------------------------------------------------------------
// Orchestration helpers for the IDrawSink abstraction.  Pure C++,
// Maya-free; the adapter GTest suite links this TU directly.
// =============================================================================
#include "rbfmax/maya/draw_sink.hpp"

namespace rbfmax {
namespace maya {

void emit_centers_draw_calls(const std::vector<Eigen::Vector3d>& centers,
                             const std::array<float, 4>& color,
                             double sphere_radius,
                             IDrawSink& sink) noexcept {
    sink.begin();
    for (const auto& p : centers) {
        DrawCall c;
        c.type   = DrawCall::Type::kSphere;
        c.p0     = p;
        c.p1     = Eigen::Vector3d::Zero();
        c.radius = sphere_radius;
        c.color  = color;
        sink.emit(c);
    }
    sink.end();
}

}  // namespace maya
}  // namespace rbfmax
