# Architecture

## Two processes

```
gta_sa.exe (32-bit)                     EyeFinderBrowser.exe (64-bit)
  EyeFinder92SA.asi                       CEF / Chromium, rendering off-screen
    sends input and game state ------->     shell view (ui/shell.html)
    draws frames as a texture  <-------     one view per tab
                     (shared memory, never blocks the game)
```

| | Plugin | Helper |
|---|---|---|
| File | `EyeFinder92SA.asi` | `EyeFinderBrowser.exe` |
| Built with | MSBuild, `EyeFinder92SA.sln` | CMake, `helper/` |
| Configurations | `Release|GTASA`, `Debug|GTASA` | Release only |
| Source | `source/`, `include/` | `helper/main.cpp` |

Chromium runs separately so that it doesn't use the game's limited 32-bit address space, and so that a browser crash can't take the game down. A job object makes the helper and its subprocesses die with the game, even if the game crashes.

## How they talk

They communicate only through `shared/ipc.h`, a shared memory section with these parts:

| Part | Direction | What it's for |
|---|---|---|
| Triple frame buffer | Helper to plugin | Frames. Each side swaps its slot with `ready`, so nobody waits |
| `ring` of `Event` | Plugin to helper | Input, show and hide, mail count and session. It drops events when full |
| `mailOps` | Helper to plugin | Marking mail read, deleting it and answering it |
| `siteScripts` | Helper to plugin | CLEO scripts that a local site asked to start |
| `valueOps` | Helper to plugin | Shared values set by pages |
| `gameState` | Plugin to helper | Published about five times a second, and guarded by a sequence counter |
| `cursor`, `closeRequest`, `keyboardOpen` | Helper to plugin | Single atomic values |

The layout must be identical in 32 and 64 bits, so use only fixed-size types. Bump `kProtocol` whenever it changes: the helper exits at startup if the numbers differ.

## Inside the helper

The helper runs several windowless Chromium views:

- **The shell**, one full-screen view of `ui/shell.html`, with the desktop and browser window.
- **One view per tab.** Tabs have their own views so that real sites work, because many refuse to load in an iframe.

The shell reports the rectangle reserved for page content, and `ComposeAndPublish` copies the active tab into it, so the plugin only ever sees one frame. This has a consequence you'll run into: **tabs are drawn over the shell**. Anything in the shell that must appear over a page has two options:

- Hide the tab through the layout message, as the internet warning and the recycle bin do.
- Have the helper copy that part of the shell back over the tab, as the on-screen keyboard does.

Mouse input goes to whichever view is under the cursor, and keyboard input to the last one clicked. All of this state lives on CEF's UI thread, except the event ring and the few atomics.

## The shell's channel to the helper

```
ui/shell.html  --window.cefQuery({cmd: ...})-->  ShellQueryHandler::OnQuery
ui/shell.html  <--window.ef.something(json)---   CallShell
```

| Commands to the helper | Callbacks to the shell (`window.ef`) |
|---|---|
| `layout`, `openTab`, `closeTab`, `activateTab` | `onTab`, `onStatus`, `openTab`, `shortcut` |
| `navigate`, `back`, `forward`, `reload`, `stop`, `zoom` | `onBlocked` |
| `setInternet` | `onMail`, `onSession` |
| `logOff` | |
| `keyboard`, `keyboardApply` | `keyboardOpen`, `keyboardCancel`, `padClicked`, `padKeyboard` |

Only the shell may use this channel. Four things enforce it, and all of them must stay:

1. `cefQuery` is only injected into `file:` pages, in `OnContextCreated`.
2. `OnQuery` checks that the caller is the shell's browser, its main frame and its URL.
3. The shell can't navigate away, in `OnBeforeBrowse`.
4. Tabs can't open `file:` URLs.

## Local sites

`SiteHandlerFactory` serves each site folder as that domain over `http` and `https`, so adding a site means adding a folder. Sites are registered when the helper starts.

