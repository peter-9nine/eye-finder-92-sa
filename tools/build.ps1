# Builds the helper (64-bit), the plugin (32-bit) and the test harness (32-bit).
param([string]$Configuration = 'Release')

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent

function Check($what) {
    if ($LASTEXITCODE -ne 0) { throw "$what failed with exit code $LASTEXITCODE" }
}

# Say what's missing now, rather than with a compiler error later. See dev\getting-started.md.
$library = if ($Configuration -eq 'Debug') { 'plugin_d.lib' } else { 'plugin.lib' }
if (-not $env:PLUGIN_SDK_DIR) { throw 'PLUGIN_SDK_DIR is not set. See dev\getting-started.md' }
if (-not (Test-Path (Join-Path $env:PLUGIN_SDK_DIR "output\lib\$library"))) { throw "$library isn't in $env:PLUGIN_SDK_DIR\output\lib, so plugin-sdk hasn't been built. See dev\getting-started.md" }
if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) { throw 'CMake is not on the PATH. See dev\getting-started.md' }

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$msbuild = if (Test-Path $vswhere) { & $vswhere -latest -version '[17.0,18.0)' -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1 }
if (-not $msbuild) { throw 'Visual Studio 2022 with the C++ tools was not found. See dev\getting-started.md' }

& (Join-Path $PSScriptRoot 'fetch-cef.ps1')

cmake -S "$root\helper" -B "$root\helper\build" -G 'Visual Studio 17 2022' -A x64
Check 'Configuring the helper'
cmake --build "$root\helper\build" --config Release --parallel
Check 'Building the helper'

cmake -S "$root\tools\harness" -B "$root\tools\harness\build" -G 'Visual Studio 17 2022' -A Win32
Check 'Configuring the harness'
cmake --build "$root\tools\harness\build" --config Release --parallel
Check 'Building the harness'

& $msbuild "$root\EyeFinder92SA.sln" /nologo /v:m /m "/p:Configuration=$Configuration" /p:Platform=GTASA
Check 'Building the plugin'

Write-Host 'Build finished'
