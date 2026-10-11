// Unit tests for FrameTimingTracker using synthetic Compositor_FrameTiming histories.
#include "frame_timing.hpp"

#include <cmath>
#include <cstdio>
#include <vector>

static int g_failures = 0;

#define CHECK(cond) do { if (!(cond)) { std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++g_failures; } } while (0)
#define CHECK_NEAR(a, b, eps) do { float _a = (a), _b = (b); if (std::fabs(_a - _b) > (eps)) { \
    std::printf("  FAIL %s:%d: %s = %.4f, expected %.4f\n", __FILE__, __LINE__, #a, _a, _b); ++g_failures; } } while (0)

static vr::Compositor_FrameTiming frame(uint32_t index, float gpu, float cpu, double time,
                                        uint32_t dropped = 0, uint32_t flags = 0) {
    vr::Compositor_FrameTiming f{};
    f.m_nSize = sizeof(f);
    f.m_nFrameIndex = index;
    f.m_flPreSubmitGpuMs = gpu;
    f.m_flNewPosesReadyMs = 1.0f;
    f.m_flNewFrameReadyMs = 1.0f + cpu;
    f.m_flSystemTimeInSeconds = time;
    f.m_nNumDroppedFrames = dropped;
    f.m_nReprojectionFlags = flags;
    return f;
}

static void testSkipsNewestAndAverages() {
    FrameTimingTracker t;
    std::vector<vr::Compositor_FrameTiming> h = {
        frame(1, 8.0f, 4.0f, 0.00), frame(2, 10.0f, 6.0f, 0.01), frame(3, 50.0f, 50.0f, 0.02) /* in flight */,
    };
    auto r = t.ingest(h.data(), (uint32_t)h.size());
    CHECK(r.newFrames == 2);
    CHECK(r.hasGpu && r.hasCpu);
    CHECK_NEAR(r.avgGpuMs, 9.0f, 1e-4f);
    CHECK_NEAR(r.avgCpuMs, 5.0f, 1e-4f);
}

static void testCountsEachFrameOnceAcrossPolls() {
    FrameTimingTracker t;
    std::vector<vr::Compositor_FrameTiming> a = { frame(1, 8, 4, 0.00), frame(2, 8, 4, 0.01), frame(3, 8, 4, 0.02) };
    std::vector<vr::Compositor_FrameTiming> b = { frame(2, 8, 4, 0.01), frame(3, 12, 4, 0.02), frame(4, 8, 4, 0.03) };
    CHECK(t.ingest(a.data(), 3).newFrames == 2); // frames 1, 2
    auto r = t.ingest(b.data(), 3);              // only frame 3 is new and complete
    CHECK(r.newFrames == 1);
    CHECK_NEAR(r.avgGpuMs, 12.0f, 1e-4f);
    CHECK(t.ingest(b.data(), 3).newFrames == 0); // nothing new
    CHECK(t.windowStats().frames == 3);
}

static void testFiltersInvalidSamples() {
    FrameTimingTracker t;
    std::vector<vr::Compositor_FrameTiming> h = { frame(1, 0.2f, 0.0f, 0.0), frame(2, 0.0f, 0.0f, 0.01) };
    auto r = t.ingest(h.data(), 2);
    CHECK(r.newFrames == 1);
    CHECK(!r.hasGpu);
    CHECK(!r.hasCpu);
}

static void testDetectsCompositorRestart() {
    FrameTimingTracker t;
    std::vector<vr::Compositor_FrameTiming> a = { frame(500, 8, 4, 10.0), frame(501, 8, 4, 10.01) };
    std::vector<vr::Compositor_FrameTiming> b = { frame(3, 8, 4, 0.0), frame(4, 8, 4, 0.01), frame(5, 8, 4, 0.02) };
    t.ingest(a.data(), 2);
    CHECK(t.ingest(b.data(), 3).newFrames == 2);
}

static void testWindowStatsCountsAndTrims() {
    using namespace vr;
    FrameTimingTracker t;
    std::vector<Compositor_FrameTiming> a = {
        frame(1, 8, 4, 0.0, 1, VRCompositor_ReprojectionReason_Cpu),
        frame(2, 8, 4, 0.1, 0, VRCompositor_ReprojectionReason_Gpu | VRCompositor_ReprojectionMotion),
        frame(3, 8, 4, 0.2, 2, VRCompositor_ReprojectionReason_Gpu),
        frame(4, 8, 4, 0.3),
    };
    t.ingest(a.data(), (uint32_t)a.size());
    auto s = t.windowStats();
    CHECK(s.frames == 3);
    CHECK(s.droppedFrames == 3);
    CHECK(s.lateFrames == 3);
    CHECK(s.cpuReprojected == 1);
    CHECK(s.gpuReprojected == 2);
    CHECK(s.motionSmoothed == 1);

    // 6 s later the old frames fall out of the 5 s window
    std::vector<Compositor_FrameTiming> b = { frame(5, 8, 4, 6.0), frame(6, 8, 4, 6.01) };
    t.ingest(b.data(), 2);
    s = t.windowStats();
    CHECK(s.frames == 1);
    CHECK(s.droppedFrames == 0);
}

static void testClearWindowKeepsPosition() {
    FrameTimingTracker t;
    std::vector<vr::Compositor_FrameTiming> a = { frame(1, 8, 4, 0.0), frame(2, 8, 4, 0.01), frame(3, 8, 4, 0.02) };
    t.ingest(a.data(), 3);
    t.clearWindow();
    CHECK(t.windowStats().frames == 0);
    CHECK(t.ingest(a.data(), 3).newFrames == 0); // already-counted frames aren't re-read
}

int main() {
    struct { const char* name; void (*fn)(); } tests[] = {
        {"skips newest frame and averages", testSkipsNewestAndAverages},
        {"counts each frame once across polls", testCountsEachFrameOnceAcrossPolls},
        {"filters invalid samples", testFiltersInvalidSamples},
        {"detects compositor restart", testDetectsCompositorRestart},
        {"window stats count flags and trim by time", testWindowStatsCountsAndTrims},
        {"clearWindow keeps read position", testClearWindowKeepsPosition},
    };
    for (const auto& t : tests) {
        int before = g_failures;
        t.fn();
        std::printf("%s %s\n", g_failures == before ? "ok  " : "FAIL", t.name);
    }
    std::printf("\n%s (%d failure%s)\n", g_failures ? "FAILED" : "PASSED", g_failures, g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}
