# Configures (if needed) and builds the native components for one architecture.
param(
    [ValidateSet('x64', 'x86')]
    [string]$Arch = 'x64',
    [switch]$SkipConfigure
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'devshell.ps1') -Arch $Arch

$repo = Split-Path $PSScriptRoot -Parent
$buildDir = Join-Path $repo "build\$Arch"
$sdlSrc = Join-Path $repo 'third_party\_dl\SDL-release-3.4.16'

if (-not $SkipConfigure -or -not (Test-Path (Join-Path $buildDir 'build.ninja'))) {
    cmake -G Ninja -S $repo -B $buildDir `
        "-DADF_SDL3_SOURCE_DIR=$sdlSrc" `
        -DCMAKE_BUILD_TYPE=Release
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

cmake --build $buildDir
exit $LASTEXITCODE
