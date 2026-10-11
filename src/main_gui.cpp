#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <tlhelp32.h>
#include <shlobj.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <chrono>
#include <thread>
#include <algorithm>
#include <cmath>
#include <filesystem>

#include <openvr.h>

#include <GLFW/glfw3.h>
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include "ipc_shared_mem.hpp"
#include "smoothing_engine.hpp"

static bool g_enableLogging = false;
static std::filesystem::path g_dataDir; // %APPDATA%\Syn Dynamic Res Scale (writable even when installed to Program Files)

// Resolves and creates the per-user data directory. Falls back to the working directory if APPDATA is unavailable.
std::filesystem::path resolveDataDir() {
    std::filesystem::path dir;
    PWSTR roaming = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, NULL, &roaming))) {
        dir = std::filesystem::path(roaming) / L"Syn Dynamic Res Scale";
    }
    CoTaskMemFree(roaming);
    if (dir.empty()) return {};

    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return ec ? std::filesystem::path{} : dir;
}

std::string pathToUtf8(const std::filesystem::path& p) {
    const std::wstring& w = p.native();
    if (w.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), NULL, 0, NULL, NULL);
    std::string out(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), out.data(), len, NULL, NULL);
    return out;
}

void logDebug(const std::string& msg) {
    if (!g_enableLogging) return;
    try {
        std::ofstream log(g_dataDir / "debug.log", std::ios::app);
        if (log.is_open()) {
            auto now = std::chrono::system_clock::now();
            log << "[" << std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() << "] " << msg << std::flush;
            log << std::endl;
        }
    } catch (...) {}
}

// 100% Fail-Proof Check for Active SteamVR IPC Pipe (Prevents EVER Autostarting SteamVR)
bool isSteamVRActiveAndRunning() {
    // 1. Check if active OpenVR IPC Pipe exists
    HANDLE hPipe = CreateFileA("\\\\.\\pipe\\hr_pipe", GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (hPipe != INVALID_HANDLE_VALUE) {
        CloseHandle(hPipe);
        return true;
    }
    
    // 2. Check if vrserver.exe process is running
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(PROCESSENTRY32W);
    bool running = false;
    if (Process32FirstW(hSnap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, L"vrserver.exe") == 0) {
                running = true;
                break;
            }
        } while (Process32NextW(hSnap, &pe));
    }
    CloseHandle(hSnap);
    return running;
}

struct AppConfig {
    bool isEnabled = true;
    bool autoDetectHz = true;
    bool enableLogging = false;
    bool useHalfRateSSW = false;         // SSW / Frame Gen Half-Rate Target
    bool enableCpuBalancing = true;     // CPU Auto-Shield ON by default
    int selectedHzIndex = 6;             // 90Hz Native default (index 6)
    float minScale = 0.65f;
    float maxScale = 1.20f;
    float cooldownSeconds = 3.0f;         // Configurable Resolution Cooldown Delay
    float cpuEmergencyFloor = 0.50f;     // CPU Emergency Hard Floor Default 50%
};

bool saveConfigToFile(const std::filesystem::path& filepath, const AppConfig& cfg) {
    std::ofstream file(filepath);
    if (!file.is_open()) return false;
    file << "[General]\n";
    file << "isEnabled=" << (cfg.isEnabled ? 1 : 0) << "\n";
    file << "autoDetectHz=" << (cfg.autoDetectHz ? 1 : 0) << "\n";
    file << "enableLogging=" << (cfg.enableLogging ? 1 : 0) << "\n";
    file << "useHalfRateSSW=" << (cfg.useHalfRateSSW ? 1 : 0) << "\n";
    file << "enableCpuBalancing=" << (cfg.enableCpuBalancing ? 1 : 0) << "\n";
    file << "selectedHzIndex=" << cfg.selectedHzIndex << "\n\n";
    file << "[Resolution]\n";
    file << "minScale=" << cfg.minScale << "\n";
    file << "maxScale=" << cfg.maxScale << "\n";
    file << "cooldownSeconds=" << cfg.cooldownSeconds << "\n";
    file << "cpuEmergencyFloor=" << cfg.cpuEmergencyFloor << "\n";
    file.flush();
    return file.good();
}

void loadConfigFromFile(const std::filesystem::path& filepath, AppConfig& cfg) {
    std::ifstream file(filepath);
    if (!file.is_open()) return;
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '[' || line[0] == ';') continue;
        size_t pos = line.find('=');
        if (pos == std::string::npos) continue;
        std::string key = line.substr(0, pos);
        std::string val = line.substr(pos + 1);
        try {
            if (key == "isEnabled") cfg.isEnabled = (std::stoi(val) != 0);
            else if (key == "autoDetectHz") cfg.autoDetectHz = (std::stoi(val) != 0);
            else if (key == "enableLogging") cfg.enableLogging = (std::stoi(val) != 0);
            else if (key == "useHalfRateSSW") cfg.useHalfRateSSW = (std::stoi(val) != 0);
            else if (key == "enableCpuBalancing") cfg.enableCpuBalancing = (std::stoi(val) != 0);
            else if (key == "selectedHzIndex") cfg.selectedHzIndex = std::clamp(std::stoi(val), 0, 8);
            else if (key == "minScale") cfg.minScale = std::clamp(std::stof(val), 0.30f, 1.00f);
            else if (key == "maxScale") cfg.maxScale = std::clamp(std::stof(val), 1.00f, 2.50f);
            else if (key == "cooldownSeconds") cfg.cooldownSeconds = std::clamp(std::stof(val), 0.5f, 10.0f);
            else if (key == "cpuEmergencyFloor") cfg.cpuEmergencyFloor = std::clamp(std::stof(val), 0.30f, 1.00f);
        } catch (...) {}
    }
    if (cfg.minScale > cfg.maxScale) {
        std::swap(cfg.minScale, cfg.maxScale);
    }
}

