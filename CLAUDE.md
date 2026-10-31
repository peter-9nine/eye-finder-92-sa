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
$url = "file:///$root/helper/build/Release/ui/shell.html" -replace '\\','/'
tools\harness\build\Release\EyeFinderHarness.exe "$root\helper\build\Release\EyeFinderBrowser.exe" $url <output folder>
```

The harness drives the desktop, tabs, toolbar, dropdowns and Log Off, measures performance and writes raw BGRX captures at 1280x720. Its click coordinates and expected pixel colors match `ui/shell.html` and `sites/www.eyefinder.info/tests.html`, so update them if those layouts change. A passing run doesn't prove everything looks right, so inspect the captures. It also tests the mail files, the pager and the API, using a stand-in module named `EyeFinder92SA.asi`. It can't test anything game-side, such as the pause menu, fades, the save hooks or the help box. `tools\deploy.ps1 -Demo` installs `examples/ApiDemo` for testing the API in-game.

## Architecture

There are two processes, built separately:

- **Plugin (`EyeFinder92SA.asi`, 32-bit):** MSBuild, `EyeFinder92SA.sln`, configurations `Release|GTASA` and `Debug|GTASA`.
- **Helper (`EyeFinderBrowser.exe`, 64-bit):** CMake, `helper/`, Release only because the minimal CEF distribution has no Debug libraries.

They communicate only through `shared/ipc.h`, a shared memory section holding a lock-free triple frame buffer and an input event ring. The layout must stay identical in 32 and 64 bits, so use only fixed-size types there. Bump `kProtocol` when it changes. Neither side ever blocks the other: the ring drops events when full, and the game keeps its last frame if the helper stalls.

The helper runs several windowless Chromium views: one full-screen view for the interface (`ui/shell.html`: the desktop and browser window) and one per tab. The shell reports the rectangle reserved for page content, and the helper composites the active tab into it, so the plugin only ever sees one frame. Tabs have their own views so that real sites work, because many refuse to load in an iframe. Mouse input goes to whichever view is under the cursor, and keyboard input to the last one clicked.

The shell gives orders through `window.cefQuery`, handled in `ShellQueryHandler`, and the helper calls back through `window.ef`. Only the shell may use this channel: `cefQuery` is injected only into `file:` pages, the handler checks the browser and URL, the shell can't navigate away, and tabs can't open `file:` URLs. Keep all of that when adding commands.

`sites/<domain>/` is served as that domain over `http` and `https` by `SiteHandlerFactory`, so adding a site means adding a folder. Sites are registered at helper startup.

Plugin code is split so that the harness can exercise it without the game:

- **Game-free, also compiled into the harness:** `BrowserClient` (process, job object, shared memory) and `OverlayRenderer` (Direct3D 9 upload and drawing).
- **Game-specific:** `BrowserOverlay` (open and close, input, pause menu, player controls, stats) and `Main.cpp` (plugin-sdk events, the cursor hook).

Non-obvious behavior:

- **Input:** the mouse is polled with `GetCursorPos` and `GetAsyncKeyState`, and the wheel comes from `CPad`, because the game reads the mouse through DirectInput. Keyboard input comes from a window procedure subclass. `WM_KEYUP` also reaches the game so that keys don't stick.
- **Cursor recentering:** the hook at `0x007453F0` stops the game moving the cursor while the browser is open.
- **Pause menu:** the game opens it when `CPad::NewKeyState.esc` goes down, so Escape must reach the game. While `m_bMenuActive` is set, the plugin passes all input through, stops drawing and mutes pages. On return it restarts the helper if the resolution changed.
- **Opening and closing:** F8, or TAB at the cyber cafe position. The game fades to black, the browser appears, and the browser itself is darkened for its own fade because it's drawn after the game's fade.
- **Focus:** navigating can replace Chromium's internal widget, which then has no focus and silently refuses to open dropdowns, so the helper reasserts focus on every click.
- **Render state:** `OverlayRenderer::Draw` wraps everything in a state block, because RenderWare caches device state and must not see it change.
- **Dropdowns:** Chromium paints them as a separate `PET_POPUP` element, and the helper composites it onto the frame.
- **Device loss:** the texture is in `D3DPOOL_DEFAULT`, so `OnDeviceLost` releases it and `OnDeviceReset` requests a new frame.
- **Resolution changes:** the helper restarts if the game's size changed, which loses the open tabs.
- **DXVK:** the developer's game runs through DXVK, so test Direct3D changes through it as well as native Direct3D.

## Email and save data

- **Public API:** `api/EyeFinderApi.h` documents it, and `source/Api.cpp` exports it from the plugin. It's a plain C interface that other mods depend on, so only add to it: never change or remove a function. The real exports must stay undecorated (check with `dumpbin /exports`).
- **Threads:** API calls can come from any thread, so they only copy into a queue. `MailSystem::Update` drains it on the game thread.
- **Ownership:** the plugin owns the inbox (`MailStore`) and writes `save data\current session.json` atomically. The helper only reads that file. Player actions travel back as `ef::MailOp` through a second shared memory ring, and the unread count as `EventType::MailChanged`.
- **Mail site:** `sites/mail.eyefinder.info` reads `/api/inbox` and posts to `/api/read/<id>` and `/api/delete/<id>`. `SiteHandlerFactory` serves these only for that domain and requires an `X-EyeFinder` header, so that real sites can't reach them: a custom header forces a CORS preflight that's never granted. Mail text is always inserted as text, never HTML, because it comes from other mods.
- **Save files:** hooks on `GenericSave` (`0x5D13E0`) and `GenericLoad` (`0x5D17B0`) copy between the session and `save data\<game save name>.json`, using `ms_SaveFileNameJustSaved` and `ms_LoadFileName`. `restartGameEvent` fires before `GenericLoad`, so it resets the inbox for both new games and loads, and a successful load then replaces it. Future mod data belongs in the same file, under its own top-level key.
- **Replies:** `EF_SendEmailWithReplies` takes a key, two answer texts and optional CLEO scripts. `MailStore` keeps a key-to-answer map that survives deleting the email, and each key is delivered once per saved game. `Api.cpp` holds a copy for `EF_GetReply`, because it can be called from any thread.
- **CLEO:** `MailSystem::RunCleoScript` looks up `_CLEO_CreateCustomScript@12` at run time and passes a null calling script. Never pass one of the game's own scripts, because CLEO reads parameters from the caller's code. Only call it from `Update`, outside the game's script loop.
- **Site game API:** every local site gets `/_game/sa.js` and `POST /_game/script/<name>`, which runs `sites/<domain>/scripts/<name>.efs`. Those are compiled CLEO scripts with a different extension, because modloader starts every `.cs` file in a mod folder at game start. It's limited to simple names in the site's own folder and needs the `X-EyeFinder` header. Real sites can't reach it, and `tests.html` checks this with a real cross-origin request.
- **Notifications:** `PagerRenderer` is game-free and drawn after the browser, so it shows over it. The help box text goes through `CHud::SetHelpMessage`, which silently refuses at times, so `MailSystem` retries until the text appears in `CHud::m_pHelpMessage`. Don't use `HelpMessageDisplayed()` to confirm it, because it stays false until the box is drawn, which once made the message repeat forever.

## Local mode

The browser starts in local mode, and the real internet is only enabled by the shell's `setInternet` command, after its confirmation dialog. Keep the dialog on every switch. There are two layers. `IsAllowedUrl` in the helper applies to navigation, popups and every resource through `OnBeforeResourceLoad`, and non-network schemes such as `data:` and `blob:` pass. `ApplyNetworkPolicy` also points Chromium's proxy preference at a dead address, because WebSockets skip `OnBeforeResourceLoad`, and WebRTC is limited to proxied traffic. Local sites use scheme handlers, not the network, so they keep working. The dialog hides the active tab through the layout message, because tabs are composited over the shell. `tests.html` reports whether its own fetch, WebSocket and WebRTC reach the internet, so check it in both modes after changing this.

## Before this ships with real internet access

- Chromium's sandbox is off (`USE_SANDBOX OFF`, `no_sandbox = true`).
- CEF must be updated with each release (`tools\fetch-cef.ps1` pins the version and checksum).
- Real sites must never get access to game features such as money.

## Conventions

- **File encoding:** sources are UTF-8 without BOM, and comments are in Spanish. `.sln`, `.vcxproj` and `.filters` files are UTF-8 with BOM and CRLF.
- **Hooks:** use `safetyhook::create_inline` with absolute 1.0 US addresses.
- **Plugin target:** 32-bit, C++latest, static CRT. Include project headers with quotes, and don't name one `types.h`, because the SDK's would shadow it.
- **`$(PLUGIN_SDK_DIR)`:** it's shared by every plugin in `generated/`, so changes there affect them all.
