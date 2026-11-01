# Getting started

## What you need

- **Windows**, with **Visual Studio 2022** and its "Desktop development with C++" tools, including the Windows SDK. Any edition works.
- **CMake** 3.21 or newer, on your `PATH`.
- **Git.**
- **[plugin-sdk](https://github.com/DK22Pac/plugin-sdk)**, built as below.
- **GTA San Andreas 1.0 US** with modloader, for testing in-game.

## Setting up from nothing

Use short folder paths. CMake's compiler check fails with a confusing error in a deep folder, because of Windows' 260-character path limit.

**1. Build plugin-sdk.** This only needs doing once, and it takes a few minutes:

```powershell
git clone https://github.com/DK22Pac/plugin-sdk.git D:\plugin-sdk
setx PLUGIN_SDK_DIR D:\plugin-sdk
$env:PLUGIN_SDK_DIR = 'D:\plugin-sdk'

cd D:\plugin-sdk\tools\premake
.\premake5.exe vs2022
cd D:\plugin-sdk

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$msbuild = & $vswhere -latest -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
& $msbuild plugin.sln /m /t:Plugin_SA /p:Configuration=Release "/p:Platform=Mixed Platforms"
```

That creates `output\lib\plugin.lib`. For a Debug plugin, also build with `/p:Configuration=Debug`.

**2. Clone and build the mod:**

```powershell
git clone https://github.com/peter-9nine/eye-finder-92-sa.git D:\dev\EyeFinder92SA
cd D:\dev\EyeFinder92SA
tools\build.ps1
```

If PowerShell refuses to run scripts, use `powershell -ExecutionPolicy Bypass -File tools\build.ps1`.

**3. To test in-game,** point `GTA_SA_DIR` at the game, then deploy:

```powershell
setx GTA_SA_DIR "D:\GAMES\GTA San Andreas"
$env:GTA_SA_DIR = 'D:\GAMES\GTA San Andreas'
tools\deploy.ps1
```

Then double-click `Install CEF.bat` in `modloader\EyeFinder92SA`, which is only needed once.

`setx` only reaches new terminals and Visual Studio windows, which is why these also set the variable for the current one. Everything else the build needs is either in the repository, such as `vendor/nlohmann`, or downloaded by it.

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

**Building the plugin also copies the mod to the game,** in Visual Studio or with `build.ps1`, even when nothing needed compiling. If the game is open or `GTA_SA_DIR` isn't set, it says so and skips the copy, without failing the build. `deploy.ps1` does the same copy by itself, and refuses instead.

It lays the folder out exactly as a player gets it, so deleting `modloader\EyeFinder92SA\browser` and building gives you a clean install. CEF isn't copied: double-click `Install CEF.bat` in that folder once, and it stays there through later builds.

Visual Studio only builds the plugin. Pages in `ui` and `sites` reach the game with any build, but after changing the helper, run `tools\build.ps1`.

`build.ps1` always builds the helper in Release, because the minimal CEF distribution has no Debug libraries. To build a Debug plugin, use `tools\build.ps1 -Configuration Debug`.

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
