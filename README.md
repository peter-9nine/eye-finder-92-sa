# EyeFinder92SA

A real web browser inside GTA San Andreas (1.0 US), built on [plugin-sdk](https://github.com/DK22Pac/plugin-sdk) and [CEF](https://github.com/chromiumembedded/cef).

**Status: prototype.** It has a 1992-style desktop, a web browser with tabs that reaches both the real internet and sites stored with the mod, and an email inbox that other mods can write to.

> **Don't use this prototype for browsing you care about.** Chromium's sandbox is off, and the browser has real internet access. Avoid untrusted sites and don't log in to your accounts.

## Using it

1. Run `tools\deploy.ps1` with the game closed, or copy a release into `modloader\EyeFinder92SA`.
2. Load a game and press **F8**, or **TAB** at the cyber cafe computer.

| Key | Action |
|---|---|
| F8 | Open or close the computer |
| Esc | Open the game's pause menu |
| Ctrl+T, Ctrl+W | New tab, close tab |
| Ctrl+Tab, Ctrl+Shift+Tab | Next tab, previous tab |
| Ctrl+L | Go to the address bar |
| F5 | Reload |
| Alt+Left, Alt+Right | Back, forward |

## Adding a site

Create a folder named after the address in `sites\`, with normal web files in it:

```
sites\www.mysite.com\
  index.html
  style.css
```

You can preview it in any web browser. Restart the game to pick up a new folder.

`modloader\EyeFinder92SA\EyeFinder92SA.log` records startup problems and, while the browser is open, performance every 5 seconds.

## Email and notifications for other mods

Other mods can send emails and notifications. Copy [`api/EyeFinderApi.h`](api/EyeFinderApi.h) into your mod, with no library to link:

```cpp
#include "EyeFinderApi.h"

// An email, announced as "You have 3 new emails" in the game's help box
eyefinder::SendEmail("Sweet", "Get back here", "We need you on Grove Street.", EF_NOTIFY_TEXT);

// An email, announced on the pager
eyefinder::SendEmail("Big Smoke", "Dinner", "Two number 9s.", EF_NOTIFY_PAGER);

// Notifications without an email
eyefinder::Notify("The Pay 'n' Spray is closed.");
eyefinder::Page("Cesar", "Meet me at the garage.");

int unread = eyefinder::GetUnreadCount();
```

- Calls are safe from any thread, and do nothing if EyeFinder92SA isn't installed.
- EyeFinder92SA may load after your mod, so don't call these from `DllMain` or a global constructor.
- The functions are plain `__cdecl` exports of `EyeFinder92SA.asi`, so any language that can call a DLL can use them. See the header for details.
- Text is UTF-8. Email bodies are plain text, and web addresses become links. The pager and help box show ASCII only, so accents are removed.
- Notifications wait for cutscenes and the pause menu to end. The help box also waits for the computer to close, while the pager appears over it.

`examples/ApiDemo` is a complete example. To try it, run `tools\deploy.ps1 -Demo`, then in-game press F9 (email), F10 (email on the pager), F11 (pager message) or Shift+F11 (help box text). To remove it, delete `EyeFinderApiDemo.asi` from the mod folder.

## Save data

Mail belongs to the game you're playing, so each game save has its own file in `modloader\EyeFinder92SA\save data`:

| Game save | Mod save |
|---|---|
| `GTASAsf1.b` | `save data\GTASAsf1.json` |

Saving the game writes that file, and loading the game reads it. A new game, or a save with no mod file, starts with a new inbox. As with game progress, anything since your last save is lost if you load without saving. `current session.json` holds the game in progress.

## How it works

```
gta_sa.exe (32-bit)                     EyeFinderBrowser.exe (64-bit)
  EyeFinder92SA.asi                       Chromium, rendering off-screen
    sends mouse and keyboard  ---------->   receives input
    draws frames as a texture <----------   writes frames
                     (shared memory, never blocks the game)
```

Chromium runs in its own process, so it doesn't use the game's limited 32-bit address space, and a browser crash can't take the game down. The helper and its subprocesses exit with the game.

## Building

Needs Visual Studio 2022 with the C++ tools, CMake, `PLUGIN_SDK_DIR` pointing at a built plugin-sdk, and `GTA_SA_DIR` for deploying.

```powershell
tools\build.ps1    # downloads CEF on first run, then builds everything
tools\deploy.ps1   # copies to modloader; refuses to run while the game is open
```

## Testing without the game

`EyeFinderHarness.exe` is a 32-bit program that uses the same client and renderer code as the plugin, with its own Direct3D 9 device:

```powershell
tools\harness\build\Release\EyeFinderHarness.exe <helper.exe> <shell url> <output folder>
```

It drives the desktop and browser, reports frame rates and upload times, and saves back buffer captures as raw BGRX files. To test through DXVK, copy the harness next to DXVK's `d3d9.dll`.

## Author

Peter_99
