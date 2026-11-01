# Builds the release package in dist: the whole mod, without CEF, which players download by themselves.
param([string]$Configuration = 'Release')

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$dist = Join-Path $root 'dist'
$package = Join-Path $dist 'EyeFinder92SA'
$browser = Join-Path $package 'browser'

if (Test-Path $dist) { Remove-Item $dist -Recurse -Force }
New-Item -ItemType Directory -Force -Path $browser | Out-Null

Copy-Item "$root\bin\GTASA\$Configuration\EyeFinder92SA.asi", "$root\locations.json", "$root\LICENSE" $package
Copy-Item "$root\helper\build\Release\EyeFinderBrowser.exe" $browser
Copy-Item "$root\helper\build\system" $package -Recurse
Copy-Item "$root\dev" $package -Recurse
Copy-Item "$root\licenses" $package -Recurse
Copy-Item "$root\public\*" $package -Recurse
& (Join-Path $PSScriptRoot 'make-installer.ps1') -ModFolder $package

$unwanted = Get-ChildItem $package -Recurse -File -Include 'libcef.dll', 'chrome_elf.dll', '*.pak', 'icudtl.dat', '*.pdb'
if ($unwanted) { throw "CEF files got into the package: $($unwanted.Name -join ', ')" }

$zip = Join-Path $dist 'EyeFinder92SA.zip'
Compress-Archive -Path $package -DestinationPath $zip
Write-Host ("Package: {0} ({1:N1} MB)" -f $zip, ((Get-Item $zip).Length / 1MB))
