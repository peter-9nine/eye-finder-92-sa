# Puts the CEF installer and its instructions in a mod folder, for deploy.ps1 and package.ps1.
# Its version, checksum and sizes are filled in here, so that they always match this build's CEF.
param([Parameter(Mandatory)][string]$ModFolder)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$utf8 = New-Object Text.UTF8Encoding($false)

$fetch = Get-Content "$PSScriptRoot\fetch-cef.ps1" -Raw
if ($fetch -notmatch "\`$name = '([^']+)'") { throw 'Could not find the CEF version in fetch-cef.ps1' }
$name = $Matches[1]
if ($fetch -notmatch "\`$sha1 = '([^']+)'") { throw 'Could not find the CEF checksum in fetch-cef.ps1' }
$sha1 = $Matches[1]
if ($name -notmatch '^cef_binary_(.+)_windows64_minimal$') { throw "Unexpected CEF name: $name" }
$version = $Matches[1]

$cef = Join-Path $root 'third_party\cef'
if (-not (Test-Path "$cef\Release")) { throw 'CEF is not in third_party. Run tools\build.ps1 first.' }
$unpacked = Get-ChildItem "$cef\Release", "$cef\Resources" -Recurse -File
$installed = $unpacked | Where-Object { $_.Extension -notin '.lib', '.pdb' }

$installer = (Get-Content "$PSScriptRoot\install-cef.ps1" -Raw).
    Replace('{{NAME}}', $name).Replace('{{SHA1}}', $sha1).Replace('{{VERSION}}', $version).
    Replace('{{INSTALLED_MB}}', [string][int](($installed | Measure-Object Length -Sum).Sum / 1MB)).
    Replace('{{UNPACKED_MB}}', [string][int](($unpacked | Measure-Object Length -Sum).Sum / 1MB)).
    Replace('{{FILE_COUNT}}', [string]($unpacked.Count + 1))  # And CREDITS.html.
[IO.File]::WriteAllText((Join-Path $ModFolder 'install-cef.ps1'), $installer, $utf8)
Copy-Item "$PSScriptRoot\Install CEF.bat" $ModFolder -Force

$url = "https://cef-builds.spotifycdn.com/$($name.Replace('+', '%2B')).tar.bz2"
$instructions = (Get-Content "$PSScriptRoot\INSTALL_CEF.txt" -Raw).Replace('{{URL}}', $url).Replace('{{NAME}}', $name)
[IO.File]::WriteAllText((Join-Path $ModFolder 'browser\INSTALL_CEF.txt'), $instructions, $utf8)