| Kind | In the project | In the game folder | On deploy |
|---|---|---|---|
| The mod's own | `sites/` | `system\sites` | Mirrored |
| Public sites and ads | `public/` | `sites`, `ads` | Added to, because they belong to the user |
| Dark web sites | `public/sites/hidden/` | `sites\hidden` | Added to. See [Sessions](sessions.md#the-hidden-folder) |

On a name clash the mod's site wins, and only the mod's `mail.eyefinder.info` gets the mail API.

Every local site also gets the game API under `/_game/`, described in [Mail, saves and the API](data-and-api.md).

## Local mode

The browser starts with only local sites, and the real internet needs the shell's `setInternet` command, after a confirmation every time. There are two layers:

1. **`IsAllowedUrl`** applies to navigation, popups and every resource through `OnBeforeResourceLoad`. Schemes that don't use the network, such as `data:` and `blob:`, pass.
2. **`ApplyNetworkPolicy`** points Chromium's proxy preference at a dead address, because WebSockets skip `OnBeforeResourceLoad`. It also limits WebRTC to proxied traffic.

Local sites use scheme handlers rather than the network, so they keep working. `sites/www.eyefinder.info/tests.html` reports whether its own fetch, WebSocket and WebRTC reach the internet, so check it in both modes after changing any of this.

## Folders

The game folder separates CEF from everything else, so that the mod can ship without it:

```
modloader\EyeFinder92SA\
  EyeFinder92SA.asi, EyeFinder92SA.ini, locations.json, save data\
  Install CEF.bat, install-cef.ps1   the installer for CEF
  browser\   EyeFinderBrowser.exe, INSTALL_CEF.txt, and CEF
  system\    ui\ and sites\: the mod's, replaced on update
  sites\, ads\   public: anyone can add to them
  dev\       these docs
```

`helper/build` has the same layout, with `Release` as its `browser`. The helper finds everything from its own parent folder, in `ModDirectory()`.

## Which file does what

Plugin code is split so that the harness can use part of it without the game.

**Game-free, also compiled into the harness:**

| File | What it does |
|---|---|
| `source/BrowserClient.cpp` | Starts the helper, the job object, shared memory, and the check for CEF |
| `source/OverlayRenderer.cpp` | Direct3D 9 upload and drawing, and the cursor |
| `source/PagerRenderer.cpp` | Draws the pager, plays its beeps, and `ToPlainAscii` |
| `source/MailStore.cpp` | The inbox, replies, values, pager memory and their files |
| `source/KeyConfig.cpp` | `EyeFinder92SA.ini` |
| `source/Locations.cpp` | `locations.json` and the session price |

**Game-specific:**

| File | What it does |
|---|---|
| `source/Main.cpp` | plugin-sdk events and hooks |
| `source/BrowserOverlay.cpp` | Opening and closing, input, the pause menu, sessions, and player controls |
| `source/PadInput.cpp` | Reads the controller |
| `source/MailSystem.cpp` | Runs the inbox on the game thread, notifications, CLEO, and game state |
| `source/Api.cpp` | The public API's exports. It's also built as the harness's stand-in |

**Helper and interface:**

| File | What it does |
|---|---|
| `helper/main.cpp` | The whole helper: views, compositing, sites, the game API, commands and input |
| `ui/shell.html` | Desktop, browser window, on-screen keyboard, recycle bin and timer |
| `ui/recycle_bin.js` | What's in the recycle bin for each session |
| `ui/sa.js` | The library for pages, served as `/_game/sa.js` |
| `shared/ipc.h` | The protocol |

## Non-obvious behavior

- **Pause menu:** the game opens it when `CPad::NewKeyState.esc` goes down, so Escape must reach the game. While `m_bMenuActive` is set, the plugin passes all input through, stops drawing and mutes pages. On return it restarts the helper if the resolution changed.
- **Fades:** the game fades to black and the browser appears. The browser darkens itself for its own fade, because it's drawn after the game's fade.
- **Render state:** `OverlayRenderer::Draw` wraps everything in a state block, because RenderWare caches device state and must not see it change.
- **Device loss:** the texture is in `D3DPOOL_DEFAULT`, so `OnDeviceLost` releases it and `OnDeviceReset` asks for a new frame.
- **Resolution changes:** the helper restarts if the game's size changed, which loses the open tabs.
- **Dropdowns:** Chromium paints them as a separate `PET_POPUP` element, and the helper composites it onto the frame.
- **Zoom:** each `View` keeps its own percentage, because Chromium stores zoom per domain. It's applied again after each load and on tab switches.
