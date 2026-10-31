# Copia el plugin y el helper a modloader. Se niega a hacerlo con el juego abierto.
param([string]$Configuration = 'Release', [switch]$Demo)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent

if (-not $env:GTA_SA_DIR) { throw 'GTA_SA_DIR is not set' }
if (Get-Process gta_sa -ErrorAction SilentlyContinue) { throw 'Close the game before deploying' }

$target = Join-Path $env:GTA_SA_DIR 'modloader\EyeFinder92SA'
$browser = Join-Path $target 'browser'
New-Item -ItemType Directory -Force -Path $browser | Out-Null

Copy-Item "$root\bin\GTASA\$Configuration\EyeFinder92SA.asi" $target -Force

# /MIR deja la carpeta igual que la compilación. Robocopy devuelve menos de 8 si todo ha ido bien.
robocopy "$root\helper\build\Release" $browser /MIR /NFL /NDL /NJH /NJS /NP /XF '*.pdb' '*.lib' '*.exp' '*.ilk' | Out-Null
if ($LASTEXITCODE -ge 8) { throw "robocopy failed with exit code $LASTEXITCODE" }

# El contenido público de ejemplo se añade sin borrar nada: esas carpetas son de quien las use.
robocopy "$root\public" $target /E /NFL /NDL /NJH /NJS /NP | Out-Null
if ($LASTEXITCODE -ge 8) { throw "robocopy failed with exit code $LASTEXITCODE" }

# El mod de ejemplo de la API solo se instala si se pide. -Demo:$false no lo quita.
if ($Demo) { Copy-Item "$root\tools\harness\build\Release\EyeFinderApiDemo.asi", "$root\examples\ApiDemo\values_test.efs" $target -Force }

$size = (Get-ChildItem $target -Recurse -File | Measure-Object Length -Sum).Sum / 1MB
Write-Host ("Deployed to {0} ({1:N0} MB)" -f $target, $size)
exit 0
