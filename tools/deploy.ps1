# Installs the mod in modloader, laid out as a player gets it. CEF isn't copied: Install CEF.bat downloads it,
# and one that's already there is kept. Building the plugin runs this too, with -FromBuild.
param(
    [string]$Configuration = 'Release',
    [string]$Plugin,
    [switch]$Demo,
    [switch]$FromBuild
)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent

# A build must never fail because of this: it says why it skipped, and carries on.
function Stop-Deploy([string]$reason) {
    if ($FromBuild) {
        Write-Host "Not copying to the game: $reason"
        exit 0
    }
    throw $reason
}

function Mirror([string]$from, [string]$to) {
    # /MIR leaves the folder identical to the source. Robocopy returns less than 8 if all went well.
    robocopy $from $to /MIR /NFL /NDL /NJH /NJS /NP | Out-Null
    if ($LASTEXITCODE -ge 8) { throw "robocopy failed with exit code $LASTEXITCODE" }
}

if (-not $env:GTA_SA_DIR) { Stop-Deploy 'GTA_SA_DIR is not set' }
if (Get-Process gta_sa -ErrorAction SilentlyContinue) { Stop-Deploy 'close the game first' }

if (-not $Plugin) { $Plugin = "$root\bin\GTASA\$Configuration\EyeFinder92SA.asi" }
$helper = "$root\helper\build\Release\EyeFinderBrowser.exe"
if (-not (Test-Path -LiteralPath $Plugin)) { Stop-Deploy "$Plugin isn't built yet" }
if (-not (Test-Path -LiteralPath $helper)) { Stop-Deploy 'the helper is not built yet. Run tools\build.ps1 once' }

$target = Join-Path $env:GTA_SA_DIR 'modloader\EyeFinder92SA'
$browser = Join-Path $target 'browser'
New-Item -ItemType Directory -Force -Path $browser | Out-Null

Copy-Item -LiteralPath $Plugin $target -Force
Copy-Item -LiteralPath $helper $browser -Force

# From the sources, so that a change to a page reaches the game without rebuilding the helper.
Mirror "$root\ui" (Join-Path $target 'system\ui')
Mirror "$root\sites" (Join-Path $target 'system\sites')
Mirror "$root\dev" (Join-Path $target 'dev')

# The public example content is added without deleting anything, because those folders belong to whoever uses them.
robocopy "$root\public" $target /E /NFL /NDL /NJH /NJS /NP | Out-Null
if ($LASTEXITCODE -ge 8) { throw "robocopy failed with exit code $LASTEXITCODE" }

& (Join-Path $PSScriptRoot 'make-installer.ps1') -ModFolder $target

# The locations are meant to be edited where they're installed, so an edited file is never overwritten.
$locations = Join-Path $target 'locations.json'
if (-not (Test-Path $locations)) { Copy-Item "$root\locations.json" $locations }
elseif ((Get-FileHash $locations).Hash -ne (Get-FileHash "$root\locations.json").Hash) { Write-Warning "$locations differs from the project's, and was kept" }

# The API example mod is only installed on request. -Demo:$false doesn't remove it.
if ($Demo) { Copy-Item "$root\tools\harness\build\Release\EyeFinderApiDemo.asi", "$root\examples\ApiDemo\values_test.efs" $target -Force }

Write-Host "Copied to $target"
if (-not (Test-Path -LiteralPath (Join-Path $browser 'libcef.dll'))) { Write-Host 'CEF is not installed there yet. Run Install CEF.bat in that folder.' }
exit 0
