# Builds both native architectures, publishes the WPF configurator and
# stages native\<x86|x64> next to it so the release folder is portable:
# it can be zipped/copied to another machine with no source tree and the
# deploy step still finds the proxy DLLs / launcher.
param(
    [switch]$SkipBuild,
    [switch]$SkipDist
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent

if (-not $SkipBuild) {
    & (Join-Path $PSScriptRoot 'build.ps1') -Arch x64
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    & (Join-Path $PSScriptRoot 'build.ps1') -Arch x86
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

$pub = Join-Path $repo 'artifacts\publish_configurator'
dotnet publish (Join-Path $repo 'configurator\AdfConfigurator.csproj') `
    -c Release -r win-x64 --self-contained true -o $pub
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

# Stage prebuilt native components: all three proxy DLLs + the launcher,
# one folder per architecture (old 32-bit games like RA2 need x86).
$nativeFiles = @('winmm.dll', 'version.dll', 'dinput8.dll', 'adf_launcher.exe')
foreach ($arch in @('x64', 'x86')) {
    $src = Join-Path $repo "artifacts\$arch"
    $dst = Join-Path $pub "native\$arch"
    New-Item -ItemType Directory -Force -Path $dst | Out-Null
    foreach ($f in $nativeFiles) {
        $srcFile = Join-Path $src $f
        if (-not (Test-Path $srcFile)) { throw "missing $srcFile - build native first" }
        Copy-Item $srcFile $dst -Force
    }
}

if (-not $SkipDist) {
    $dist = Join-Path $repo 'dist\AdfConfigurator'
    robocopy $pub $dist /MIR /NFL /NDL /NJH /NJS /R:1 /W:1 | Out-Null
    # robocopy exit codes < 8 are success (1 = files copied)
    if ($LASTEXITCODE -ge 8) { exit $LASTEXITCODE }
}

Write-Host "published: $pub"
Write-Host "dist:      $dist"
