// Proxy DLL entry point. Does no heavy work under loader lock: it only
// disables thread notifications and starts a worker thread that runs the
// mapping engine. Export forwarding itself lives in the generated stubs.

#include <windows.h>

#include <string>

#include "adf/mapper.h"

namespace {

bool FileExists(const wchar_t* path) {
    DWORD attr = GetFileAttributesW(path);
    return attr != INVALID_FILE_ATTRIBUTES &&
           !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

DWORD WINAPI MapperThread(LPVOID) {
    wchar_t exePath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);

    std::wstring path(exePath);
    size_t slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return 0;
    std::wstring cfg = path.substr(0, slash) + L"\\adf_mapper.json";

    if (!FileExists(cfg.c_str())) return 0;

    std::string err;
    adf::RunMapperFromFile(cfg, GetCurrentProcessId(),
                           [] { return true; }, err);
    return 0;
}

} // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        HANDLE t = CreateThread(nullptr, 0, MapperThread, nullptr, 0, nullptr);
        if (t) CloseHandle(t);
    }
    return TRUE;
}
