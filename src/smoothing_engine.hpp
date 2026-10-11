#pragma once
#include <algorithm>
#include <cmath>
#include <chrono>

class SmoothingEngine {
public:
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

    // Pure GPU + Automatic CPU Bottleneck Detection & Smart Budget Ratio Update Loop
    float update(float rawGpuMs, float rawCpuMs, float currentScale) {
        if (rawGpuMs <= 0.5f) return currentScale;

        // 1. Exponential Moving Average (EMA) filter: GPU (alpha = 0.10) & Heavy CPU smoothing (alpha = 0.05)
        constexpr float gpuAlpha = 0.10f;
        constexpr float cpuAlpha = 0.05f;

        if (m_filteredGpuMs <= 0.0f) {
            m_filteredGpuMs = rawGpuMs;
            m_filteredCpuMs = (rawCpuMs > 0.0f) ? rawCpuMs : 0.0f;
        } else {
            m_filteredGpuMs = gpuAlpha * rawGpuMs + (1.0f - gpuAlpha) * m_filteredGpuMs;
            if (rawCpuMs > 0.0f) {
                m_filteredCpuMs = cpuAlpha * rawCpuMs + (1.0f - cpuAlpha) * m_filteredCpuMs;
            }
        }

        auto now = std::chrono::steady_clock::now();

        // 2. CPU BOTTLENECK DETECTION & 3-SECOND HOLD DELAY:
        // Detect CPU spike when filtered CPU exceeds 85% of target budget and exceeds GPU time
        bool rawCpuSpike = (m_filteredCpuMs > m_targetFrametimeMs * 0.85f) && (m_filteredCpuMs > m_filteredGpuMs);
        if (rawCpuSpike) {
            m_lastCpuSpikeTime = now;
        }

        // Hold CPU Bottleneck Shield active for user-configured cooldownSeconds (default 3.0s) after spike ends
        bool isCpuBottlenecked = false;
        if (m_enableCpuBalancing && m_lastCpuSpikeTime.time_since_epoch().count() > 0) {
            auto cpuSpikeElapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastCpuSpikeTime).count();
            if (cpuSpikeElapsedMs < static_cast<long long>(m_cooldownSeconds * 1000.0f)) {
                isCpuBottlenecked = true;
            }
        }

        // 3. Cooldown timer: Hold static between resolution changes to prevent texture thrashing
        if (m_lastChangeTime.time_since_epoch().count() > 0) {
            auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastChangeTime).count();
            if (elapsedMs < static_cast<long long>(m_cooldownSeconds * 1000.0f)) {
                return currentScale;
            }
        }

        // 4. Target GPU Frametime Budget (85% of HMD refresh rate budget, e.g. 9.44ms for 90Hz)
        float targetGpuBudgetMs = m_targetFrametimeMs * 0.85f;

        if (isCpuBottlenecked) {
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

        // Sweet-spot deadband (0.88x to 1.08x of target budget)
        float ratio = m_filteredGpuMs / targetGpuBudgetMs;
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
        if (isCpuBottlenecked && m_cpuEmergencyFloor < effectiveMinScale) {
            effectiveMinScale = m_cpuEmergencyFloor;
        }

        // Clamp strictly between effective min and max scale limits
        float newTargetScale = (std::clamp)(roundedScale, effectiveMinScale, m_maxScale);

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
        m_lastChangeTime = {};
        m_lastCpuSpikeTime = {};
    }

private:
    float m_targetHz = 90.0f;
    float m_targetFrametimeMs = 11.11f;
    float m_minScale = 0.65f;
    float m_maxScale = 1.20f;
    float m_cooldownSeconds = 3.0f;
    bool m_enableCpuBalancing = true;
    float m_cpuEmergencyFloor = 0.50f;
    float m_filteredGpuMs = 0.0f;
    float m_filteredCpuMs = 0.0f;
    std::chrono::steady_clock::time_point m_lastChangeTime{};
    std::chrono::steady_clock::time_point m_lastCpuSpikeTime{};
};
