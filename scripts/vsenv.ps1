# Sets MSVC + Windows SDK env vars manually (Enter-VsDevShell is broken in
# this environment: its batch expansion chokes on paths with spaces).
param(
    [ValidateSet('x64', 'x86')]
    [string]$Arch = 'x64'
)

$msvc = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.44.35207'
$sdk  = 'C:\Program Files (x86)\Windows Kits\10'
$sdkVer = '10.0.26100.0'
$hostArch = 'x64'
$targetArch = $Arch

$env:INCLUDE = @(
    "$msvc\include"
    "$sdk\Include\$sdkVer\ucrt"
    "$sdk\Include\$sdkVer\um"
    "$sdk\Include\$sdkVer\shared"
    "$sdk\Include\$sdkVer\winrt"
    "$sdk\Include\$sdkVer\cppwinrt"
) -join ';'

$env:LIB = @(
    "$msvc\lib\$targetArch"
    "$sdk\Lib\$sdkVer\ucrt\$targetArch"
    "$sdk\Lib\$sdkVer\um\$targetArch"
) -join ';'

# rc.exe / mt.exe live in the SDK bin folder; CMake needs them on PATH (and
# CMAKE_MT explicitly, otherwise the linker step reports CMAKE_MT-NOTFOUND).
$sdkBin = "$sdk\bin\$sdkVer\$hostArch"
$env:PATH = "$msvc\bin\Host$hostArch\$targetArch;$sdkBin;$env:PATH"
$env:CMAKE_MT = "$sdkBin\mt.exe"
