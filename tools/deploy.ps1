# Copies the plugin and helper to modloader. It refuses while the game is running.
param([string]$Configuration = 'Release', [switch]$Demo)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent

if (-not $env:GTA_SA_DIR) { throw 'GTA_SA_DIR is not set' }
if (Get-Process gta_sa -ErrorAction SilentlyContinue) { throw 'Close the game before deploying' }

$target = Join-Path $env:GTA_SA_DIR 'modloader\EyeFinder92SA'
$browser = Join-Path $target 'browser'
New-Item -ItemType Directory -Force -Path $browser | Out-Null

Copy-Item "$root\bin\GTASA\$Configuration\EyeFinder92SA.asi" $target -Force

# /MIR leaves the folder identical to the build. Robocopy returns less than 8 if all went well.
robocopy "$root\helper\build\Release" $browser /MIR /NFL /NDL /NJH /NJS /NP /XF '*.pdb' '*.lib' '*.exp' '*.ilk' | Out-Null
if ($LASTEXITCODE -ge 8) { throw "robocopy failed with exit code $LASTEXITCODE" }

# The public example content is added without deleting anything, because those folders belong to whoever uses them.
robocopy "$root\public" $target /E /NFL /NDL /NJH /NJS /NP | Out-Null
if ($LASTEXITCODE -ge 8) { throw "robocopy failed with exit code $LASTEXITCODE" }

# The API example mod is only installed on request. -Demo:$false doesn't remove it.
if ($Demo) { Copy-Item "$root\tools\harness\build\Release\EyeFinderApiDemo.asi", "$root\examples\ApiDemo\values_test.efs" $target -Force }

$size = (Get-ChildItem $target -Recurse -File | Measure-Object Length -Sum).Sum / 1MB
Write-Host ("Deployed to {0} ({1:N0} MB)" -f $target, $size)
exit 0
