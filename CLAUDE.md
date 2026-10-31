# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

EyeFinder92SA is a GTA San Andreas (1.0 US) ASI plugin that shows a real web browser over the game, using CEF in a separate process. It replaces the image-based `generated/EyeFinder92` project, which stays as a reference. It's currently a prototype: the goal is to prove the approach before building in-universe sites and game features on it.

This directory is its own git repository (remote: `peter-9nine/eye-finder-92-sa`), separate from the parent plugin-sdk repo, which ignores `generated/`.

## Commands

Run from this directory in PowerShell:

```powershell
tools\build.ps1     # fetches CEF if needed, builds helper, harness and plugin
tools\deploy.ps1    # copies to %GTA_SA_DIR%\modloader\EyeFinder92SA; refuses if the game is running
```

Test without the game, using the harness (there's no other test or lint setup):

```powershell
$root = (Get-Location).Path
$url = "file:///$root/helper/build/Release/ui/start.html" -replace '\\','/'
tools\harness\build\Release\EyeFinderHarness.exe "$root\helper\build\Release\EyeFinderBrowser.exe" $url 1280 720 <output folder>
```

Append `?animate` to the URL to measure a page that repaints constantly. The harness's click coordinates match `ui/start.html`'s layout at 1280x720, so update them if that layout changes. It writes raw BGRX captures, and a passing run doesn't prove the page looks right, so inspect them.

## Architecture

There are two processes, built separately:

- **Plugin (`EyeFinder92SA.asi`, 32-bit):** MSBuild, `EyeFinder92SA.sln`, configurations `Release|GTASA` and `Debug|GTASA`.
- **Helper (`EyeFinderBrowser.exe`, 64-bit):** CMake, `helper/`, Release only because the minimal CEF distribution has no Debug libraries.

They communicate only through `shared/ipc.h`, a shared memory section holding a lock-free triple frame buffer and an input event ring. The layout must stay identical in 32 and 64 bits, so use only fixed-size types there. Bump `kProtocol` when it changes. Neither side ever blocks the other: the ring drops events when full, and the game keeps its last frame if the helper stalls.

Plugin code is split so that the harness can exercise it without the game:

- **Game-free, also compiled into the harness:** `BrowserClient` (process, job object, shared memory) and `OverlayRenderer` (Direct3D 9 upload and drawing).
- **Game-specific:** `BrowserOverlay` (open and close, input, player controls, stats) and `Main.cpp` (plugin-sdk events, the cursor hook).

Non-obvious behavior:

- **Input:** the mouse is polled with `GetCursorPos` and `GetAsyncKeyState`, and the wheel comes from `CPad`, because the game reads the mouse through DirectInput. Keyboard input comes from a window procedure subclass. `WM_KEYUP` also reaches the game so that keys don't stick.
- **Cursor recentering:** the hook at `0x007453F0` stops the game moving the cursor while the browser is open.
- **Render state:** `OverlayRenderer::Draw` wraps everything in a state block, because RenderWare caches device state and must not see it change.
- **Dropdowns:** Chromium paints them as a separate `PET_POPUP` element, and the helper composites it onto the frame.
- **Device loss:** the texture is in `D3DPOOL_DEFAULT`, so `OnDeviceLost` releases it and `OnDeviceReset` requests a new frame.
- **Resolution changes:** the helper restarts on the next open if the game's size changed, which reloads the page.
- **DXVK:** the developer's game runs through DXVK, so test Direct3D changes through it as well as native Direct3D.

## Before this ships with real internet access

- Chromium's sandbox is off (`USE_SANDBOX OFF`, `no_sandbox = true`).
- CEF must be updated with each release (`tools\fetch-cef.ps1` pins the version and checksum).
- Real sites must never get access to game features such as money.

## Conventions

- **File encoding:** sources are UTF-8 without BOM, and comments are in Spanish. `.sln`, `.vcxproj` and `.filters` files are UTF-8 with BOM and CRLF.
- **Hooks:** use `safetyhook::create_inline` with absolute 1.0 US addresses.
- **Plugin target:** 32-bit, C++latest, static CRT. Include project headers with quotes, and don't name one `types.h`, because the SDK's would shadow it.
- **`$(PLUGIN_SDK_DIR)`:** it's shared by every plugin in `generated/`, so changes there affect them all.
