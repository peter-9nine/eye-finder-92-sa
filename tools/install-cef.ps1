# Downloads CEF, the browser engine, into the mod's browser folder. Players run it through Install CEF.bat.
# tools\package.ps1 fills in the {{...}} values, so that they always match the build being released.
param([switch]$Yes)

$ErrorActionPreference = 'Stop'

$name = '{{NAME}}'
$sha1 = '{{SHA1}}'
$version = '{{VERSION}}'
$installedMb = [int]'{{INSTALLED_MB}}'
$unpackedMb = [int]'{{UNPACKED_MB}}'
$fileCount = [int]'{{FILE_COUNT}}'

$url = "https://cef-builds.spotifycdn.com/$($name.Replace('+', '%2B')).tar.bz2"
$modFolder = $PSScriptRoot
$browser = Join-Path $modFolder 'browser'
$work = Join-Path $browser '_cef_install'
$archive = Join-Path $work 'cef.tar.bz2'
$required = 'libcef.dll', 'chrome_elf.dll', 'icudtl.dat', 'v8_context_snapshot.bin', 'resources.pak', 'chrome_100_percent.pak', 'locales\en-US.pak'

function Fail([string]$message) {
    Write-Host ''
    Write-Host "PROBLEM: $message" -ForegroundColor Red
    Write-Host 'Nothing in the game was changed. You can run this again at any time.'
    exit 1
}

function Show-Status([string]$text) {
    # Rewrites the same line, unless the output is going to a file.
    if ([Console]::IsOutputRedirected) { return }
    $width = [Math]::Max([Console]::WindowWidth - 1, 40)
    if ($text.Length -gt $width) { $text = $text.Substring(0, $width) }
    [Console]::Write("`r" + $text.PadRight($width))
}

function Format-Mb([double]$bytes) { '{0:N0} MB' -f ($bytes / 1MB) }

function Get-InstalledVersion {
    $library = Join-Path $browser 'libcef.dll'
    if (-not (Test-Path -LiteralPath $library)) { return $null }
    return (Get-Item -LiteralPath $library).VersionInfo.FileVersion
}

function Get-MissingFiles {
    foreach ($file in $required) {
        if (-not (Test-Path -LiteralPath (Join-Path $browser $file))) { $file }
    }
}

# Runs the helper with nothing to do. It exits with code 2 if it could load CEF, and with a Windows
# error if it couldn't. UseShellExecute is off so that no window or warning appears for this.
function Test-Helper {
    $start = New-Object Diagnostics.ProcessStartInfo
    $start.FileName = Join-Path $browser 'EyeFinderBrowser.exe'
    $start.WorkingDirectory = $browser
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $process = [Diagnostics.Process]::Start($start)
    if (-not $process.WaitForExit(30000)) {
        $process.Kill()
        return $false
    }
    return $process.ExitCode -eq 2
}

function Save-Download([long]$total) {
    $request = [Net.HttpWebRequest]::Create($url)
    $request.UserAgent = 'EyeFinder92SA'
    $request.ReadWriteTimeout = 60000
    $response = $request.GetResponse()
    $input_ = $response.GetResponseStream()
    $output = [IO.File]::Create($archive)
    try {
        $buffer = New-Object byte[] (256KB)
        $received = [long]0
        $clock = [Diagnostics.Stopwatch]::StartNew()
        $lastDraw = -1000
        while (($read = $input_.Read($buffer, 0, $buffer.Length)) -gt 0) {
            $output.Write($buffer, 0, $read)
            $received += $read
            if ($clock.ElapsedMilliseconds - $lastDraw -ge 200) {
                $lastDraw = $clock.ElapsedMilliseconds
                $speed = $received / [Math]::Max($clock.Elapsed.TotalSeconds, 0.1)
                $percent = if ($total -gt 0) { 100 * $received / $total } else { 0 }
                $filled = [int][Math]::Floor($percent / 4)
                $bar = ('#' * $filled).PadRight(25, '-')
                $left = if ($speed -gt 0 -and $total -gt 0) { [TimeSpan]::FromSeconds(($total - $received) / $speed).ToString('mm\:ss') } else { '--:--' }
                Show-Status ('  [{0}] {1,3:N0}%  {2} of {3}  {4:N1} MB/s  {5} left' -f $bar, $percent, (Format-Mb $received), (Format-Mb $total), ($speed / 1MB), $left)
            }
        }
        Show-Status ('  [{0}] 100%  {1} downloaded' -f ('#' * 25), (Format-Mb $received))
        Write-Host ''
        if ($total -gt 0 -and $received -ne $total) { throw "The download stopped early, at $(Format-Mb $received) of $(Format-Mb $total)." }
    }
    finally {
        $output.Dispose()
        $input_.Dispose()
        $response.Dispose()
    }
}

