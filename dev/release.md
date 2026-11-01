# Releasing

## What ships

CEF is too big to ship, so the release doesn't include it. Players download it into the `browser` folder themselves.

```powershell
tools\build.ps1
tools\package.ps1   # creates dist\EyeFinder92SA and dist\EyeFinder92SA.zip
```

`package.ps1` includes the plugin, `locations.json`, `EyeFinderBrowser.exe`, `system`, these docs, and the public sites and ads. It fails if any CEF files reach the package.

`INSTALL_CEF.txt` is generated from `tools\INSTALL_CEF.txt`, with the download link taken from `tools\fetch-cef.ps1`, so the instructions can't disagree with the build.

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
- [ ] Try the package on a clean game: unzip it, confirm the "needs CEF" message appears, then follow `INSTALL_CEF.txt`.
- [ ] `api/FAQ.md` and `api/EyeFinderApi.h` match what the release does.
- [ ] If the API changed, `EF_API_VERSION` was raised, and nothing was removed or changed.

## Before shipping with real internet access

The real internet is behind a confirmation today, but it's still reachable, so these matter:

- **Chromium's sandbox is off** (`USE_SANDBOX OFF` in `helper/CMakeLists.txt`, and `no_sandbox = true` in `helper/main.cpp`). Turn it on.
- **CEF must stay current,** as above.
- **Real sites must never get access to game features such as money.** See the header check in [Mail, saves and the API](data-and-api.md#game-api-for-sites).
