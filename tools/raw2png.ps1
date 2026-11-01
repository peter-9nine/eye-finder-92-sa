# Turns the harness's raw 1280x720 BGRX captures into PNG files next to them.
param([Parameter(Mandatory)][string]$Folder)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$format = [System.Drawing.Imaging.PixelFormat]::Format32bppRgb
$area = New-Object System.Drawing.Rectangle 0, 0, 1280, 720

$count = 0
foreach ($file in Get-ChildItem $Folder -Filter '*.raw') {
    $bytes = [IO.File]::ReadAllBytes($file.FullName)
    $bitmap = New-Object System.Drawing.Bitmap 1280, 720, $format
    $data = $bitmap.LockBits($area, [System.Drawing.Imaging.ImageLockMode]::WriteOnly, $format)
    [Runtime.InteropServices.Marshal]::Copy($bytes, 0, $data.Scan0, $bytes.Length)
    $bitmap.UnlockBits($data)
    $bitmap.Save([IO.Path]::ChangeExtension($file.FullName, '.png'), [System.Drawing.Imaging.ImageFormat]::Png)
    $bitmap.Dispose()
    $count++
}
Write-Host "Converted $count captures in $Folder"
