# Smoke test: compiles a tiny program that calls DirectInput8Create and runs
# it from a directory where OUR dinput8.dll shadows the system one. If export
# forwarding is broken the call fails / the process crashes.
param(
    [ValidateSet('x64', 'x86')]
    [string]$Arch = 'x64'
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'devshell.ps1') -Arch $Arch

$repo = Split-Path $PSScriptRoot -Parent
$work = Join-Path $repo "build\smoke\$Arch"
New-Item -ItemType Directory -Force -Path $work | Out-Null

$cpp = Join-Path $work 'proxy_check.cpp'
@'
#define INITGUID
#include <windows.h>
#include <dinput.h>
#include <cstdio>
static BOOL WINAPI StopEnum(LPCDIDEVICEINSTANCE, LPVOID) {
    return DIENUM_STOP;
}
int main() {
    IDirectInput8W* di = nullptr;
    HRESULT hr = DirectInput8Create(GetModuleHandleW(nullptr),
        DIRECTINPUT_VERSION, IID_IDirectInput8W,
        reinterpret_cast<void**>(&di), nullptr);
    std::printf("DirectInput8Create hr=0x%08lX ptr=%p\n",
                static_cast<unsigned long>(hr), static_cast<void*>(di));
    if (!di) return 1;
    HRESULT en = di->EnumDevices(DI8DEVCLASS_GAMECTRL, StopEnum,
                                  nullptr, DIEDFL_ATTACHEDONLY);
    std::printf("EnumDevices hr=0x%08lX\n", static_cast<unsigned long>(en));
    di->Release();
    return 0;
}
'@ | Set-Content -Encoding ASCII $cpp

Push-Location $work
try {
    cl /nologo /EHsc /MT /DUNICODE /D_UNICODE $cpp /Fe:proxy_check.exe dinput8.lib
    if ($LASTEXITCODE -ne 0) { throw "compilation failed" }
    Copy-Item (Join-Path $repo "artifacts\$Arch\dinput8.dll") . -Force
    & .\proxy_check.exe
    $code = $LASTEXITCODE
} finally {
    Pop-Location
}
if ($code -ne 0) { throw "proxy smoke test failed ($Arch)" }
Write-Host "SMOKE OK ($Arch)"
