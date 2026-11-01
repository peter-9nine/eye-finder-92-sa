# Builds the helper (64-bit), the plugin (32-bit) and the test harness (32-bit).
param([string]$Configuration = 'Release')

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent

function Check($what) {
    if ($LASTEXITCODE -ne 0) { throw "$what failed with exit code $LASTEXITCODE" }
}

& (Join-Path $PSScriptRoot 'fetch-cef.ps1')

cmake -S "$root\helper" -B "$root\helper\build" -G 'Visual Studio 17 2022' -A x64
Check 'Configuring the helper'
cmake --build "$root\helper\build" --config Release --parallel
Check 'Building the helper'

cmake -S "$root\tools\harness" -B "$root\tools\harness\build" -G 'Visual Studio 17 2022' -A Win32
Check 'Configuring the harness'
cmake --build "$root\tools\harness\build" --config Release --parallel
Check 'Building the harness'

$msbuild = 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe'
& $msbuild "$root\EyeFinder92SA.sln" /nologo /v:m /m "/p:Configuration=$Configuration" /p:Platform=GTASA
Check 'Building the plugin'

Write-Host 'Build finished'
