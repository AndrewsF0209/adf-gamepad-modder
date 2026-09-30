// Fallback deployment: launches the game as a child process and runs the
// mapping engine in-process until the game exits. Used when no suitable
// proxy DLL can be installed next to the game executable.

#include <windows.h>
#include <shellapi.h>

#include <string>

#include "adf/config.h"
#include "adf/mapper.h"

namespace {

std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(),
                                static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()),
                        w.data(), n);
    return w;
}

std::wstring ExeDir() {
    wchar_t buf[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring p(buf);
    size_t slash = p.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L"." : p.substr(0, slash);
}

void Fatal(const std::wstring& msg) {
    MessageBoxW(nullptr, msg.c_str(), L"ADF Gamepad Mapper", MB_ICONERROR);
}

bool RelaunchElevated() {
    wchar_t self[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    HINSTANCE r = ShellExecuteW(nullptr, L"runas", self, nullptr, nullptr,
                                SW_SHOWNORMAL);
    return reinterpret_cast<INT_PTR>(r) > 32;
}

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    const std::wstring dir = ExeDir();
    const std::wstring cfgPath = dir + L"\\adf_mapper.json";

    adf::Config cfg;
    std::string err;
    if (!cfg.Load(cfgPath, err)) {
        Fatal(L"adf_mapper.json load failed:\n" +
              std::wstring(err.begin(), err.end()));
        return 1;
    }
    if (cfg.gameExe.empty()) {
        Fatal(L"adf_mapper.json does not specify gameExe.");
        return 1;
    }

    std::wstring exeName = Utf8ToWide(cfg.gameExe);
    const bool absolute = exeName.size() >= 2 && exeName[1] == L':';
    std::wstring target = absolute ? exeName : dir + L"\\" + exeName;
    std::wstring args = Utf8ToWide(cfg.gameArgs);
    std::wstring cmd = L"\"" + target + L"\"";
    if (!args.empty()) cmd += L" " + args;

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};

    if (!CreateProcessW(target.c_str(), cmd.data(), nullptr, nullptr, FALSE,
                        0, nullptr, dir.c_str(), &si, &pi)) {
        if (GetLastError() == ERROR_ELEVATION_REQUIRED) {
            RelaunchElevated();
            return 0;
        }
        Fatal(L"Could not start the game:\n" + target);
        return 1;
    }

    CloseHandle(pi.hThread);

    adf::RunMapperFromFile(
        cfgPath, pi.dwProcessId,
        [&] { return WaitForSingleObject(pi.hProcess, 0) == WAIT_TIMEOUT; },
        err);

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess);
    return static_cast<int>(exitCode);
}
