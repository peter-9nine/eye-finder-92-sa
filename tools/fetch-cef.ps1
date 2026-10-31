# Descarga la versión fijada de CEF (64 bits, minimal) en third_party\cef y verifica su SHA-1.
$ErrorActionPreference = 'Stop'

$name = 'cef_binary_150.0.23+g4ccc4ca+chromium-150.0.7871.287_windows64_minimal'
$sha1 = '804ecda5e1adad6ff4b7ffe3fbc45af13d1eeff6'

$root = Split-Path $PSScriptRoot -Parent
$thirdParty = Join-Path $root 'third_party'
$dest = Join-Path $thirdParty 'cef'

if (Test-Path (Join-Path $dest 'cmake\cef_variables.cmake')) {
    Write-Host "CEF already present in $dest"
    exit 0
}

New-Item -ItemType Directory -Force -Path $thirdParty | Out-Null
$archive = Join-Path $thirdParty "$name.tar.bz2"
$url = "https://cef-builds.spotifycdn.com/$($name.Replace('+', '%2B')).tar.bz2"

if (-not (Test-Path $archive)) {
    Write-Host "Downloading $url"
    & curl.exe -L --fail --silent --show-error -o $archive $url
    if ($LASTEXITCODE -ne 0) { throw "Download failed" }
}

$actual = (Get-FileHash $archive -Algorithm SHA1).Hash.ToLower()
if ($actual -ne $sha1) {
    Remove-Item $archive
    throw "SHA-1 mismatch: expected $sha1, got $actual"
}

Write-Host 'Extracting'
& "$env:WINDIR\System32\tar.exe" -xjf $archive -C $thirdParty
if ($LASTEXITCODE -ne 0) { throw "Extraction failed" }

Rename-Item (Join-Path $thirdParty $name) 'cef'
Remove-Item $archive
Write-Host "CEF ready in $dest"
