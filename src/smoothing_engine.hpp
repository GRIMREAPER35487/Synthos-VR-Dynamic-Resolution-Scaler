#pragma once
#include <algorithm>
#include <cmath>
#include <chrono>

class SmoothingEngine {
public:
    using Clock = std::chrono::steady_clock;

    SmoothingEngine() = default;

    void configure(float targetHz, float minScale = 0.65f, float maxScale = 1.20f, float cooldownSeconds = 3.0f, bool enableCpuBalancing = true, float cpuEmergencyFloor = 0.50f) {
        if (targetHz > 10.0f) {
            m_targetHz = targetHz;
            m_targetFrametimeMs = 1000.0f / targetHz;
        }
        m_minScale = std::clamp(minScale, 0.30f, 1.00f);
        m_maxScale = std::clamp(maxScale, 1.00f, 2.50f);
        m_cooldownSeconds = std::clamp(cooldownSeconds, 0.5f, 10.0f);
        m_enableCpuBalancing = enableCpuBalancing;
        m_cpuEmergencyFloor = std::clamp(cpuEmergencyFloor, 0.30f, 1.00f);
    }

    // Pure GPU + Automatic CPU Bottleneck Detection & Smart Budget Ratio Update Loop.
    // `now` is injectable for tests; callers normally omit it.
    float update(float rawGpuMs, float rawCpuMs, float currentScale, Clock::time_point now = Clock::now()) {
        if (rawGpuMs <= 0.5f) return currentScale;

        // 1. Time-based Exponential Moving Average (EMA): GPU (tau = 160 ms) & heavier CPU smoothing (tau = 330 ms).
        // Weighting by elapsed time keeps the response identical however often update() is called
        // (the old fixed per-sample alpha of 0.10 / 0.05 matched these time constants at a 60 Hz call rate).
        constexpr float gpuTauSeconds = 0.16f;
        constexpr float cpuTauSeconds = 0.33f;

        if (m_filteredGpuMs <= 0.0f) {
            m_filteredGpuMs = rawGpuMs;
            m_filteredCpuMs = (rawCpuMs > 0.0f) ? rawCpuMs : 0.0f;
        } else {
            float dt = std::clamp(std::chrono::duration<float>(now - m_lastUpdateTime).count(), 0.0f, 1.0f);
            float gpuAlpha = 1.0f - std::exp(-dt / gpuTauSeconds);
            float cpuAlpha = 1.0f - std::exp(-dt / cpuTauSeconds);
            m_filteredGpuMs = gpuAlpha * rawGpuMs + (1.0f - gpuAlpha) * m_filteredGpuMs;
            if (rawCpuMs > 0.0f) {
                m_filteredCpuMs = cpuAlpha * rawCpuMs + (1.0f - cpuAlpha) * m_filteredCpuMs;
            }
        }
        m_lastUpdateTime = now;

        // 2. CPU BOTTLENECK DETECTION & HOLD DELAY:
        // Detect CPU spike when filtered CPU exceeds 85% of target budget and exceeds GPU time
        bool rawCpuSpike = (m_filteredCpuMs > m_targetFrametimeMs * 0.85f) && (m_filteredCpuMs > m_filteredGpuMs);
        if (rawCpuSpike) {
            m_lastCpuSpikeTime = now;
        }

        // Hold CPU Bottleneck Shield active for user-configured cooldownSeconds (default 3.0s) after spike ends
        m_cpuBottlenecked = false;
        if (m_enableCpuBalancing && m_lastCpuSpikeTime.time_since_epoch().count() > 0) {
            if (now - m_lastCpuSpikeTime < secondsToDuration(m_cooldownSeconds)) {
                m_cpuBottlenecked = true;
            }
        }

        // 3. Target GPU Frametime Budget (85% of HMD refresh rate budget, e.g. 9.44ms for 90Hz)
        float targetGpuBudgetMs = m_targetFrametimeMs * 0.85f;

        if (m_cpuBottlenecked) {
            float availableGpuBudget = m_targetFrametimeMs - m_filteredCpuMs;

            if (availableGpuBudget >= 3.0f) {
                // We CAN scale smoothly to fit inside remaining GPU budget!
                targetGpuBudgetMs = availableGpuBudget;
            } else {
                // CPU load is so heavy that remaining GPU budget is < 3.0ms. Drop straight to Hard Floor!
                targetGpuBudgetMs = (std::max)(2.0f, targetGpuBudgetMs * 0.5f);
            }
        }

        if (targetGpuBudgetMs <= 1.0f || m_filteredGpuMs <= 0.5f) {
            return currentScale;
        }

        float ratio = m_filteredGpuMs / targetGpuBudgetMs;

        // 4. Cooldown timer: Hold static between resolution changes so the new resolution's frametime is
        // measured before deciding again (and to prevent texture thrashing). Exception: a severe overrun
        // may drop resolution early, once a short settle time has passed since the last change.
        bool severeOverrunDropOnly = false;
        if (m_lastChangeTime.time_since_epoch().count() > 0) {
            auto sinceChange = now - m_lastChangeTime;
            if (sinceChange < secondsToDuration(m_cooldownSeconds)) {
                if (ratio > kSevereOverrunRatio && sinceChange >= secondsToDuration(kSevereOverrunSettleSeconds)) {
                    severeOverrunDropOnly = true;
                } else {
                    return currentScale;
                }
            }
        }

        // Sweet-spot deadband (0.88x to 1.08x of target budget)
        if (ratio >= 0.88f && ratio <= 1.08f) {
            return currentScale; // Perfect sweet-spot: Keep resolution locked
        }

        // 5. PURE MATH-BASED TARGET SCALE CALCULATION
        // Exact target scale required to bring GPU frametime back to target budget
        float exactMathScale = currentScale * (targetGpuBudgetMs / m_filteredGpuMs);

        // Round math scale to clean 2% increments (e.g. 88%, 90%, 92%)
        float roundedScale = std::round(exactMathScale * 50.0f) / 50.0f;

        // Effective Minimum Scale Floor: Lower of user minScale or cpuEmergencyFloor when balancing is active
        float effectiveMinScale = m_minScale;
        if (m_cpuBottlenecked && m_cpuEmergencyFloor < effectiveMinScale) {
            effectiveMinScale = m_cpuEmergencyFloor;
        }

        // Clamp strictly between effective min and max scale limits
        float newTargetScale = (std::clamp)(roundedScale, effectiveMinScale, m_maxScale);

        if (severeOverrunDropOnly && newTargetScale >= currentScale) {
            return currentScale;
        }

        // 6. Apply ONCE if target differs from current scale by at least 2% (0.02)
        if (std::abs(newTargetScale - currentScale) >= 0.020f) {
            m_lastChangeTime = now;
            return newTargetScale;
        }

        return currentScale;
    }

