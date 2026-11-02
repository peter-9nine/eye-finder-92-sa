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

# Windows' own tar.exe can't be used here: many Windows 10 builds have no bzip2 support and fail with
# code 1, and it can also misread non-English characters in the folder path. PowerShell 5.1 compiles
# this with C# 5, so it can't use newer syntax. Keep this file ASCII, because PowerShell 5.1 reads it
# as ANSI.
$unpackerSource = @'
using System;
using System.IO;
using System.Text;

public sealed class EyeFinderBz2Reader {
    private static readonly uint[] CrcTable = MakeCrcTable();
    private readonly Stream input;
    private ulong bitBuffer;
    private int bitCount;
    private uint[] tt;
    private int blockLength, used, run, repeat;
    private uint position, crc, expectedCrc;
    private byte previous;
    private bool inBlock, finished;

    public EyeFinderBz2Reader(Stream input) { this.input = input; ReadStreamHeader(); }

    private static uint[] MakeCrcTable() {
        uint[] table = new uint[256];
        for (uint i = 0; i < 256; i++) {
            uint c = i << 24;
            for (int k = 0; k < 8; k++) c = (c & 0x80000000) != 0 ? (c << 1) ^ 0x04c11db7 : c << 1;
            table[i] = c;
        }
        return table;
    }

    private static Exception Damaged(string what) { return new InvalidDataException("The archive is damaged (" + what + ")."); }

    private uint Bits(int n) {
        while (bitCount < n) {
            int b = input.ReadByte();
            if (b < 0) throw Damaged("it ends too early");
            bitBuffer = (bitBuffer << 8) | (uint)b;
            bitCount += 8;
        }
        bitCount -= n;
        return (uint)((bitBuffer >> bitCount) & ((1UL << n) - 1));
    }

    private void ReadStreamHeader() {
        if (Bits(8) != 'B' || Bits(8) != 'Z' || Bits(8) != 'h') throw Damaged("not bzip2 data");
        uint level = Bits(8);
        if (level < '1' || level > '9') throw Damaged("bad block size");
        int size = (int)(level - '0') * 100000;
        if (tt == null || tt.Length < size) tt = new uint[size];
    }

