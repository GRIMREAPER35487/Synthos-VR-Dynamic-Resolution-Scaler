#pragma once
#include <cstdint>

#pragma pack(push, 1)
struct SharedVRState {
    // Magic header / Version check
    uint32_t magic = 0x56525245; // "VRRE"

    // Inputs from UI to Layer
    float targetScale = 1.0f;       // 0.5f to 1.5f (current target scale)
    float minScale = 0.65f;         // Minimum allowed scale
    float maxScale = 1.20f;         // Maximum allowed scale
    bool isEnabled = true;          // Master toggle for dynamic resolution
    bool autoDetectHz = true;       // Auto-detect HMD refresh rate from SteamVR
    bool debugForceBlack = false;    // Debug visual test: force 10% min resolution

    // Outputs from Layer/Engine to UI
    float detectedHmdHz = 90.0f;      // Auto-detected HMD Refresh Rate from SteamVR
    float currentGpuFrametime = 0.0f; // In milliseconds
    float currentCpuFrametime = 0.0f; // In milliseconds
    float activeAppliedScale = 1.0f;  // Actual applied scale in current frame
    uint32_t activeWidth = 0;         // Effective render width
    uint32_t activeHeight = 0;        // Effective render height
    uint32_t nativeWidth = 0;         // Bucket 100% native width
    uint32_t nativeHeight = 0;        // Bucket 100% native height
    char activeAppKey[128] = {0};     // SteamVR application key (e.g. "steam.app.438100")
    uint64_t frameCounter = 0;        // Incrementing frame counter
};
#pragma pack(pop)
