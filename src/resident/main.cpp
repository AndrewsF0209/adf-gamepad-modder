// adf_resident.dll — thin wrapper that runs the full mapper engine inside
// the configurator's own process so the user gets real-time gamepad→key/mouse
// mapping while the app is open (DS4Windows / Steam Input style).
//
// The engine is the exact same code path that runs inside game processes
// via proxy DLLs — same Tick(), same gestures / radial / layers / modifiers.
// foregroundOnly is forced off so mapping is active in every application.

#include <windows.h>

#include <atomic>
#include <filesystem>
#include <string>
#include <thread>

#include "adf/mapper.h"

namespace adf {

namespace {

std::atomic<int> g_state{0}; // 0 = stopped, 1 = running, 2 = stopping
std::thread g_thread;
std::wstring g_lastErr;

// Minimal sink that pushes events into the engine. The engine calls
// InputOut internally (sink_kbdmouse.cpp) which calls SendInput — that's
// exactly what we want for resident mode: real keyboard/mouse injection.

int ResidentRun(const std::wstring& jsonPath, std::string& err) {
    // foregroundOnly=false (map for all apps), foregroundPid=0 (unused).
    return RunMapperFromFile(jsonPath, 0,
        []() { return g_state.load() == 1; }, err);
}

} // namespace

} // namespace adf

extern "C" {

// Start the resident mapper with the given JSON config path.
// Returns 0 on success, 1 if already running, 2 on init error.
__declspec(dllexport) int adf_resident_start(const wchar_t* jsonPath) {
    if (adf::g_state.load() != 0) return 1;
    if (!jsonPath || !std::filesystem::exists(jsonPath)) return 2;

    adf::g_lastErr.clear();
    adf::g_state.store(1);
    std::wstring path(jsonPath);

    adf::g_thread = std::thread([path]() {
        std::string err;
        int rc = adf::ResidentRun(path, err);
        if (rc != 0 && !err.empty())
            adf::g_lastErr = std::wstring(err.begin(), err.end());
        adf::g_state.store(0);
    });
    return 0;
}

// Stop the resident mapper. Blocks until the thread has finished.
__declspec(dllexport) void adf_resident_stop() {
    if (adf::g_state.load() == 0) return;
    adf::g_state.store(2); // signal stop
    if (adf::g_thread.joinable())
        adf::g_thread.join();
    adf::g_state.store(0);
}

// Returns 1 if running, 0 if stopped.
__declspec(dllexport) int adf_resident_is_active() {
    return adf::g_state.load() == 1 ? 1 : 0;
}

// Returns last error message (UTF-8) or empty string.
__declspec(dllexport) const char* adf_resident_last_error() {
    static thread_local std::string buf;
    if (adf::g_lastErr.empty()) return "";
    buf.assign(adf::g_lastErr.begin(), adf::g_lastErr.end());
    return buf.c_str();
}

} // extern "C"