    private bool ReadBlock() {
        while (true) {
            ulong magic = ((ulong)Bits(24) << 24) | Bits(24);
            if (magic == 0x314159265359UL) break;
            if (magic != 0x177245385090UL) throw Damaged("bad block marker");
            Bits(32);  // The whole stream's checksum. Every block has its own, which is checked.
            bitCount -= bitCount % 8;
            if (bitCount == 0) {
                int b = input.ReadByte();
                if (b < 0) return false;
                bitBuffer = (uint)b;
                bitCount = 8;
            }
            ReadStreamHeader();
        }

        expectedCrc = Bits(32);
        if (Bits(1) != 0) throw Damaged("randomised block");
        int origin = (int)Bits(24);

        byte[] symbols = new byte[256];
        int symbolCount = 0;
        uint ranges = Bits(16);
        for (int i = 0; i < 16; i++) {
            if ((ranges & (0x8000u >> i)) == 0) continue;
            uint present = Bits(16);
            for (int j = 0; j < 16; j++) {
                if ((present & (0x8000u >> j)) != 0) symbols[symbolCount++] = (byte)(i * 16 + j);
            }
        }
        if (symbolCount == 0) throw Damaged("no symbols");
        int alphabet = symbolCount + 2;
        int endOfBlock = symbolCount + 1;

        int groups = (int)Bits(3);
        if (groups < 2 || groups > 6) throw Damaged("bad table count");
        int selectorCount = (int)Bits(15);
        if (selectorCount < 1) throw Damaged("no selectors");
        byte[] selectors = new byte[selectorCount];
        byte[] order = new byte[] { 0, 1, 2, 3, 4, 5 };
        for (int i = 0; i < selectorCount; i++) {
            int j = 0;
            while (Bits(1) == 1) {
                if (++j >= groups) throw Damaged("bad selector");
            }
            byte chosen = order[j];
            Array.Copy(order, 0, order, 1, j);
            order[0] = chosen;
            selectors[i] = chosen;
        }

        int[][] limit = new int[groups][], bases = new int[groups][], perm = new int[groups][];
        int[] minimum = new int[groups];
        int[] lengths = new int[alphabet];
        for (int t = 0; t < groups; t++) {
            int current = (int)Bits(5);
            int low = 32, high = 0;
            for (int i = 0; i < alphabet; i++) {
                while (true) {
                    if (current < 1 || current > 20) throw Damaged("bad code length");
                    if (Bits(1) == 0) break;
                    current += Bits(1) == 0 ? 1 : -1;
                }
                lengths[i] = current;
                if (current < low) low = current;
                if (current > high) high = current;
            }
            limit[t] = new int[23];
            bases[t] = new int[23];
            perm[t] = new int[258];
            minimum[t] = low;
            int p = 0;
            for (int i = low; i <= high; i++) {
                for (int j = 0; j < alphabet; j++) {
                    if (lengths[j] == i) perm[t][p++] = j;
                }
            }
            for (int i = 0; i < alphabet; i++) bases[t][lengths[i] + 1]++;
            for (int i = 1; i < 23; i++) bases[t][i] += bases[t][i - 1];
            int code = 0;
            for (int i = low; i <= high; i++) {
                code += bases[t][i + 1] - bases[t][i];
                limit[t][i] = code - 1;
                code <<= 1;
            }
            for (int i = low + 1; i <= high; i++) bases[t][i] = ((limit[t][i - 1] + 1) << 1) - bases[t][i];
        }

        byte[] moved = new byte[256];
        for (int i = 0; i < 256; i++) moved[i] = (byte)i;
        int[] counts = new int[256];
        int count = 0, runLength = 0, runBit = 0, selector = -1, left = 0, group = 0;
        while (true) {
            if (left == 0) {
                if (++selector >= selectorCount) throw Damaged("too few selectors");
                left = 50;
                group = selectors[selector];
            }
            left--;
            int size = minimum[group];
            int value = (int)Bits(size);
            int[] groupLimit = limit[group];
            while (true) {
                if (size > 20) throw Damaged("bad code");
                if (value <= groupLimit[size]) break;
                size++;
                value = (value << 1) | (int)Bits(1);
            }
            int index = value - bases[group][size];
            if (index < 0 || index >= 258) throw Damaged("bad code");
            int symbol = perm[group][index];

            if (symbol <= 1) {
                if (runBit > 20) throw Damaged("run too long");
                runLength += (symbol + 1) << runBit;
                runBit++;
                continue;
            }
            if (runLength > 0) {
                if (runLength > tt.Length - count) throw Damaged("block too long");
                byte repeated = symbols[moved[0]];
                counts[repeated] += runLength;
                for (int i = 0; i < runLength; i++) tt[count++] = repeated;
                runLength = 0;
                runBit = 0;
            }
            if (symbol == endOfBlock) break;

            if (count >= tt.Length) throw Damaged("block too long");
            int place = symbol - 1;
            byte front = moved[place];
            Array.Copy(moved, 0, moved, 1, place);
            moved[0] = front;
            byte literal = symbols[front];
            counts[literal]++;
            tt[count++] = literal;
        }
        if (origin >= count) throw Damaged("bad block origin");

        int total = 0;
        for (int i = 0; i < 256; i++) {
            int next = counts[i];
            counts[i] = total;
            total += next;
        }
        for (int i = 0; i < count; i++) {
            byte b = (byte)tt[i];
            tt[counts[b]++] |= (uint)i << 8;
        }

        position = tt[origin] >> 8;
        blockLength = count;
        used = 0;
        run = 0;
        repeat = 0;
        crc = 0xffffffff;
        return true;
    }

    public int Read(byte[] target, int offset, int length) {
        int written = 0;
        while (written < length) {
            if (repeat > 0) {
                target[offset + written++] = previous;
                crc = (crc << 8) ^ CrcTable[(crc >> 24) ^ previous];
                repeat--;
                continue;
            }
            if (!inBlock || used == blockLength) {
                if (finished) break;
                if (inBlock && ~crc != expectedCrc) throw Damaged("wrong checksum");
                inBlock = ReadBlock();
                if (!inBlock) {
                    finished = true;
                    break;
                }
                continue;
            }
            uint entry = tt[position];
            position = entry >> 8;
            byte b = (byte)entry;
            used++;
            if (run == 4) {
                repeat = b;
                run = 0;
                continue;
            }
            if (run > 0 && b == previous) {
                run++;
            }
            else {
                run = 1;
                previous = b;
            }
            target[offset + written++] = b;
            crc = (crc << 8) ^ CrcTable[(crc >> 24) ^ b];
        }
        return written;
    }
}

public static class EyeFinderCefUnpacker {
    private static bool Fill(EyeFinderBz2Reader reader, byte[] buffer, int length) {
        int have = 0;
        while (have < length) {
            int read = reader.Read(buffer, have, length - have);
            if (read == 0) {
                if (have == 0) return false;
                throw new InvalidDataException("The archive is damaged (it ends inside a file).");
            }
            have += read;
        }
        return true;
    }

    private static string Text(byte[] buffer, int offset, int length) {
        int end = offset;
        while (end < offset + length && buffer[end] != 0) end++;
        return Encoding.UTF8.GetString(buffer, offset, end - offset);
    }

    private static long Size(byte[] header) {
        string octal = Text(header, 124, 12).Trim();
        if (octal.Length == 0) return 0;
        try { return Convert.ToInt64(octal, 8); }
        catch (FormatException) { throw new InvalidDataException("The archive is damaged (bad file size)."); }
    }