function Expand-Cef {
    $tar = Join-Path $env:WINDIR 'System32\tar.exe'
    if (-not (Test-Path -LiteralPath $tar)) { throw 'This version of Windows has no tar.exe. Follow browser\INSTALL_CEF.txt to install CEF by hand.' }

    $start = New-Object Diagnostics.ProcessStartInfo
    $start.FileName = $tar
    # Only what the mod needs, without the long top folder.
    $start.Arguments = "-xvjf `"$archive`" -C `"$work`" --strip-components=1 `"$name/Release`" `"$name/Resources`" `"$name/CREDITS.html`""
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardError = $true
    $start.RedirectStandardOutput = $true

    $process = [Diagnostics.Process]::Start($start)
    $count = 0
    $spinner = '|/-\'
    while (($line = $process.StandardError.ReadLine()) -ne $null) {
        if ($line -match '^x ' -and $line -notmatch '/$') { $count++ }
        Show-Status ('  {0} Unpacked {1} of about {2} files' -f $spinner[$count % 4], $count, $fileCount)
    }
    $process.WaitForExit()
    Show-Status ('  Unpacked {0} files' -f $count)
    Write-Host ''
    if ($process.ExitCode -ne 0) { throw "Unpacking failed with code $($process.ExitCode)." }
}

function Install-Files {
    # The linker's files aren't needed to run the game.
    foreach ($source in 'Release', 'Resources') {
        $folder = Join-Path $work $source
        foreach ($file in Get-ChildItem -LiteralPath $folder -Recurse -File) {
            if ($file.Extension -in '.lib', '.pdb') { continue }
            $relative = $file.FullName.Substring($folder.Length + 1)
            $target = Join-Path $browser $relative
            New-Item -ItemType Directory -Force -Path (Split-Path $target -Parent) | Out-Null
            Move-Item -LiteralPath $file.FullName -Destination $target -Force
        }
    }
    Move-Item -LiteralPath (Join-Path $work 'CREDITS.html') -Destination (Join-Path $browser 'CEF_CREDITS.html') -Force
}

