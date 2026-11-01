# Getting started

## What you need

- **Windows**, with **Visual Studio 2022 Community** and its C++ tools. `tools\build.ps1` has the Community edition's MSBuild path written in it, so change that line if you use another edition.
- **CMake** 3.21 or newer, on your `PATH`.
- **plugin-sdk, already built**, so that `output\lib\plugin.lib` exists. Set `PLUGIN_SDK_DIR` to its folder.
- Two things in that plugin-sdk folder that a stock copy may not have:
  - `safetyhook\safetyhook.hpp`, which `include\pch.h` includes.
  - `tools\myplugin-gen\generated\shared`, which holds `nlohmann/json.hpp`.
- **GTA San Andreas 1.0 US** with modloader. Set `GTA_SA_DIR` to its folder, for deploying.

Optional, for testing in-game:

- **GInput**, or the controller does nothing.
- **CLEO**, tested with 5.4.0, or script requests are logged and ignored.
- **DXVK**, because Direct3D changes should be tested through it as well as native Direct3D.

The first build downloads CEF to `third_party\cef` and checks its SHA-1, so it takes a while and needs an internet connection.

## Building and deploying

Run these from the project folder, in PowerShell:

```powershell
tools\build.ps1     # fetches CEF if needed, then builds the helper, harness and plugin
tools\deploy.ps1    # copies to %GTA_SA_DIR%\modloader\EyeFinder92SA
```

`deploy.ps1` refuses to run while the game is open. A build alone changes nothing in your game, so always deploy before testing in-game.

`build.ps1` always builds the helper in Release, because the minimal CEF distribution has no Debug libraries. To build a Debug plugin, use `tools\build.ps1 -Configuration Debug`, and then `tools\deploy.ps1 -Configuration Debug`.

## Trying it in-game

Load a game, then use one of these:

| Key | What it does |
|---|---|
| **F8** | Debug: opens anywhere. It uses the computer you're at, or else a normal session at $10 a minute. It also closes the browser |
| **Shift+F8** | Debug: a police session, anywhere |
| **Ctrl+F8** | Debug: a dark web session, anywhere |
| **TAB** or **L1** | What players use, at a computer listed in `locations.json` |

F8 is `Browser=` in `EyeFinder92SA.ini`. A plain F8 session charges real in-game money, so you can test paying.

`tools\deploy.ps1 -Demo` also installs `examples/ApiDemo`, which adds F9 (email with answers), F10 (email on the pager), F11 (pager message) and Shift+F11 (help box text). Delete `EyeFinderApiDemo.asi` to remove it, because a later deploy doesn't.

## Where to look when something's wrong

| What | Where |
|---|---|
| Plugin log: startup problems, session receipts, scripts, and performance every 5 seconds while open | `modloader\EyeFinder92SA\EyeFinder92SA.log` |
| Helper and Chromium log | `%LOCALAPPDATA%\EyeFinder92SA\helper.log` |
| Chromium profile and cache | `%LOCALAPPDATA%\EyeFinder92SA\profile` |
| Current game's mail and values | `modloader\EyeFinder92SA\save data\current session.json` |
| CLEO's own log | `cleo.log` in the game folder |

## Editing without rebuilding

The interface and sites are plain files. For a quick change, edit them in `modloader\EyeFinder92SA\system`, and press F5 in the browser for a page, or reopen the game for the shell. Copy your change back to `ui` or `sites` in the project before deploying, because a deploy replaces that folder.

The one exception is a new site folder, which is only detected when the helper starts.

## Next

Read [Architecture](architecture.md), then run the [harness](testing.md) once, so that you can tell later whether you've broken it.
