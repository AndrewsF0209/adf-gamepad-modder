# Loads the VS 2022 developer environment for the given target architecture.
param(
    [ValidateSet('x64', 'x86')]
    [string]$Arch = 'x64'
)

$buildTools = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools'
$devshell = Join-Path $buildTools 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll'

Import-Module $devshell
$vsArch = if ($Arch -eq 'x86') { 'x86' } else { 'amd64' }
Enter-VsDevShell -VsInstallPath $buildTools -Arch $vsArch -HostArch amd64 `
    -SkipAutomaticLocation | Out-Null
# Enter-VsDevShell intermittently fails (batch expansion chokes on paths with
# spaces); if INCLUDE/LIB stayed empty, set them manually.
if (-not $env:INCLUDE -or -not $env:LIB) {
    & (Join-Path $PSScriptRoot 'vsenv.ps1') -Arch $Arch
}