[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

Write-Host ''
Write-Host '  EyeFinder92SA: browser engine installer' -ForegroundColor Cyan
Write-Host '  ---------------------------------------'
Write-Host ''

if ($name -like '*{{*') { Fail 'This script only works from a release of the mod. Developers: tools\build.ps1 downloads CEF for you.' }
if (-not (Test-Path -LiteralPath (Join-Path $modFolder 'EyeFinder92SA.asi')) -or -not (Test-Path -LiteralPath (Join-Path $browser 'EyeFinderBrowser.exe'))) {
    Fail 'This must stay in the EyeFinder92SA mod folder, next to EyeFinder92SA.asi and the browser folder. Move it back and run it from there.'
}
if (Get-Process gta_sa -ErrorAction SilentlyContinue) { Fail 'The game is running. Close it, then run this again.' }

if ((Get-InstalledVersion) -eq $version -and -not (Get-MissingFiles) -and (Test-Helper)) {
    Write-Host 'The browser engine is already installed and working. There is nothing to do.' -ForegroundColor Green
    exit 0
}

try {
    New-Item -ItemType Directory -Force -Path $work | Out-Null
    [IO.File]::WriteAllText((Join-Path $work 'write-test'), 'ok')
}
catch {
    Fail "Windows won't let this write to the mod folder. Right-click Install CEF.bat and choose Run as administrator, or install the game outside Program Files."
}

Write-Host 'Checking the download...'
try {
    $head = [Net.HttpWebRequest]::Create($url)
    $head.Method = 'HEAD'
    $head.UserAgent = 'EyeFinder92SA'
    $head.Timeout = 30000
    $headResponse = $head.GetResponse()
    $downloadBytes = $headResponse.ContentLength
    $headResponse.Dispose()
}
catch {
    if (Test-Path -LiteralPath $work) { Remove-Item -LiteralPath $work -Recurse -Force }
    Fail "Couldn't reach the download server. Check your internet connection. ($($_.Exception.Message))"
}

$neededBytes = $downloadBytes + ($unpackedMb * 1MB)
$freeBytes = (New-Object IO.DriveInfo ([IO.Path]::GetPathRoot($modFolder))).AvailableFreeSpace

Write-Host ''
Write-Host 'To show web pages, this mod uses CEF, the engine behind the Chrome browser.'
Write-Host 'It is too big to include with the mod, so this will:'
Write-Host ''
Write-Host "  1. Download CEF $($version.Split('+')[0]) from its official server:"
Write-Host "     $($url.Substring(0, $url.IndexOf('/', 8)))"
Write-Host '  2. Check that the file is exactly the expected one.'
Write-Host '  3. Install it in the browser folder of this mod.'
Write-Host '  4. Delete the downloaded file, and test that it all works.'
Write-Host ''
Write-Host ("  Download size:     {0}" -f (Format-Mb $downloadBytes))
Write-Host ("  Space when done:   {0}" -f (Format-Mb ($installedMb * 1MB)))
Write-Host ("  Space needed now:  {0}, for a few minutes" -f (Format-Mb $neededBytes))
Write-Host ("  Free on this disk: {0}" -f (Format-Mb $freeBytes))
Write-Host ''
Write-Host 'Nothing is changed outside the mod folder. It does not need an account.'
Write-Host ''

if ($freeBytes -lt $neededBytes) {
    Remove-Item -LiteralPath $work -Recurse -Force
    Fail "There isn't enough free space on this disk. Free up $(Format-Mb ($neededBytes - $freeBytes)), then run this again."
}

if (-not $Yes) {
    $answer = Read-Host 'Install it now? (Y/N)'
    if ($answer -notmatch '^\s*y') {
        Remove-Item -LiteralPath $work -Recurse -Force
        Write-Host 'Cancelled. Nothing was changed.'
        exit 0
    }
    Write-Host ''
}

try {
    Write-Host 'Downloading...'
    try { Save-Download $downloadBytes }
    catch { throw "The download failed. Check your internet connection and try again. ($($_.Exception.Message))" }

    Write-Host ''
    Write-Host 'Checking the file...'
    $actual = (Get-FileHash -LiteralPath $archive -Algorithm SHA1).Hash.ToLower()
    if ($actual -ne $sha1) { throw 'The downloaded file is not the expected one, so it was not installed. It may have been damaged on the way. Try again.' }
    Write-Host '  It is correct.'

    Write-Host ''
    Write-Host 'Unpacking. This is the slow part, and it can take a few minutes...'
    Expand-Cef

    Write-Host ''
    Write-Host 'Installing...'
    Install-Files

    Write-Host 'Testing...'
    $missing = @(Get-MissingFiles)
    if ($missing) { throw "These files are missing after installing: $($missing -join ', ')." }
    if ((Get-InstalledVersion) -ne $version) { throw "The installed version is $(Get-InstalledVersion), but the mod needs $version." }
    if (-not (Test-Helper)) { throw 'The browser program could not start with CEF. Your antivirus may be blocking it.' }
}
catch {
    if (Test-Path -LiteralPath $work) { Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue }
    Fail $_.Exception.Message
}

Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue

Write-Host ''
Write-Host 'All done! The browser is ready.' -ForegroundColor Green
Write-Host 'Start the game, and use a computer in-game, such as the one at the cyber cafe.'
exit 0