    private static string PaxPath(byte[] data) {
        int at = 0;
        while (at < data.Length) {
            int space = Array.IndexOf(data, (byte)' ', at);
            if (space < 0) break;
            int length;
            if (!int.TryParse(Encoding.ASCII.GetString(data, at, space - at), out length) || length <= 0) break;
            string entry = Encoding.UTF8.GetString(data, space + 1, at + length - space - 2);
            if (entry.StartsWith("path=", StringComparison.Ordinal)) return entry.Substring(5);
            at += length;
        }
        return null;
    }

    // Unpacks only what the mod needs, without the long top folder. Returns the number of files.
    public static int Extract(string archive, string target, Action<int> progress) {
        int files = 0;
        byte[] header = new byte[512];
        byte[] buffer = new byte[256 * 1024];
        string longName = null;
        using (FileStream input = new FileStream(archive, FileMode.Open, FileAccess.Read, FileShare.Read, 1024 * 1024)) {
            EyeFinderBz2Reader reader = new EyeFinderBz2Reader(input);
            while (Fill(reader, header, 512)) {
                if (Array.TrueForAll(header, delegate(byte b) { return b == 0; })) break;

                char type = (char)header[156];
                long size = Size(header);
                long padded = (size + 511) & ~511L;

                if (type == 'x' || type == 'L') {
                    if (padded > 1024 * 1024) throw new InvalidDataException("The archive is damaged (long name).");
                    byte[] data = new byte[padded];
                    if (padded > 0 && !Fill(reader, data, (int)padded)) throw new InvalidDataException("The archive is damaged (it ends too early).");
                    byte[] exact = new byte[size];
                    Array.Copy(data, exact, size);
                    string found = type == 'x' ? PaxPath(exact) : Text(exact, 0, exact.Length);
                    if (found != null) longName = found;
                    continue;
                }

                string name = longName;
                longName = null;
                if (name == null) {
                    name = Text(header, 0, 100);
                    string prefix = Text(header, 345, 155);
                    if (Text(header, 257, 5) == "ustar" && prefix.Length > 0) name = prefix + "/" + name;
                }

                string relative = null;
                if (type == '0' || type == '\0' || type == '7') {
                    int slash = name.IndexOf('/');
                    if (slash > 0) {
                        string rest = name.Substring(slash + 1);
                        if (rest.StartsWith("Release/", StringComparison.Ordinal) || rest.StartsWith("Resources/", StringComparison.Ordinal) || rest == "CREDITS.html") relative = rest;
                    }
                }
                if (relative != null && (relative.Contains("..") || relative.Contains(":") || relative.Contains("\\"))) {
                    throw new InvalidDataException("The archive has an unsafe file name: " + relative);
                }

                FileStream output = null;
                try {
                    if (relative != null) {
                        string path = Path.Combine(target, relative.Replace('/', '\\'));
                        try {
                            Directory.CreateDirectory(Path.GetDirectoryName(path));
                            output = new FileStream(path, FileMode.Create, FileAccess.Write, FileShare.None, 1024 * 1024);
                        }
                        catch (Exception e) {
                            if (e is IOException || e is UnauthorizedAccessException || e is NotSupportedException) throw new IOException("Couldn't create " + relative + ": " + e.Message, e);
                            throw;
                        }
                    }
                    long remaining = padded;
                    long content = size;
                    while (remaining > 0) {
                        int part = (int)Math.Min(remaining, buffer.Length);
                        if (!Fill(reader, buffer, part)) throw new InvalidDataException("The archive is damaged (it ends inside a file).");
                        if (output != null && content > 0) {
                            try { output.Write(buffer, 0, (int)Math.Min(content, part)); }
                            catch (IOException e) { throw new IOException("Couldn't write " + relative + ": " + e.Message, e); }
                        }
                        content -= part;
                        remaining -= part;
                    }
                }
                finally {
                    if (output != null) output.Dispose();
                }
                if (relative != null) {
                    files++;
                    progress(files);
                }
            }
        }
        return files;
    }
}
'@

function Expand-Cef {
    try { Add-Type -TypeDefinition $unpackerSource -Language CSharp -ErrorAction Stop }
    catch { throw "Windows wouldn't let this prepare its unpacker. Follow browser\INSTALL_CEF.txt to install CEF by hand. ($($_.Exception.Message))" }

    $spinner = '|/-\'
    $progress = [Action[int]] {
        param($count)
        Show-Status ('  {0} Unpacked {1} of about {2} files' -f $spinner[$count % 4], $count, $fileCount)
    }
    try { $count = [EyeFinderCefUnpacker]::Extract($archive, $work, $progress) }
    catch {
        $reason = if ($_.Exception.InnerException) { $_.Exception.InnerException.Message } else { $_.Exception.Message }
        Write-Host ''
        throw "Unpacking failed: $reason Your antivirus may be blocking it. If this keeps happening, follow browser\INSTALL_CEF.txt to install CEF by hand."
    }
    Show-Status ('  Unpacked {0} files' -f $count)
    Write-Host ''
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