// Dynamically registers manifest.vrmanifest into SteamVR cleanly via OpenVR API or safe JSON
bool registerManifestWithSteamVR() {
    char exePath[MAX_PATH] = {0};
    GetModuleFileNameA(NULL, exePath, MAX_PATH);
    std::string exeDir(exePath);
    size_t lastSlash = exeDir.find_last_of("\\/");
    if (lastSlash != std::string::npos) {
        exeDir = exeDir.substr(0, lastSlash);
    }
    std::string manifestPath = exeDir + "\\manifest.vrmanifest";

    logDebug("[VERBOSE] Registering Manifest with SteamVR: " + manifestPath);

    // 1. If OpenVR applications interface is already available, use official API
    if (vr::VRApplications() != nullptr) {
        vr::EVRApplicationError err = vr::VRApplications()->AddApplicationManifest(manifestPath.c_str(), false);
        if (err == vr::VRApplicationError_None) {
            vr::VRApplications()->SetApplicationAutoLaunch("steam.overlay.syn_dynamic_res_scale", true);
            logDebug("[VERBOSE] Successfully registered manifest using VRApplications API");
            return true;
        }
    }

    // 2. Query Steam installation path from Registry
    char steamPath[MAX_PATH] = {0};
    DWORD pathSize = sizeof(steamPath);
    HKEY hKey = NULL;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\Valve\\Steam", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        RegQueryValueExA(hKey, "SteamPath", NULL, NULL, (LPBYTE)steamPath, &pathSize);
        RegCloseKey(hKey);
    }
    if (steamPath[0] == '\0') {
        if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\WOW6432Node\\Valve\\Steam", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
            pathSize = sizeof(steamPath);
            RegQueryValueExA(hKey, "InstallPath", NULL, NULL, (LPBYTE)steamPath, &pathSize);
            RegCloseKey(hKey);
        }
    }
    for (int i = 0; steamPath[i] != '\0'; ++i) {
        if (steamPath[i] == '/') steamPath[i] = '\\';
    }

    std::string appConfigPath = (steamPath[0] != '\0') 
        ? (std::string(steamPath) + "\\config\\appconfig.json") 
        : "C:\\Program Files (x86)\\Steam\\config\\appconfig.json";

    std::string escapedManifestPath = "";
    for (char c : manifestPath) {
        if (c == '\\') escapedManifestPath += "\\\\";
        else escapedManifestPath += c;
    }

    std::ifstream inFile(appConfigPath);
    if (inFile.is_open()) {
        std::stringstream buffer;
        buffer << inFile.rdbuf();
        std::string content = buffer.str();
        inFile.close();

        if (content.find(escapedManifestPath) == std::string::npos) {
            size_t pos = content.find("manifest_paths");
            if (pos != std::string::npos) {
                size_t bracketOpen = content.find("[", pos);
                size_t bracketClose = content.find("]", bracketOpen != std::string::npos ? bracketOpen : pos);
                if (bracketClose != std::string::npos) {
                    std::string arrayContent = content.substr(bracketOpen + 1, bracketClose - bracketOpen - 1);
                    bool hasElements = (arrayContent.find_first_not_of(" \t\r\n") != std::string::npos);
                    std::string insertStr = hasElements 
                        ? (",\n     \"" + escapedManifestPath + "\"\n  ") 
                        : ("\n     \"" + escapedManifestPath + "\"\n  ");
                    content.insert(bracketClose, insertStr);

                    std::ofstream outFile(appConfigPath);
                    if (outFile.is_open()) {
                        outFile << content;
                        outFile.close();
                        logDebug("[VERBOSE] Successfully wrote manifest path to appconfig.json");
                        return true;
                    }
                }
            }
        } else {
            logDebug("[VERBOSE] Manifest already registered in appconfig.json");
            return true;
        }
    } else {
        logDebug("[VERBOSE ERROR] Failed to open Steam appconfig.json at: " + appConfigPath);
    }
    return false;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    // 1. Ensure working directory is always the executable's directory
    char exePath[MAX_PATH] = {0};
    GetModuleFileNameA(NULL, exePath, MAX_PATH);
    std::string exeDir(exePath);
    size_t lastSlash = exeDir.find_last_of("\\/");
    if (lastSlash != std::string::npos) {
        SetCurrentDirectoryA(exeDir.substr(0, lastSlash).c_str());
    }

    bool launchedWithSteamVR = false;
    if (lpCmdLine != nullptr) {
        std::string cmd(lpCmdLine);
        if (cmd.find("-autostarted") != std::string::npos || cmd.find("-vrmonitor") != std::string::npos) {
            launchedWithSteamVR = true;
        }
    }
    if (isSteamVRActiveAndRunning()) {
        launchedWithSteamVR = true;
    }

    g_dataDir = resolveDataDir();
    const std::filesystem::path configPath = g_dataDir / "settings.ini";
    const std::string configPathStr = pathToUtf8(configPath);
    AppConfig config;
    loadConfigFromFile(configPath, config);
    g_enableLogging = config.enableLogging;

    logDebug("==================================================");
    logDebug("[VERBOSE STARTUP] Syn Dynamic Res Scale Version 1.0.0 (LaunchedWithVR: " + std::string(launchedWithSteamVR ? "Yes" : "No") + ")");

    if (!glfwInit()) {
        logDebug("[VERBOSE ERROR] Failed to initialize GLFW windowing system");
        return 1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    GLFWwindow* window = glfwCreateWindow(720, 690, "Syn Dynamic Res Scale", NULL, NULL);
    if (!window) {
        logDebug("[VERBOSE ERROR] Failed to create GLFW window");
        glfwTerminate();
        return 1;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr; // Single fixed full-window layout; nothing worth persisting (and the exe dir may be read-only)

    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 8.0f;
    style.FrameRounding = 5.0f;
    style.PopupRounding = 6.0f;
    style.ScrollbarRounding = 6.0f;
    style.GrabRounding = 5.0f;
    style.TabRounding = 6.0f;

    style.Colors[ImGuiCol_WindowBg]           = ImVec4(0.09f, 0.10f, 0.12f, 1.00f);
    style.Colors[ImGuiCol_Header]             = ImVec4(0.20f, 0.25f, 0.35f, 1.00f);
    style.Colors[ImGuiCol_HeaderHovered]      = ImVec4(0.28f, 0.35f, 0.48f, 1.00f);
    style.Colors[ImGuiCol_HeaderActive]       = ImVec4(0.15f, 0.20f, 0.30f, 1.00f);
    style.Colors[ImGuiCol_Button]             = ImVec4(0.18f, 0.38f, 0.68f, 1.00f);
    style.Colors[ImGuiCol_ButtonHovered]      = ImVec4(0.25f, 0.48f, 0.82f, 1.00f);
    style.Colors[ImGuiCol_ButtonActive]       = ImVec4(0.12f, 0.30f, 0.55f, 1.00f);
    style.Colors[ImGuiCol_FrameBg]            = ImVec4(0.15f, 0.16f, 0.19f, 1.00f);
    style.Colors[ImGuiCol_FrameBgHovered]     = ImVec4(0.22f, 0.24f, 0.28f, 1.00f);
    style.Colors[ImGuiCol_SliderGrab]         = ImVec4(0.25f, 0.55f, 0.95f, 1.00f);
    style.Colors[ImGuiCol_SliderGrabActive]   = ImVec4(0.35f, 0.65f, 1.00f, 1.00f);

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 130");

    HANDLE hMapFile = CreateFileMappingA(
        INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, sizeof(SharedVRState), "Local\\VRResScalerSharedMem"
    );
    SharedVRState* pShared = nullptr;
    if (hMapFile) {
        pShared = (SharedVRState*)MapViewOfFile(hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedVRState));
        if (pShared) {
            pShared->magic = 0x56525245;
            pShared->targetScale = 1.0f;
            pShared->minScale = config.minScale;
            pShared->maxScale = config.maxScale;
            pShared->isEnabled = config.isEnabled;
            pShared->autoDetectHz = config.autoDetectHz;
            pShared->debugForceBlack = false;
            pShared->detectedHmdHz = 90.0f;
            pShared->nativeWidth = 2880;
            pShared->nativeHeight = 2880;
            pShared->activeWidth = 2880;
            pShared->activeHeight = 2880;
            pShared->frameCounter = 0;
            pShared->activeAppKey[0] = '\0';
        }
    }

    SmoothingEngine smoothing;
    smoothing.configure(90.0f, config.minScale, config.maxScale, config.cooldownSeconds, config.enableCpuBalancing, config.cpuEmergencyFloor);

    // Persistent State Across Frame Iterations
    float activeHz = 90.0f;
    float currentScale = 1.0f;
    float lastAppliedVrScale = 1.0f;
    float lastValidGpuMs = 8.0f;
    float lastValidCpuMs = 6.0f;
    uint32_t cachedNativeWidth = 2880;
    uint32_t cachedNativeHeight = 2880;
    std::string lastActiveAppKey = "";

    const char* hzOptions[] = { 
        "36 Hz (72 Hz SSW / 27.78 ms)",
        "40 Hz (80 Hz SSW / 25.00 ms)",
        "45 Hz (90 Hz SSW / 22.22 ms)",
        "60 Hz (120 Hz SSW / 16.67 ms)",
        "72 Hz (Native / 13.89 ms)",
        "80 Hz (Native / 12.50 ms)",
        "90 Hz (Native / 11.11 ms)",
        "120 Hz (Native / 8.33 ms)",
        "144 Hz (Native / 6.94 ms)"
    };
    const float hzValues[] = { 36.0f, 40.0f, 45.0f, 60.0f, 72.0f, 80.0f, 90.0f, 120.0f, 144.0f };

    std::vector<float> gpuFrametimeHistory(100, 0.0f);
    std::vector<float> cpuFrametimeHistory(100, 0.0f);

    std::string statusMessage = "Settings file: " + configPathStr;
    std::string activeAppKeyStr = "Waiting for VR Game (e.g. VRChat)...";

    vr::IVRSystem* pVRSystem = nullptr;
    vr::VROverlayHandle_t overlayHandle = vr::k_ulOverlayHandleInvalid;
    bool steamVRActive = false;
    bool configChanged = false;
    auto lastVRCheckTime = std::chrono::steady_clock::now() - std::chrono::seconds(10);

    // Main GUI Loop
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        auto now = std::chrono::steady_clock::now();
        
        // PURE SETTINGS EDITOR MODE: Check for active SteamVR every 2 seconds
        if (!pVRSystem && std::chrono::duration_cast<std::chrono::seconds>(now - lastVRCheckTime).count() >= 2) {
            lastVRCheckTime = now;
            try {
                bool isRunning = isSteamVRActiveAndRunning();
                if (isRunning) {
                    vr::EVRInitError err = vr::VRInitError_None;
                    pVRSystem = vr::VR_Init(&err, vr::VRApplication_Overlay);
                    if (err == vr::VRInitError_None && pVRSystem) {
                        steamVRActive = true;
                        if (vr::VROverlay()) {
                            vr::VROverlay()->CreateOverlay("steam.overlay.syn_dynamic_res_scale", "Syn Dynamic Res Scale", &overlayHandle);
                        }

                        // Query native recommended render size upon connection
                        uint32_t recW = 0, recH = 0;
                        pVRSystem->GetRecommendedRenderTargetSize(&recW, &recH);
                        if (recW > 0 && recH > 0) {
                            cachedNativeWidth = recW;
                            cachedNativeHeight = recH;
                            if (pShared) {
                                pShared->nativeWidth = recW;
                                pShared->nativeHeight = recH;
                            }
                        }
                    } else {
                        pVRSystem = nullptr;
                        steamVRActive = false;
                    }
                } else {
                    pVRSystem = nullptr;
                    steamVRActive = false;
                }
            } catch (...) {
                pVRSystem = nullptr;
                steamVRActive = false;
            }
        }

        // POLL STEAMVR EVENTS FOR AUTO-CLOSE ON QUIT (Only shutdown if SteamVR or our process quits)
        if (pVRSystem) {
            try {
                vr::VREvent_t vrEvent;
                while (pVRSystem->PollNextEvent(&vrEvent, sizeof(vrEvent))) {
                    if (vrEvent.eventType == vr::VREvent_Quit || 
                        vrEvent.eventType == vr::VREvent_DriverRequestedQuit ||
                        (vrEvent.eventType == vr::VREvent_ProcessQuit && vrEvent.data.process.pid == GetCurrentProcessId())) {
                        if (pVRSystem) pVRSystem->AcknowledgeQuit_Exiting();
                        if (vr::VROverlay() && overlayHandle != vr::k_ulOverlayHandleInvalid) {
                            vr::VROverlay()->DestroyOverlay(overlayHandle);
                            overlayHandle = vr::k_ulOverlayHandleInvalid;
                        }
                        vr::VR_Shutdown();
                        pVRSystem = nullptr;
                        steamVRActive = false;
                        lastActiveAppKey = "";
                        lastAppliedVrScale = -1.0f;
                        smoothing.reset();

                        if (launchedWithSteamVR) {
                            logDebug("[VERBOSE SHUTDOWN] Application was launched with SteamVR - Closing window on SteamVR quit.");
                            glfwSetWindowShouldClose(window, GLFW_TRUE);
                        }
                        break;
                    }
                }
            } catch (...) {}
        }

        float gpuMs = 0.0f;
        float cpuMs = 0.0f;

        if (pVRSystem && steamVRActive) {
            // --- 3-TIER ROBUST HMD REFRESH RATE AUTO-DETECTION ---
            float detectedHz = 0.0f;

            // Tier 1: Hardware Tracked Property (Index, Vive, Beyond, etc.)
            if (pVRSystem->IsTrackedDeviceConnected(vr::k_unTrackedDeviceIndex_Hmd)) {
                vr::ETrackedPropertyError err = vr::TrackedProp_Success;
                float freq = pVRSystem->GetFloatTrackedDeviceProperty(vr::k_unTrackedDeviceIndex_Hmd, vr::Prop_DisplayFrequency_Float, &err);
                if (err == vr::TrackedProp_Success && freq > 30.0f && freq < 300.0f) {
                    detectedHz = freq;
                }
            }

            // Tier 2: SteamVR Preferred Refresh Rate in Settings
            if (detectedHz <= 30.0f && vr::VRSettings()) {
                vr::EVRSettingsError setErr = vr::VRSettingsError_None;
                float prefHz = vr::VRSettings()->GetFloat(vr::k_pch_SteamVR_Section, vr::k_pch_SteamVR_PreferredRefreshRate, &setErr);
                if (setErr == vr::VRSettingsError_None && prefHz > 30.0f && prefHz < 300.0f) {
                    detectedHz = prefHz;
                }
            }

            // Tier 3: Compositor Hardware VSync Frame Interval (Meta Quest, Virtual Desktop, Steam Link, ALVR)
            if (detectedHz <= 30.0f && vr::VRCompositor()) {
                vr::Compositor_FrameTiming timing{};
                timing.m_nSize = sizeof(vr::Compositor_FrameTiming);
                if (vr::VRCompositor()->GetFrameTiming(&timing, 0) && timing.m_flClientFrameIntervalMs > 3.0f && timing.m_flClientFrameIntervalMs < 40.0f) {
                    float measuredHz = 1000.0f / timing.m_flClientFrameIntervalMs;
                    for (float standardHz : { 72.0f, 80.0f, 90.0f, 120.0f, 144.0f }) {
                        if (std::abs(measuredHz - standardHz) <= 2.5f) {
                            measuredHz = standardHz;
                            break;
                        }
                    }
                    detectedHz = measuredHz;
                }
            }

            if (detectedHz > 30.0f) {
                activeHz = detectedHz;
                if (pShared) pShared->detectedHmdHz = detectedHz;
            }

            // Query native render target size if not initialized
            if (pShared && (pShared->nativeWidth == 0 || pShared->nativeWidth == 2880)) {
                uint32_t recW = 0, recH = 0;
                pVRSystem->GetRecommendedRenderTargetSize(&recW, &recH);
                if (recW > 0 && recH > 0) {
                    cachedNativeWidth = recW;
                    cachedNativeHeight = recH;
                    pShared->nativeWidth = recW;
                    pShared->nativeHeight = recH;
                }
            }

            char appKeyBuf[128] = {0};
            try {
                if (vr::VRCompositor() != nullptr && vr::VRApplications() != nullptr) {
                    uint32_t activePid = vr::VRCompositor()->GetCurrentSceneFocusProcess();
                    if (activePid > 0) {
                        vr::EVRApplicationError appErr = vr::VRApplications()->GetApplicationKeyByProcessId(activePid, appKeyBuf, sizeof(appKeyBuf) - 1);
                        appKeyBuf[127] = '\0';
                        if (appErr != vr::VRApplicationError_None || appKeyBuf[0] == '\0') {
                            // Fallback: Query process executable name for standalone / non-manifest VR games
                            HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, activePid);
                            if (hProc != NULL) {
                                char procPath[MAX_PATH] = {0};
                                DWORD procPathLen = MAX_PATH;
                                if (QueryFullProcessImageNameA(hProc, 0, procPath, &procPathLen)) {
                                    std::string fullPath(procPath);
                                    size_t slash = fullPath.find_last_of("\\/");
                                    std::string exeName = (slash != std::string::npos) ? fullPath.substr(slash + 1) : fullPath;
                                    std::string fallbackKey = "system.generated." + exeName;
                                    strncpy_s(appKeyBuf, fallbackKey.c_str(), sizeof(appKeyBuf) - 1);
                                }
                                CloseHandle(hProc);
                            }
                        }
                    }
                }
            } catch (...) {
                appKeyBuf[0] = '\0';
            }

            // PER-GAME SCALING: Include all active scene games (Steam and Non-Steam), exclude Dashboard/Overlays
            bool isSceneGame = (appKeyBuf[0] != '\0' && 
                                strncmp(appKeyBuf, "system.overlay.", 15) != 0 && 
                                strncmp(appKeyBuf, "valve.steamvr.", 14) != 0);

            if (isSceneGame) {
                // Game switched or initial launch: reset scaling state for immediate sync
                if (std::string(appKeyBuf) != lastActiveAppKey) {
                    lastActiveAppKey = std::string(appKeyBuf);
                    lastAppliedVrScale = -1.0f;
                    smoothing.reset();
                }

                activeAppKeyStr = std::string(appKeyBuf);
                if (pShared) {
                    strncpy_s(pShared->activeAppKey, appKeyBuf, sizeof(pShared->activeAppKey) - 1);
                    pShared->frameCounter++;
                }

                try {
                    if (vr::VRCompositor()) {
                        vr::Compositor_FrameTiming timing{};
                        timing.m_nSize = sizeof(vr::Compositor_FrameTiming);
                        if (vr::VRCompositor()->GetFrameTiming(&timing, 0)) {
                            // Prefer game pre-submit GPU time if available, fallback to total render GPU time
                            float rawGpu = (timing.m_flPreSubmitGpuMs > 0.0f) 
                                ? (timing.m_flPreSubmitGpuMs + timing.m_flPostSubmitGpuMs) 
                                : timing.m_flTotalRenderGpuMs;
                            
                            // Filter out non-render noise samples
                            if (rawGpu > 1.0f && rawGpu < 100.0f) {
                                lastValidGpuMs = rawGpu;
                            }
                            gpuMs = lastValidGpuMs;

                            // ACCURATE GAME CPU FRAMETIME: Time between WaitGetPoses return and second eye Submit
                            float rawCpu = 0.0f;
                            if (timing.m_flNewFrameReadyMs > timing.m_flNewPosesReadyMs) {
                                rawCpu = timing.m_flNewFrameReadyMs - timing.m_flNewPosesReadyMs;
                            }
                            if (rawCpu <= 0.1f) {
                                rawCpu = timing.m_flCompositorRenderCpuMs + timing.m_flSubmitFrameMs;
                            }
                            if (rawCpu > 0.1f && rawCpu < 100.0f) {
                                lastValidCpuMs = rawCpu;
                            }
                            cpuMs = lastValidCpuMs;
                        }
                    }

                    float targetScalingHz = activeHz;
                    if (pShared && !pShared->autoDetectHz) {
                        int hzIdx = std::clamp(config.selectedHzIndex, 0, (int)IM_ARRAYSIZE(hzValues) - 1);
                        targetScalingHz = hzValues[hzIdx];
                    } else if (config.useHalfRateSSW) {
                        targetScalingHz = activeHz / 2.0f;
                    }

                    if (pShared && pShared->isEnabled) {
                        smoothing.configure(targetScalingHz, pShared->minScale, pShared->maxScale, config.cooldownSeconds, config.enableCpuBalancing, config.cpuEmergencyFloor);
                        currentScale = smoothing.update(gpuMs, cpuMs, currentScale);

                        float appliedVrScale = currentScale;
                        if (pShared->debugForceBlack) {
                            appliedVrScale = 0.50f;
                        }

                        // COMPLETE VRSETTINGS UPDATE WITH DUAL WRITE (Int32 + Float)
                        if (lastAppliedVrScale < 0.0f || std::abs(appliedVrScale - lastAppliedVrScale) >= 0.019f) {
                            if (vr::VRSettings() != nullptr && appKeyBuf[0] != '\0') {
                                int32_t intPct = static_cast<int32_t>(std::round(appliedVrScale * 100.0f));
                                vr::EVRSettingsError err1 = vr::VRSettingsError_None;
                                vr::EVRSettingsError err2 = vr::VRSettingsError_None;

                                try {
                                    // 1. Set Integer resolutionScale
                                    vr::VRSettings()->SetInt32(appKeyBuf, "resolutionScale", intPct, &err1);

                                    // 2. Set Float supersampleScale
                                    vr::VRSettings()->SetFloat(appKeyBuf, vr::k_pch_SteamVR_SupersampleScale_Float, appliedVrScale, &err2);

                                    lastAppliedVrScale = appliedVrScale;
                                    logDebug("[VERBOSE VRSETTINGS UPDATE] GameKey=" + std::string(appKeyBuf) + " IntScale=" + std::to_string(intPct) + "% FloatScale=" + std::to_string(appliedVrScale));
                                } catch (...) {
                                    logDebug("[VERBOSE EXCEPTION] Exception during VRSettings update");
                                }
                            }
                        }

                        pShared->targetScale = appliedVrScale;
                        pShared->activeAppliedScale = appliedVrScale;
                        pShared->currentGpuFrametime = gpuMs;
                        pShared->currentCpuFrametime = cpuMs;

                        // Square-root dimension scaling for correct pixel area
                        float dimFactor = std::sqrt(appliedVrScale);
                        pShared->activeWidth = static_cast<uint32_t>(std::round(pShared->nativeWidth * dimFactor));
                        pShared->activeHeight = static_cast<uint32_t>(std::round(pShared->nativeHeight * dimFactor));
                    }
                } catch (const std::exception& ex) {
                    logDebug(std::string("[VERBOSE EXCEPTION] Exception in game scaling loop: ") + ex.what());
                } catch (...) {
                    logDebug("[VERBOSE EXCEPTION] Unknown exception in game scaling loop");
                }
            } else {
                activeAppKeyStr = "Waiting for VR Game (e.g. VRChat)...";
                currentScale = 1.0f;
                lastAppliedVrScale = -1.0f;
                lastActiveAppKey = "";
                smoothing.reset();
                if (pShared) {
                    pShared->targetScale = 1.0f;
                    pShared->activeAppliedScale = 1.0f;
                    pShared->currentGpuFrametime = 0.0f;
                    pShared->currentCpuFrametime = 0.0f;
                    pShared->activeWidth = pShared->nativeWidth;
                    pShared->activeHeight = pShared->nativeHeight;
                    pShared->activeAppKey[0] = '\0';
                }
            }
        } else {
            currentScale = 1.0f;
            lastAppliedVrScale = -1.0f;
            lastActiveAppKey = "";
            smoothing.reset();
            activeAppKeyStr = "None (SteamVR Idle / Settings Mode)";
            if (pShared) {
                pShared->targetScale = 1.0f;
                pShared->activeAppliedScale = 1.0f;
                pShared->currentGpuFrametime = 0.0f;
                pShared->currentCpuFrametime = 0.0f;
                pShared->activeWidth = pShared->nativeWidth;
                pShared->activeHeight = pShared->nativeHeight;
                pShared->activeAppKey[0] = '\0';
            }
        }

        gpuFrametimeHistory.erase(gpuFrametimeHistory.begin());
        gpuFrametimeHistory.push_back(gpuMs);

        cpuFrametimeHistory.erase(cpuFrametimeHistory.begin());
        cpuFrametimeHistory.push_back(cpuMs);

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        int win_w, win_h;
        glfwGetWindowSize(window, &win_w, &win_h);
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2((float)win_w, (float)win_h));

        ImGui::Begin("Syn Dynamic Res Scale", NULL, 
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);

        // --- HEADER BAR ---
        ImGui::TextColored(ImVec4(0.3f, 0.7f, 1.0f, 1.0f), "SYN DYNAMIC RES SCALE");
        ImGui::SameLine();
        ImGui::TextDisabled("| Per-App Dynamic Resolution");

        ImGui::Separator();
        ImGui::Spacing();

        // --- STEAMVR PROCESS DETECTION STATUS ---
        ImGui::Text("SteamVR Status:"); ImGui::SameLine();
        if (steamVRActive && pVRSystem) {
            ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.3f, 1.0f), "[ ACTIVE - CONNECTED ]");
        } else {
            ImGui::TextColored(ImVec4(0.85f, 0.65f, 0.25f, 1.00f), "[ SETTINGS MODE - STEAMVR IDLE ]");
        }

        ImGui::SameLine(ImGui::GetWindowWidth() - 190);
        if (ImGui::Button("Register with SteamVR", ImVec2(175, 24))) {
            if (registerManifestWithSteamVR()) {
                statusMessage = "Registered! Restart SteamVR to see Syn Dynamic Res Scale in Startup Overlay Apps.";
            } else {
                statusMessage = "Already registered in Steam appconfig.json.";
            }
        }

        ImGui::Spacing();

        // --- TELEMETRY DASHBOARD ---
        if (ImGui::CollapsingHeader("Live Telemetry & Resolution Readout", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.12f, 0.13f, 0.16f, 1.00f));
            ImGui::BeginChild("TelemetryFrame", ImVec2(0, 140), true);

            int hzIdx = std::clamp(config.selectedHzIndex, 0, (int)IM_ARRAYSIZE(hzValues) - 1);
            float effHz = config.autoDetectHz ? (config.useHalfRateSSW ? activeHz / 2.0f : activeHz) : hzValues[hzIdx];
            float frameBudgetMs = (effHz > 10.0f) ? (1000.0f / effHz) : 11.11f;

            if (steamVRActive && pVRSystem) {
                float currentPct = currentScale * 100.0f;
                uint32_t w = pShared ? pShared->activeWidth : cachedNativeWidth;
                uint32_t h = pShared ? pShared->activeHeight : cachedNativeHeight;
                bool isCpuSpiking = (cpuMs > frameBudgetMs * 0.85f && cpuMs > gpuMs);

                ImGui::Columns(2, "TeleCols", false);
                ImGui::Text("Active Render Scale:");
                ImGui::SetWindowFontScale(1.4f);
                ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.0f), "%.1f%%  [%u x %u px]", currentPct, w, h);
                ImGui::SetWindowFontScale(1.0f);
                ImGui::TextDisabled("Target Game: %s", activeAppKeyStr.c_str());

                ImGui::NextColumn();
                ImGui::Text("Frametime Readout (Target: %.1f ms):", frameBudgetMs);
                ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.15f, 1.0f), "GPU: %.1f ms", gpuMs);
                ImGui::SameLine();
                ImGui::Text(" | ");
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.2f, 0.65f, 1.00f, 1.0f), "CPU: %.1f ms", cpuMs);

                if (isCpuSpiking && config.enableCpuBalancing) {
                    ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.4f, 1.0f), "[ AUTO CPU BOTTLENECK SHIELD ACTIVE ]");
                } else {
                    ImGui::TextDisabled("State: Normal GPU Scaling");
                }
                
                // DUAL OVERLAID GRAPH (GPU = Warm Orange | CPU = Cyan Blue)
                float graphMaxMs = (std::max)(25.0f, frameBudgetMs * 1.5f);
                ImVec2 graphSize = ImVec2(-1, 55);
                ImVec2 cursorBeforeGraph = ImGui::GetCursorPos();

                // 1. Draw CPU Line in Cyan Blue
                ImGui::PushStyleColor(ImGuiCol_PlotLines, ImVec4(0.2f, 0.65f, 1.00f, 1.0f));
                ImGui::PlotLines("##CPUPlot", cpuFrametimeHistory.data(), (int)cpuFrametimeHistory.size(), 0, NULL, 0.0f, graphMaxMs, graphSize);
                ImGui::PopStyleColor();

                // 2. Overlay GPU Line in Warm Orange
                ImGui::SetCursorPos(cursorBeforeGraph);
                ImGui::PushStyleColor(ImGuiCol_PlotLines, ImVec4(1.0f, 0.55f, 0.15f, 0.90f));
                ImGui::PlotLines("##GPUPlot", gpuFrametimeHistory.data(), (int)gpuFrametimeHistory.size(), 0, NULL, 0.0f, graphMaxMs, graphSize);
                ImGui::PopStyleColor();

                ImGui::Columns(1);
            } else {
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 40);
                ImGui::TextColored(ImVec4(0.85f, 0.65f, 0.25f, 1.0f), "  [SETTINGS EDITOR MODE] SteamVR is currently not open.");
                ImGui::TextDisabled("  Adjust settings below. When SteamVR launches, dynamic resolution will connect automatically.");
            }

            ImGui::EndChild();
            ImGui::PopStyleColor();
        }

        ImGui::Spacing();

        // --- CONTROLS SECTION ---
        if (ImGui::CollapsingHeader("Resolution & Smoothing Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (pShared) {
                if (ImGui::Checkbox("Master Enable (Dynamic Resolution)", &config.isEnabled)) {
                    pShared->isEnabled = config.isEnabled;
                    configChanged = true;
                }

                if (ImGui::Checkbox("Auto-Detect HMD Refresh Rate from SteamVR", &config.autoDetectHz)) {
                    pShared->autoDetectHz = config.autoDetectHz;
                    configChanged = true;
                }

                if (ImGui::Checkbox("SSW / Frame Gen Half-Rate Target (e.g. 60 FPS for 120 Hz HMD)", &config.useHalfRateSSW)) {
                    configChanged = true;
                }

                if (ImGui::Checkbox("Auto-Detect CPU Bottlenecks & Shield SSW Frame Gen", &config.enableCpuBalancing)) {
                    configChanged = true;
                }

                if (ImGui::Checkbox("Enable Debug File Logging (debug.log)", &config.enableLogging)) {
                    g_enableLogging = config.enableLogging;
                    configChanged = true;
                }

                if (config.autoDetectHz) {
                    float effHz = config.useHalfRateSSW ? activeHz / 2.0f : activeHz;
                    ImGui::TextColored(ImVec4(0.3f, 0.8f, 0.4f, 1.0f), "  HMD Auto-Detected: %.1f Hz | Scaling Target: %.1f Hz (Frame Budget: %.2f ms)", 
                        activeHz, effHz, (effHz > 10.0f) ? (1000.0f / effHz) : 11.11f);
                } else {
                    if (ImGui::Combo("Manual Target Refresh Rate", &config.selectedHzIndex, hzOptions, IM_ARRAYSIZE(hzOptions))) {
                        config.selectedHzIndex = std::clamp(config.selectedHzIndex, 0, (int)IM_ARRAYSIZE(hzValues) - 1);
                        smoothing.configure(hzValues[config.selectedHzIndex], pShared->minScale, pShared->maxScale, config.cooldownSeconds, config.enableCpuBalancing, config.cpuEmergencyFloor);
                        configChanged = true;
                    }
                }

                ImGui::Spacing();

                // COOLDOWN DELAY SLIDER
                if (ImGui::SliderFloat("Adjustment Delay / Lock Cooldown", &config.cooldownSeconds, 0.5f, 10.0f, "%.1f seconds")) {
                    configChanged = true;
                }
                ImGui::TextDisabled("  How long resolution stays static between updates to prevent texture thrashing.");

                ImGui::Spacing();

                if (config.enableCpuBalancing) {
                    float cpuFloorPct = config.cpuEmergencyFloor * 100.0f;
                    if (ImGui::SliderFloat("CPU Emergency Scale Hard Floor", &cpuFloorPct, 30.0f, 80.0f, "%.0f%%")) {
                        config.cpuEmergencyFloor = cpuFloorPct / 100.0f;
                        configChanged = true;
                    }
                    ImGui::TextDisabled("  Hard floor resolution when CPU spikes hit to keep GPU finished fast for SSW.");
                    ImGui::Spacing();
                }

                float minPct = config.minScale * 100.0f;
                float maxPct = config.maxScale * 100.0f;

                if (ImGui::SliderFloat("Minimum Resolution Scale", &minPct, 30.0f, 100.0f, "%.0f%%")) {
                    config.minScale = minPct / 100.0f;
                    if (config.minScale > config.maxScale) config.maxScale = config.minScale;
                    pShared->minScale = config.minScale;
                    pShared->maxScale = config.maxScale;
                    configChanged = true;
                }

                if (ImGui::SliderFloat("Maximum Resolution Scale", &maxPct, 100.0f, 250.0f, "%.0f%%")) {
                    config.maxScale = maxPct / 100.0f;
                    if (config.maxScale < config.minScale) config.minScale = config.maxScale;
                    pShared->minScale = config.minScale;
                    pShared->maxScale = config.maxScale;
                    configChanged = true;
                }
            }
        }

        ImGui::Spacing();

        // --- DEBUG TEST SECTION ---
        if (ImGui::CollapsingHeader("Visual Debug & Scale Test", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (pShared) {
                bool isTestActive = pShared->debugForceBlack;
                if (isTestActive) {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.25f, 0.25f, 1.00f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.95f, 0.35f, 0.35f, 1.00f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.70f, 0.15f, 0.15f, 1.00f));
                    if (ImGui::Button("FORCE 50% RESOLUTION: [ ACTIVE ]", ImVec2(320, 28))) {
                        pShared->debugForceBlack = false;
                    }
                    ImGui::PopStyleColor(3);
                } else {
                    if (ImGui::Button("FORCE 50% RESOLUTION: [ OFF ]", ImVec2(320, 28))) {
                        pShared->debugForceBlack = true;
                    }
                }

                ImGui::SameLine();
                ImGui::TextDisabled("(Click to toggle - Not Saved)");

                if (pShared->debugForceBlack) {
                    ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "  [TEST ACTIVE] Per-App Resolution dropped to 50% for %s!", activeAppKeyStr.c_str());
                } else {
                    ImGui::TextDisabled("  Targeting active VR app: %s", activeAppKeyStr.c_str());
                }
            }
        }

        if (configChanged) {
            statusMessage = saveConfigToFile(configPath, config)
                ? "Settings saved to settings.ini"
                : "ERROR: Could not save settings to " + configPathStr;
            configChanged = false;
        }

        ImGui::Spacing();
        ImGui::Separator();

        if (ImGui::Button("Save Settings Now", ImVec2(140, 24))) {
            statusMessage = saveConfigToFile(configPath, config)
                ? "Settings manually saved to settings.ini"
                : "ERROR: Could not save settings to " + configPathStr;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%s", statusMessage.c_str());

        ImGui::End();

        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.09f, 0.10f, 0.12f, 1.00f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);

        // Low CPU usage throttle when idle or minimized
        if (!steamVRActive || glfwGetWindowAttrib(window, GLFW_ICONIFIED)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }
    }

    if (pVRSystem) {
        if (vr::VROverlay() && overlayHandle != vr::k_ulOverlayHandleInvalid) {
            vr::VROverlay()->DestroyOverlay(overlayHandle);
        }
        vr::VR_Shutdown();
    }
    if (pShared) UnmapViewOfFile(pShared);
    if (hMapFile) CloseHandle(hMapFile);

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();

    logDebug("[VERBOSE SHUTDOWN] Application Shutdown Cleanly.");
    return 0;
}
