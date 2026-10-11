// Unit tests for SmoothingEngine. Build target: smoothing_engine_tests (run via ctest or directly).
#include "smoothing_engine.hpp"

#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

using namespace std::chrono_literals;
using Clock = SmoothingEngine::Clock;

static int g_failures = 0;

#define CHECK(cond) do { if (!(cond)) { std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++g_failures; } } while (0)
#define CHECK_NEAR(a, b, eps) do { float _a = (a), _b = (b); if (std::fabs(_a - _b) > (eps)) { \
    std::printf("  FAIL %s:%d: %s = %.4f, expected %.4f\n", __FILE__, __LINE__, #a, _a, _b); ++g_failures; } } while (0)

// 90 Hz: frame budget 11.11 ms, GPU target 9.44 ms
static SmoothingEngine makeEngine(bool cpuBalancing = true) {
    SmoothingEngine e;
    e.configure(90.0f, 0.65f, 1.20f, 3.0f, cpuBalancing, 0.50f);
    return e;
}

// Feeds a constant sample every `step` for `duration`, threading the scale through like the app does
static float feed(SmoothingEngine& e, float gpu, float cpu, float scale, Clock::time_point& t,
                  Clock::duration duration, Clock::duration step = 16ms) {
    for (auto end = t + duration; t < end; t += step) {
        scale = e.update(gpu, cpu, scale, t);
    }
    return scale;
}

static void testDeadbandHoldsScale() {
    auto e = makeEngine();
    auto t = Clock::now();
    float scale = feed(e, 9.0f, 4.0f, 1.0f, t, 5s); // ratio 0.95, inside 0.88..1.08
    CHECK_NEAR(scale, 1.0f, 1e-6f);
}

static void testOverBudgetDropsToComputedScale() {
    auto e = makeEngine();
    auto t = Clock::now();
    float scale = e.update(12.0f, 4.0f, 1.0f, t); // 9.44 / 12 = 0.787 -> rounds to 0.78
    CHECK_NEAR(scale, 0.78f, 1e-4f);
}

static void testUnderBudgetRaisesClampedToMax() {
    auto e = makeEngine();
    auto t = Clock::now();
    float scale = e.update(4.0f, 2.0f, 1.0f, t); // would want 2.36x, max is 1.20
    CHECK_NEAR(scale, 1.20f, 1e-4f);
}

static void testClampsToMin() {
    auto e = makeEngine(false);
    auto t = Clock::now();
    float scale = e.update(40.0f, 4.0f, 1.0f, t); // would want 0.24x, min is 0.65
    CHECK_NEAR(scale, 0.65f, 1e-4f);
}

static void testCooldownBlocksModerateChanges() {
    auto e = makeEngine();
    auto t = Clock::now();
    float scale = e.update(12.0f, 4.0f, 1.0f, t); // change #1 -> 0.78
    // Still 15% over budget at the new scale: moderate, so it must wait out the 3 s cooldown
    scale = feed(e, 10.9f, 4.0f, scale, t, 2900ms);
    CHECK_NEAR(scale, 0.78f, 1e-4f);
    scale = feed(e, 10.9f, 4.0f, scale, t, 200ms);
    CHECK(scale < 0.78f);
}

static void testCooldownBlocksRaisesEvenWhenFarUnder() {
    auto e = makeEngine();
    auto t = Clock::now();
    float scale = e.update(12.0f, 4.0f, 1.0f, t); // change #1 -> 0.78
    scale = feed(e, 3.0f, 2.0f, scale, t, 2900ms); // far under budget, but raising always waits
    CHECK_NEAR(scale, 0.78f, 1e-4f);
}

static void testSevereOverrunDropsAfterSettle() {
    auto e = makeEngine();
    auto t = Clock::now();
    float scale = e.update(4.0f, 2.0f, 1.0f, t); // change #1 -> raise to 1.20
    CHECK_NEAR(scale, 1.20f, 1e-4f);
    // Heavy scene: GPU 2x budget. Not before the 1 s settle...
    float before = feed(e, 19.0f, 4.0f, scale, t, 900ms);
    CHECK_NEAR(before, 1.20f, 1e-4f);
    // ...but well before the 3 s cooldown ends
    float after = feed(e, 19.0f, 4.0f, before, t, 300ms);
    CHECK(after < 1.20f);
}

static void testTimeBasedSmoothingIndependentOfCallRate() {
    // Same signal (5 ms for 1 s, then 15 ms for 0.25 s) sampled at 30 Hz and 144 Hz should filter alike
    auto run = [](Clock::duration step) {
        auto e = makeEngine(false);
        auto t = Clock::now();
        const float scale = 1.0f; // scale decisions are ignored; only the filtered value matters here
        for (auto end = t + 1s; t < end; t += step) e.update(5.0f, 1.0f, scale, t);
        for (auto end = t + 250ms; t < end; t += step) e.update(15.0f, 1.0f, scale, t);
        return e.filteredGpuMs();
    };
    float slow = run(std::chrono::microseconds(33333));
    float fast = run(std::chrono::microseconds(6944));
    CHECK_NEAR(slow, fast, 0.6f);
    CHECK(slow > 10.0f && slow < 15.0f); // moved most of the way, not instantly
}

static void testCpuBottleneckDetectedAndHeld() {
    auto e = makeEngine();
    auto t = Clock::now();
    feed(e, 5.0f, 10.5f, 1.0f, t, 1s); // CPU 10.5 ms > 85% of 11.11 and > GPU
    CHECK(e.isCpuBottlenecked());
    feed(e, 5.0f, 3.0f, 1.0f, t, 1500ms); // CPU recovers; filter settles within ~1 s
    CHECK(e.isCpuBottlenecked()); // still inside the 3 s hold after the last spike
    feed(e, 5.0f, 3.0f, 1.0f, t, 3s);
    CHECK(!e.isCpuBottlenecked());
}

static void testCpuBalancingDisabled() {
    auto e = makeEngine(false);
    auto t = Clock::now();
    feed(e, 5.0f, 10.5f, 1.0f, t, 1s);
    CHECK(!e.isCpuBottlenecked());
}

static void testResetClearsState() {
    auto e = makeEngine();
    auto t = Clock::now();
    float scale = e.update(12.0f, 4.0f, 1.0f, t); // starts a cooldown
    e.reset();
    CHECK(e.filteredGpuMs() == 0.0f);
    CHECK(!e.isCpuBottlenecked());
    // No cooldown after reset: an immediate change is allowed
    float next = e.update(4.0f, 2.0f, scale, t + 10ms);
    CHECK(next > scale);
}

int main() {
    const std::vector<std::pair<std::string, std::function<void()>>> tests = {
        {"deadband holds scale", testDeadbandHoldsScale},
        {"over budget drops to computed scale", testOverBudgetDropsToComputedScale},
        {"under budget raises, clamped to max", testUnderBudgetRaisesClampedToMax},
        {"clamps to min", testClampsToMin},
        {"cooldown blocks moderate changes", testCooldownBlocksModerateChanges},
        {"cooldown blocks raises even when far under", testCooldownBlocksRaisesEvenWhenFarUnder},
        {"severe overrun drops after settle", testSevereOverrunDropsAfterSettle},
        {"time-based smoothing independent of call rate", testTimeBasedSmoothingIndependentOfCallRate},
        {"CPU bottleneck detected and held", testCpuBottleneckDetectedAndHeld},
        {"CPU balancing disabled", testCpuBalancingDisabled},
        {"reset clears state", testResetClearsState},
    };
    for (const auto& [name, fn] : tests) {
        int before = g_failures;
        fn();
        std::printf("%s %s\n", g_failures == before ? "ok  " : "FAIL", name.c_str());
    }
    std::printf("\n%s (%d failure%s)\n", g_failures ? "FAILED" : "PASSED", g_failures, g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}