    void reset() {
        m_filteredGpuMs = 0.0f;
        m_filteredCpuMs = 0.0f;
        m_cpuBottlenecked = false;
        m_lastChangeTime = {};
        m_lastCpuSpikeTime = {};
        m_lastUpdateTime = {};
    }

    // State as of the last update(), for display
    bool isCpuBottlenecked() const { return m_cpuBottlenecked; }
    float filteredGpuMs() const { return m_filteredGpuMs; }
    float filteredCpuMs() const { return m_filteredCpuMs; }

    // GPU this far over budget may drop resolution before the cooldown ends...
    static constexpr float kSevereOverrunRatio = 1.25f;
    // ...but only once this long has passed since the last change, so the current resolution is measured first
    static constexpr float kSevereOverrunSettleSeconds = 1.0f;

private:
    static Clock::duration secondsToDuration(float seconds) {
        return std::chrono::duration_cast<Clock::duration>(std::chrono::duration<float>(seconds));
    }

    float m_targetHz = 90.0f;
    float m_targetFrametimeMs = 11.11f;
    float m_minScale = 0.65f;
    float m_maxScale = 1.20f;
    float m_cooldownSeconds = 3.0f;
    bool m_enableCpuBalancing = true;
    float m_cpuEmergencyFloor = 0.50f;
    float m_filteredGpuMs = 0.0f;
    float m_filteredCpuMs = 0.0f;
    bool m_cpuBottlenecked = false;
    Clock::time_point m_lastChangeTime{};
    Clock::time_point m_lastCpuSpikeTime{};
    Clock::time_point m_lastUpdateTime{};
};
