// Lazy export resolver shared by every generated proxy DLL.
// Compiled once per proxy target (each gets its own adf_proxy_names.h).

#include <windows.h>

#include <string>

#include "adf_proxy_names.h"

// g_slots is defined by the generated MASM stubs. It holds the resolved
// address of every exported function (zero until first call).
#if defined(_M_X64)
extern "C" unsigned __int64 g_slots[];
using slot_t = unsigned __int64;
#else
// extern "C" emits the leading underscore on x86, matching _g_slots in asm.
extern "C" unsigned long g_slots[];
using slot_t = unsigned long;
#endif
extern "C" void FailStub();

namespace {

std::wstring Widen(const char* s) {
    if (!s) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
    std::wstring w(n ? n - 1 : 0, L'\0');
    if (n) MultiByteToWideChar(CP_UTF8, 0, s, -1, w.data(), n);
    return w;
}

HMODULE LoadSystemDll() {
    wchar_t sysDir[MAX_PATH] = {};
    UINT n = GetSystemDirectoryW(sysDir, MAX_PATH);
    if (n > 0 && n < MAX_PATH) {
        std::wstring path = sysDir;
        path += L"\\";
        path += Widen(adf::proxy::kTargetName);
        HMODULE h = LoadLibraryExW(path.c_str(), nullptr,
                                   LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (h) return h;
    }
    // Fallback (should normally not be needed).
    return LoadLibraryW(Widen(adf::proxy::kTargetName).c_str());
}

} // namespace

// Called from the generated asm trampoline (idx in r10/eax, all argument
// registers preserved around it). Resolves the real export, caches it and
// tail-jumps to it via the asm.
extern "C" void* ResolveName(int idx) {
    // Plain (non magic) static: stubs may run under the loader lock where
    // the CRT thread-safe init guard is undesirable. Redundant concurrent
    // LoadLibrary calls are harmless.
    static HMODULE realDll = nullptr;
    if (!realDll) realDll = LoadSystemDll();

    void* p = nullptr;
    if (realDll && idx >= 0 && idx < adf::proxy::kExportCount) {
        p = reinterpret_cast<void*>(
            GetProcAddress(realDll, adf::proxy::kExportNames[idx]));
    }
    if (!p) {
        // Should never happen (table generated from the very same DLL).
        p = reinterpret_cast<void*>(&FailStub);
    }
    g_slots[idx] = reinterpret_cast<slot_t>(p);
    return p;
}
