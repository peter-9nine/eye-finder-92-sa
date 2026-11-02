# Releasing

## What ships

CEF is too big to ship, so the release doesn't include it. Players double-click `Install CEF.bat` in the mod folder, which downloads it into the `browser` folder.

```powershell
tools\build.ps1
tools\package.ps1   # creates dist\EyeFinder92SA and dist\EyeFinder92SA.zip
```

`package.ps1` includes the plugin, `locations.json`, the license and `licenses`, `EyeFinderBrowser.exe`, `system`, these docs, and the public sites and ads. It fails if any CEF files reach the package.

`INSTALL_CEF.txt`, the instructions for doing it by hand, is generated from `tools\INSTALL_CEF.txt`, with the download link taken from `tools\fetch-cef.ps1`, so the instructions can't disagree with the build.

## The installer

`tools\Install CEF.bat` starts `tools\install-cef.ps1`, because double-clicking a `.ps1` file only opens it in Notepad. `tools\make-installer.ps1` fills in the installer's version, checksum and sizes, measured from `third_party\cef`, so they can't drift either. Both `package.ps1` and `deploy.ps1` use it. To try the whole thing on your own game, delete `modloader\EyeFinder92SA\browser`, build, and run it there.

It explains itself and asks first, showing the real download size from the server and the free space on the disk. It then downloads with progress, checks the SHA-1, unpacks only `Release`, `Resources` and `CREDITS.html`, and moves the files into `browser`, leaving out `.lib` and `.pdb` files. It always removes its temporary folder, `browser\_cef_install`.

It unpacks with its own C# code, compiled by `Add-Type`, and not with Windows' `tar.exe`. Many Windows 10 builds have a `tar.exe` without bzip2 support, which fails with code 1 unless a separate `bzip2` program is installed, so a developer's newer Windows won't show the problem. PowerShell 5.1 only compiles C# 5, and reads the script as ANSI, so keep that code to old syntax and the file to ASCII.

As a last check it runs `EyeFinderBrowser.exe` with no arguments. The helper exits with code 2 if it could load CEF, which proves that the files really work. Keep that exit code if you change how the helper starts.

To test it without answering the question, run it with `-Yes`.

## How the plugin checks for CEF

`BrowserRuntimeProblem` runs before the helper starts. It checks for CEF's files, and that `libcef.dll` is the exact version in `kCefVersion`, because the helper only runs with the build it was compiled against. If something's wrong, the log says what, and the player is told to read `INSTALL_CEF.txt`. It runs each time the browser opens, so adding CEF doesn't need a game restart.

## Updating CEF

CEF must be updated with each release that has real internet access, because Chromium's security fixes come through it.

1. Change the name and SHA-1 in `tools\fetch-cef.ps1`.
2. Change `kCefVersion` in `shared/ipc.h` to match. The harness fails if the two disagree.
3. Delete `third_party\cef`, run `tools\build.ps1`, and fix anything that no longer compiles.
4. Run the harness, and check the captures.
5. Tell players in the release notes, because they must download CEF again.

## Release checklist

- [ ] The harness passes, and you've looked at its captures.
- [ ] The [in-game checklist](testing.md#in-game-checklist) passes, on native Direct3D and DXVK.
- [ ] **F8 is off.** With the default `Browser=F8`, players can open any session anywhere, including police and dark web. `package.ps1` doesn't include an ini, so add one with `Browser=None`.
- [ ] **`locations.json` has real positions.** See [Known issues](known-issues.md).
- [ ] Remove or decide on the test pages: `tests.html`, `keyboard-tests.html` and `search-tests.html` ship with `www.eyefinder.info`.
- [ ] Try the package on a clean game: unzip it, confirm the "needs CEF" message appears, then run `Install CEF.bat`.
- [ ] `licenses` covers every library in the binaries, and the CEF and plugin-sdk versions haven't changed their terms.
- [ ] Decide about `include/GInputAPI.h`. See [Known issues](known-issues.md).
- [ ] `api/FAQ.md` and `api/EyeFinderApi.h` match what the release does.
- [ ] If the API changed, `EF_API_VERSION` was raised, and nothing was removed or changed.

## Before shipping with real internet access

The real internet is behind a confirmation today, but it's still reachable, so these matter:

- **Chromium's sandbox is off** (`USE_SANDBOX OFF` in `helper/CMakeLists.txt`, and `no_sandbox = true` in `helper/main.cpp`). Turn it on.
- **CEF must stay current,** as above.
- **Real sites must never get access to game features such as money.** See the header check in [Mail, saves and the API](data-and-api.md#game-api-for-sites).
