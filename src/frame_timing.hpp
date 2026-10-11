#pragma once
#include <cstdint>
#include <deque>

#include <openvr.h>

// Turns SteamVR's per-frame Compositor_FrameTiming history into per-poll averages for the smoothing
// engine, plus rolling missed-frame / reprojection counts for the UI.
//
// Every completed game frame is counted exactly once, however often poll() runs. The newest frame in
// the history is skipped because it may still be in flight (its GPU timings not yet filled in).
class FrameTimingTracker {
public:
    static constexpr uint32_t kHistoryFrames = 32;      // frames requested per poll
    static constexpr double kWindowSeconds = 5.0;       // rolling window for the missed-frame stats

    struct PollResult {
        int newFrames = 0;
        bool hasGpu = false;
        bool hasCpu = false;
        float avgGpuMs = 0.0f; // game GPU time (pre + post submit)
        float avgCpuMs = 0.0f; // WaitGetPoses returning -> second eye Submit
    };

    struct WindowStats {
        int frames = 0;
        int droppedFrames = 0;   // extra scan-outs of a previous frame (the game missed a refresh)
        int lateFrames = 0;      // frames SteamVR reprojected because the game was late (CPU and/or GPU)
        int cpuReprojected = 0;  // frames SteamVR reprojected because the CPU was late
        int gpuReprojected = 0;  // frames SteamVR reprojected because the GPU was late
        int motionSmoothed = 0;  // frames where motion smoothing / ASW was applied
    };

    // Fetches recent frames from the compositor and ingests them.
    PollResult poll(vr::IVRCompositor* compositor) {
        if (!compositor) return {};
        vr::Compositor_FrameTiming frames[kHistoryFrames] = {};
        frames[0].m_nSize = sizeof(vr::Compositor_FrameTiming);
        uint32_t count = compositor->GetFrameTimings(frames, kHistoryFrames);
        return ingest(frames, count);
    }

    // Ingests frames ordered oldest -> newest (as returned by GetFrameTimings). Public for tests.
    PollResult ingest(const vr::Compositor_FrameTiming* frames, uint32_t count) {
        PollResult result;
        if (count < 2) return result;

        uint32_t newestIndex = frames[count - 1].m_nFrameIndex;
        if (newestIndex < m_lastFrameIndex) {
            m_lastFrameIndex = 0; // compositor restarted; frame numbering started over
        }

        float gpuSum = 0.0f, cpuSum = 0.0f;
        int gpuCount = 0, cpuCount = 0;
        for (uint32_t i = 0; i + 1 < count; ++i) { // skip the newest (possibly in-flight) frame
            const auto& f = frames[i];
            if (f.m_nFrameIndex == 0 || f.m_nFrameIndex <= m_lastFrameIndex) continue;
            m_lastFrameIndex = f.m_nFrameIndex;
            ++result.newFrames;

            // Prefer game pre-submit GPU time if available, fallback to total render GPU time
            float gpu = (f.m_flPreSubmitGpuMs > 0.0f) ? (f.m_flPreSubmitGpuMs + f.m_flPostSubmitGpuMs) : f.m_flTotalRenderGpuMs;
            if (gpu > 1.0f && gpu < 100.0f) { gpuSum += gpu; ++gpuCount; }

            // Game CPU frametime: time between WaitGetPoses return and second eye Submit
            float cpu = f.m_flNewFrameReadyMs - f.m_flNewPosesReadyMs;
            if (cpu > 0.1f && cpu < 100.0f) { cpuSum += cpu; ++cpuCount; }

            m_window.push_back({f.m_flSystemTimeInSeconds, f.m_nNumDroppedFrames, f.m_nReprojectionFlags});
        }

        if (gpuCount > 0) { result.hasGpu = true; result.avgGpuMs = gpuSum / gpuCount; }
        if (cpuCount > 0) { result.hasCpu = true; result.avgCpuMs = cpuSum / cpuCount; }

        // Trim the rolling window relative to the newest frame time
        double newestTime = frames[count - 1].m_flSystemTimeInSeconds;
        while (!m_window.empty() && (newestTime - m_window.front().time > kWindowSeconds || m_window.size() > 4096)) {
            m_window.pop_front();
        }
        return result;
    }

    WindowStats windowStats() const {
        WindowStats s;
        for (const auto& f : m_window) {
            ++s.frames;
            s.droppedFrames += static_cast<int>(f.dropped);
            if (f.flags & (vr::VRCompositor_ReprojectionReason_Cpu | vr::VRCompositor_ReprojectionReason_Gpu)) ++s.lateFrames;
            if (f.flags & vr::VRCompositor_ReprojectionReason_Cpu) ++s.cpuReprojected;
            if (f.flags & vr::VRCompositor_ReprojectionReason_Gpu) ++s.gpuReprojected;
            if (f.flags & vr::VRCompositor_ReprojectionMotion) ++s.motionSmoothed;
        }
        return s;
    }

    // Clears the missed-frame stats (e.g. on game switch) without re-reading frames already counted
    void clearWindow() { m_window.clear(); }

    // Full reset, e.g. on SteamVR disconnect
    void reset() {
        m_lastFrameIndex = 0;
        m_window.clear();
    }

private:
    struct FrameRecord {
        double time;
        uint32_t dropped;
        uint32_t flags;
    };

    uint32_t m_lastFrameIndex = 0;
    std::deque<FrameRecord> m_window;
};
