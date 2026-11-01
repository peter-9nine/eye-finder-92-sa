# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

EyeFinder92SA is a GTA San Andreas (1.0 US) ASI plugin that shows a real web browser over the game, using CEF in a separate process. It replaces the image-based `generated/EyeFinder92` project, which stays as a reference. It's currently a prototype: the goal is to prove the approach before building in-universe sites and game features on it.

This directory is its own git repository (remote: `peter-9nine/eye-finder-92-sa`), separate from the parent plugin-sdk repo, which ignores `generated/`.

`dev/` holds the developer docs. Keep them current when behavior they describe changes, and keep `dev/known-issues.md` accurate. `tools\raw2png.ps1 <folder>` turns harness captures into PNG files.

## Commands

Run from this directory in PowerShell:

```powershell
tools\build.ps1     # fetches CEF if needed, builds helper, harness and plugin
tools\deploy.ps1    # copies to %GTA_SA_DIR%\modloader\EyeFinder92SA; refuses if the game is running
tools\package.ps1   # builds dist\EyeFinder92SA.zip, the release: the whole mod without CEF
```

Test without the game, using the harness (there's no other test or lint setup):

```powershell
$root = (Get-Location).Path
$url = "file:///$root/helper/build/system/ui/shell.html" -replace '\\','/'
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

`SiteHandlerFactory` serves each site folder as that domain over `http` and `https`, so adding a site means adding a folder. Sites are registered at helper startup. There are two kinds. The mod's own are in `sites/` here and `system\sites` when deployed, and `deploy.ps1` mirrors them. Public sites and ads are in `public/` here and next to the `.asi` when deployed, and `deploy.ps1` only adds to them, because they belong to the user. On a name clash the mod's site wins, and only the mod's `mail.eyefinder.info` gets the mail API.

Plugin code is split so that the harness can exercise it without the game:

- **Game-free, also compiled into the harness:** `BrowserClient` (process, job object, shared memory) and `OverlayRenderer` (Direct3D 9 upload and drawing).
- **Game-specific:** `BrowserOverlay` (open and close, input, pause menu, player controls, stats) and `Main.cpp` (plugin-sdk events, the cursor hook).

Non-obvious behavior:

- **Input:** the mouse is polled with `GetCursorPos` and `GetAsyncKeyState`, and the wheel comes from `CPad`, because the game reads the mouse through DirectInput. Keyboard input comes from a window procedure subclass. `WM_KEYUP` also reaches the game so that keys don't stick.
- **Controller:** `PadInput` needs GInput, and without it there's no controller support. `HasPadInHands()` from the GInput API (`include/GInputAPI.h`) says whether the pad is in use, and the buttons and sticks are read from `CPad::GetPad(0)->NewState`. The check matters because the mouse also feeds into `NewState`, so a click could otherwise look like a button. In the browser: left stick moves the real Windows cursor (`UpdateMouse` then treats it as the mouse), right stick scrolls, A clicks, B and Y go back and forward, X reloads, LB and RB change tabs, and D-pad up and down are arrow keys. These reuse the keyboard shortcuts in `HandleShortcut`. Buttons held when the browser appears or the pause menu closes are ignored until released, or the A used to sprint or leave the menu would click. Holding D-pad left opens or closes the pager, and left and right browse it, so the browser leaves those two alone. L1 opens the browser at a computer, and the pad closes it by clicking Log Off.
- **On-screen keyboard:** it opens only after a pad click (`MouseDown` with `b = 1`), so mouse users never see it. 60 ms after the click, the helper sends `ef.queryField` to the tab's focused frame, the render process reads the focused element with `CefV8Context::Eval`, and the result reaches `ef.keyboardOpen` in `ui/shell.html`. Clicks on the shell go to `ef.padClicked`, since the address bar is its only field. The text lives in the shell's `#kbText` until Go: `ef.applyField` then writes it with `execCommand('insertText')`, so pages that listen for input notice, and the helper sends a real Enter unless the field is multi-line or its form has another empty field. Close, B or a click outside changes nothing. The tab keeps Chromium's focus so that the page doesn't see a blur, and the shell gets the physical keyboard. The helper composites the keyboard's rectangle over the tab, and `Header::keyboardOpen` hands the pad to it: `BrowserOverlay::UpdatePadKeyboard` sends `EventType::PadKeyboard` instead of moving the cursor, and the pager stops listening. Buttons are ignored until released whenever it opens or closes, or the A on Close would click the page underneath. Start isn't available because it must reach the game, and the triggers aren't used because GInput's controls set may route them elsewhere.
- **Cursor recentering:** the hook at `0x007453F0` stops the game moving the cursor while the browser is open.
- **Pause menu:** the game opens it when `CPad::NewKeyState.esc` goes down, so Escape must reach the game. While `m_bMenuActive` is set, the plugin passes all input through, stops drawing and mutes pages. On return it restarts the helper if the resolution changed.
- **Opening and closing:** players use TAB or L1 at a computer from `locations.json`, and the prompt names whichever they're using. F8 is the debug key: it opens anywhere, with Shift for a police session or Ctrl for a dark web one. Plain F8 uses the computer there, or else a normal session at $10 a minute. It also closes the browser. The game fades to black, the browser appears, and the browser itself is darkened for its own fade because it's drawn after the game's fade.
- **Focus:** navigating can replace Chromium's internal widget, which then has no focus and silently refuses to open dropdowns, so the helper reasserts focus on every click.
- **Render state:** `OverlayRenderer::Draw` wraps everything in a state block, because RenderWare caches device state and must not see it change.
- **Dropdowns:** Chromium paints them as a separate `PET_POPUP` element, and the helper composites it onto the frame.
- **Device loss:** the texture is in `D3DPOOL_DEFAULT`, so `OnDeviceLost` releases it and `OnDeviceReset` requests a new frame.
- **Resolution changes:** the helper restarts if the game's size changed, which loses the open tabs.
- **DXVK:** the developer's game runs through DXVK, so test Direct3D changes through it as well as native Direct3D.

## Folder layout and CEF

CEF is too big to ship, so players download it into the `browser` folder themselves. Only `EyeFinderBrowser.exe` and CEF belong there, and everything else lives beside it:

```
modloader\EyeFinder92SA\
  EyeFinder92SA.asi, EyeFinder92SA.ini, locations.json, save data\
  browser\   EyeFinderBrowser.exe, INSTALL_CEF.txt, and CEF, added by the player
  system\    ui\ and sites\: the mod's, replaced on update
  sites\, ads\   public: anyone can add to them
```

`helper/build` has the same layout, with `Release` as its `browser`, and the helper finds everything from its own parent folder. `deploy.ps1` mirrors `browser`, `system` and `dev`, so don't keep your own files in them, and the release package includes `dev` too. It also never overwrites an edited `locations.json`, and only warns.

`BrowserRuntimeProblem` checks for CEF's files and the exact version before starting the helper, and the player is told to read `INSTALL_CEF.txt`. When CEF changes, update `kCefVersion` in `shared/ipc.h` and `tools\fetch-cef.ps1` together: the harness fails if the build's `libcef.dll` doesn't match. `package.ps1` fills the download link in `tools\INSTALL_CEF.txt` from `fetch-cef.ps1`, and fails if CEF files reach the package.

## Sessions

`locations.json` lists the computers, with `name`, `type` (`normal`, `pd` or `dark_web`), `position`, `radius`, and optional `interior` and `pricePerMinute`. `Locations.cpp` loads it, and a bad entry is skipped and logged. With no usable entries, the cyber cafe is used.

- **Plugin:** `BrowserOverlay::CheckForComputers` shows the prompt, and the computer the player is at decides the session. It sends `EventType::Session` on opening and after a helper restart, and `SessionTime` every second.
- **Payment:** only `normal` computers charge. The clock only runs while the browser is really open, not during fades or the pause menu. Every started minute is paid in full, and a player short of money pays what they have. `ChargeSession` runs only on a normal close, so a crash or a game load costs nothing. The receipt goes through `MailSystem`'s help box queue, which retries.
- **Desktop:** `applySession` in `ui/shell.html` sets a `session-<type>` class on the body, which picks the desktop's color, and shows that session's icon. Changing the type closes every tab, because a tab from another computer must not stay open. Icons are in `ui/icons`, and the harness's click points depend on their column.
- **Recycle bin:** a window in `ui/shell.html`, filled from `ui/recycle_bin.js`, which lists files for each session type. Their text is always inserted as text. While it's open the tab is hidden through the layout message, like the internet warning.
- **Hidden folder and search:** sites in `sites\hidden` are registered as dark web only. EyeFinder's page searches public sites with `sa.searchSites`, and `hidden.onion` searches the hidden folder with `sa.searchHidden`, whose endpoint refuses outside a dark web session. Search leaves out `"listed": false` sites, because the FAQ promises they're only reached by typing their address. Real searches only navigate to the engine, so local mode's confirmation still applies. The address bar sends searches to the page of the web that tab is on. See `dev/sessions.md`.
- **Easter egg:** misspelled EyeFinder addresses, from `aliases` in `sites/www.eyefnder.info/site.json`, reach a parked page whose "No" calls `POST /_game/backdoor`. Only that site can call it, and it lets the current session reach the dark web until the next `EventType::Session`. Don't mention it in `api/FAQ.md` or `README.md`, which players read.
- **Restricted sites:** `"session": "pd"` or `"dark_web"` in a site's `site.json` limits it to that session. `SiteHandlerFactory` checks it before anything else on every request, which covers pages, files and `/_game/`. These sites are also left out of `/_game/sites.json`. The icons only open them: the access check is in the helper, not the shell.

## Email and save data

- **Public API:** `api/EyeFinderApi.h` documents it, and `source/Api.cpp` exports it from the plugin. It's a plain C interface that other mods depend on, so only add to it: never change or remove a function. The real exports must stay undecorated (check with `dumpbin /exports`).
- **Threads:** API calls can come from any thread, so they only copy into a queue. `MailSystem::Update` drains it on the game thread.
- **Ownership:** the plugin owns the inbox (`MailStore`) and writes `save data\current session.json` atomically. The helper only reads that file. Player actions travel back as `ef::MailOp` through a second shared memory ring, and the unread count as `EventType::MailChanged`.
- **Mail site:** `sites/mail.eyefinder.info` reads `/api/inbox` and posts to `/api/read/<id>` and `/api/delete/<id>`. `SiteHandlerFactory` serves these only for that domain and requires an `X-EyeFinder` header, so that real sites can't reach them: a custom header forces a CORS preflight that's never granted. Mail text is always inserted as text, never HTML, because it comes from other mods.
- **Save files:** hooks on the only calls to `GenericSave` (`0x619081`) and `GenericLoad` (`0x53C70B`) copy between the session and `save data\<game save name>.json`, using `ms_SaveFileNameJustSaved` and `ms_LoadFileName`. `restartGameEvent` fires before `GenericLoad`, so it resets the inbox for both new games and loads, and a successful load then replaces it. Future mod data belongs in the same file, under its own top-level key.
- **Replies:** `EF_SendEmailWithReplies` takes a key, two answer texts and optional CLEO scripts. `MailStore` keeps a key-to-answer map that survives deleting the email, and each key is delivered once per saved game. `Api.cpp` holds a copy for `EF_GetReply`, because it can be called from any thread.
- **CLEO:** `MailSystem::RunCleoScript` looks up `_CLEO_CreateCustomScript@12` at run time and passes a null calling script. Never pass one of the game's own scripts, because CLEO reads parameters from the caller's code. Only call it from `Update`, outside the game's script loop.
- **Site game API:** every local site gets `/_game/sa.js` (from `ui/sa.js`), `/_game/sites.json`, `/_game/ads.json`, `/_game/ad/<id>/<file>` and `POST /_game/script/<name>`. The script request runs `<domain>/scripts/<name>.efs` from that site's folder. Those are compiled CLEO scripts with a different extension, because modloader starts every `.cs` file in a mod folder at game start. It's limited to simple names in the site's own folder and needs the `X-EyeFinder` header. Real sites can't reach it, and `tests.html` checks this with a real cross-origin request.
- **Game state:** `MailSystem::PublishGameState` copies a fixed `ef::GameStateData` into shared memory about five times a second, guarded by a sequence counter, and the helper serves it as `/_game/state.json`. To add a field, change `GameStateData`, both ends and the FAQ's table, and bump `kProtocol`.
- **Shared values:** pages post to `/_game/value/<key>` with the text in the URI-encoded `X-EyeFinder-Value` header, which arrives through the `ef::ValueOp` ring. `HandleBrowserOperations` applies these before starting site scripts, so a page can set a value and run the script that reads it. The API updates its copy immediately and hands changes to `MailStore` on the game thread. The helper reads only `values` from the session file, because that file also holds the mail. The session file is flushed at most four times a second, because scripts can change values every frame.
- **CLEO function calls:** `0AA7` takes its arguments in reverse order, and `pop` counts arguments. `examples/ApiDemo/values_test.txt` is the source of the hand-assembled `values_test.efs`.
- **Zoom:** each `View` keeps its own percentage, because Chromium stores zoom per domain. It's applied again after each load and on tab switches.
- **Pager memory:** `MailStore` keeps the last 10 pager messages in the save file's `pager` array, added when they arrive. `MailSystem::HandleKey` runs from the window procedure through `BrowserOverlay::SetKeyFilter`, and swallows a key only when it acts on the pager, so scroll keys still reach the game and pages when the pager is down.
- **Keys:** `KeyConfig` reads `EyeFinder92SA.ini` from the mod folder and creates it if it's missing. A bad name keeps the default and is logged.
- **Notifications:** `PagerRenderer` is game-free and drawn after the browser, so it shows over it. The help box text goes through `CHud::SetHelpMessage`, which silently refuses at times, so `MailSystem` retries until the text appears in `CHud::m_pHelpMessage`. Don't use `HelpMessageDisplayed()` to confirm it, because it stays false until the box is drawn, which once made the message repeat forever.

## Docs for creators

`api/FAQ.md` is for people making sites, and it promises specific behavior: update it when the site features, local mode, the `.efs` rule or the browser's limits change.

## Local mode

The browser starts in local mode, and the real internet is only enabled by the shell's `setInternet` command, after its confirmation dialog. Keep the dialog on every switch. There are two layers. `IsAllowedUrl` in the helper applies to navigation, popups and every resource through `OnBeforeResourceLoad`, and non-network schemes such as `data:` and `blob:` pass. `ApplyNetworkPolicy` also points Chromium's proxy preference at a dead address, because WebSockets skip `OnBeforeResourceLoad`, and WebRTC is limited to proxied traffic. Local sites use scheme handlers, not the network, so they keep working. The dialog hides the active tab through the layout message, because tabs are composited over the shell. `tests.html` reports whether its own fetch, WebSocket and WebRTC reach the internet, so check it in both modes after changing this.

## Before this ships with real internet access

- Chromium's sandbox is off (`USE_SANDBOX OFF`, `no_sandbox = true`).
- CEF must be updated with each release (`tools\fetch-cef.ps1` pins the version and checksum).
- Real sites must never get access to game features such as money.

## Conventions

- **File encoding:** sources are UTF-8 without BOM, and comments are in English. `.sln`, `.vcxproj` and `.filters` files are UTF-8 with BOM and CRLF.
- **Hooks:** to run code around a vanilla function, hook its call sites, so that changes other mods already made there are kept. Prefer a plugin-sdk event, and use `injector::function_hooker` when the return value is needed, because events don't provide it. Use `safetyhook::create_inline` when replacing a function's behavior. All of them take absolute 1.0 US addresses.
- **Plugin target:** 32-bit, C++latest, static CRT. Include project headers with quotes, and don't name one `types.h`, because the SDK's would shadow it.
- **`$(PLUGIN_SDK_DIR)`:** it's shared by every plugin in `generated/`, so changes there affect them all. A fresh clone must build against the public plugin-sdk, so don't depend on anything in `generated/shared` or on the developer's own SDK changes: libraries go in `vendor/`.
