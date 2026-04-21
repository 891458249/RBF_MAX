// =============================================================================
// maya_node/tests/test_draw_sink.cpp — Phase 2B Slice 13
// -----------------------------------------------------------------------------
// 8 TEST blocks covering the IDrawSink / DrawCall orchestration.  Pure
// C++, no Maya runtime; validates both the MockDrawSink fixture and
// emit_centers_draw_calls's behaviour (call count, color/radius/coord
// propagation, empty-input corner case, begin/end balance).
//
// Random seed kSeedS13 = 0xF5BFACu is reserved for future randomised
// additions; E1-E8 below are deterministic and do not consume it.
// =============================================================================
#include <array>
#include <cstdint>
#include <vector>

#include <Eigen/Core>
#include <gtest/gtest.h>

#include "rbfmax/maya/draw_sink.hpp"

namespace {

using rbfmax::maya::DrawCall;
using rbfmax::maya::emit_centers_draw_calls;
using rbfmax::maya::IDrawSink;

constexpr std::uint32_t kSeedS13 = 0xF5BFACu;  // Slice 13 seed reserved

// Simple recording sink used by the E-group tests.  Not thread-safe
// (matches the IDrawSink contract — each predict call's sink is
// thread-local).
class MockDrawSink : public IDrawSink {
public:
    std::vector<DrawCall> calls;
    int begin_count = 0;
    int end_count   = 0;

    void begin() noexcept override { ++begin_count; }
    void end()   noexcept override { ++end_count; }
    void emit(const DrawCall& c) noexcept override { calls.push_back(c); }
};

}  // namespace

// E1 — Direct sink usage works before we touch the orchestrator.
TEST(DrawSink, E1_MockSinkRecordsEmittedCalls) {
    MockDrawSink sink;
    sink.begin();
    DrawCall c;
    c.type   = DrawCall::Type::kSphere;
    c.p0     = Eigen::Vector3d(1, 2, 3);
    c.radius = 0.5;
    c.color  = {1.f, 0.f, 0.f, 1.f};
    sink.emit(c);
    sink.end();

    EXPECT_EQ(sink.begin_count, 1);
    EXPECT_EQ(sink.end_count, 1);
    ASSERT_EQ(sink.calls.size(), 1u);
    EXPECT_EQ(sink.calls[0].type, DrawCall::Type::kSphere);
    EXPECT_DOUBLE_EQ(sink.calls[0].p0.x(), 1.0);
    EXPECT_DOUBLE_EQ(sink.calls[0].radius, 0.5);
}

// E2 — 4 centers in, 4 kSphere DrawCalls out.
TEST(DrawSink, E2_EmitCentersSimpleCount) {
    MockDrawSink sink;
    const std::vector<Eigen::Vector3d> centers = {
        {0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {1, 1, 0}
    };
    emit_centers_draw_calls(centers, {1.f, 1.f, 1.f, 1.f}, 0.05, sink);
    ASSERT_EQ(sink.calls.size(), 4u);
    for (const auto& c : sink.calls) {
        EXPECT_EQ(c.type, DrawCall::Type::kSphere);
    }
}

// E3 — Color argument propagates element-wise.
TEST(DrawSink, E3_EmitCentersColorPropagates) {
    MockDrawSink sink;
    const std::vector<Eigen::Vector3d> centers = {{0, 0, 0}};
    const std::array<float, 4> red = {1.f, 0.f, 0.f, 1.f};
    emit_centers_draw_calls(centers, red, 0.1, sink);
    ASSERT_EQ(sink.calls.size(), 1u);
    EXPECT_EQ(sink.calls[0].color, red);
}

// E4 — Radius argument propagates.
TEST(DrawSink, E4_EmitCentersRadiusPropagates) {
    MockDrawSink sink;
    const std::vector<Eigen::Vector3d> centers = {{0, 0, 0}};
    emit_centers_draw_calls(centers, {1.f, 1.f, 1.f, 1.f}, 0.123, sink);
    ASSERT_EQ(sink.calls.size(), 1u);
    EXPECT_DOUBLE_EQ(sink.calls[0].radius, 0.123);
}

// E5 — Empty input still brackets exactly one begin / end pair.
TEST(DrawSink, E5_EmitCentersEmptyInput) {
    MockDrawSink sink;
    const std::vector<Eigen::Vector3d> centers;  // empty
    emit_centers_draw_calls(centers, {1.f, 1.f, 1.f, 1.f}, 0.05, sink);
    EXPECT_EQ(sink.calls.size(), 0u);
    EXPECT_EQ(sink.begin_count, 1);
    EXPECT_EQ(sink.end_count, 1);
}

// E6 — Coordinates are preserved bit-exactly (memcpy-equivalent path).
TEST(DrawSink, E6_EmitCentersCoordsPreserved) {
    MockDrawSink sink;
    const std::vector<Eigen::Vector3d> centers = {
        {1.5, 2.5, 3.5}, {-0.25, 0.0, 100.0}
    };
    emit_centers_draw_calls(centers, {1.f, 1.f, 1.f, 1.f}, 0.05, sink);
    ASSERT_EQ(sink.calls.size(), 2u);
    EXPECT_DOUBLE_EQ(sink.calls[0].p0.x(),  1.5);
    EXPECT_DOUBLE_EQ(sink.calls[0].p0.y(),  2.5);
    EXPECT_DOUBLE_EQ(sink.calls[0].p0.z(),  3.5);
    EXPECT_DOUBLE_EQ(sink.calls[1].p0.x(), -0.25);
    EXPECT_DOUBLE_EQ(sink.calls[1].p0.y(),  0.0);
    EXPECT_DOUBLE_EQ(sink.calls[1].p0.z(),  100.0);
}

// E7 — begin/end always balanced regardless of center count.
TEST(DrawSink, E7_EmitCentersBeginEndBalanced) {
    MockDrawSink sink;
    const std::vector<Eigen::Vector3d> centers(10, Eigen::Vector3d::Zero());
    emit_centers_draw_calls(centers, {1.f, 1.f, 1.f, 1.f}, 0.05, sink);
    EXPECT_EQ(sink.begin_count, 1);
    EXPECT_EQ(sink.end_count, 1);
    EXPECT_EQ(sink.calls.size(), 10u);
}

// E8 — DrawCall is default-constructible and enums/defaults are sane.
// Also anchors kSeedS13 so -Wunused-variable stays quiet under /WX.
TEST(DrawSink, E8_DrawCallDefaultsAreSane) {
    DrawCall c;
    EXPECT_EQ(c.type, DrawCall::Type::kPoint);
    EXPECT_DOUBLE_EQ(c.p0.x(), 0.0);
    EXPECT_DOUBLE_EQ(c.radius, 0.0);
    // Anchor the reserved seed so it survives /WX.
    EXPECT_NE(kSeedS13, 0u);
    SUCCEED();
}
