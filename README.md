# EyeFinder92SA

A real web browser inside GTA San Andreas (1.0 US), built on [plugin-sdk](https://github.com/DK22Pac/plugin-sdk) and [CEF](https://github.com/chromiumembedded/cef).

**Status: prototype.** It has a 1992-style desktop, a web browser with tabs that reaches both the real internet and sites stored with the mod, and an email inbox that other mods can write to.

> **Local sites only by default.** The browser starts with only the sites stored with the mod. The **LOCAL** button in the toolbar connects to the real internet, after a confirmation every time. Chromium's sandbox is off in this prototype, so when connected, avoid untrusted sites and don't log in to your accounts.

## Using it

1. Run `tools\deploy.ps1` with the game closed, or copy a release into `modloader\EyeFinder92SA`.
2. Load a game and press **F8**, or **TAB** at the cyber cafe computer.

| Key | Action |
|---|---|
| F8 | Open or close the computer |
| F6 | Bring up the pager with the latest message, or put it away |
| Page Up, Page Down | While the pager is up, go to older and newer messages |
| Esc | Open the game's pause menu |
| Ctrl+T, Ctrl+W | New tab, close tab |
| Ctrl+Tab, Ctrl+Shift+Tab | Next tab, previous tab |
| Ctrl+L | Go to the address bar |
| Ctrl and + or -, or Ctrl+wheel | Zoom in or out |
| Ctrl+0 | Reset the zoom |
| F5 | Reload |
| Alt+Left, Alt+Right | Back, forward |

To change F8, F6, Page Up or Page Down, edit `EyeFinder92SA.ini` in the mod folder and restart the game. The file is created the first time the game starts, and it lists the key names you can use.

The pager remembers its last 10 messages, saved with each saved game. Messages from `EF_Page` and emails announced on the pager are both remembered.

## Adding a site

See the [FAQ for site creators](api/FAQ.md) for the details, including how to use CLEO scripts from a page.

There are two kinds of site:

| Kind | In the game folder | In this repository | When the mod updates |
|---|---|---|---|
| Public sites and ads | `modloader\EyeFinder92SA\sites` and `ads` | Samples in `public` | Left alone |
| The mod's own sites | `modloader\EyeFinder92SA\browser\sites` | `sites` | Replaced |

To add a public site, create a folder named after its address, with normal web files in it. The EyeFinder home page lists it automatically, in a new random order on each visit, with ads between the sites. Restart the game to pick up a new folder.

```
sites\www.mysite.com\
  index.html
  site.json
```

Any page can show an ad for another site with `<div data-eyefinder-ad></div>` and `<script src="/_game/sa.js"></script>`.

`tools\import_old_pages.py` converts pages and ads from the old image-based EyeFinder92 into public sites and ads.

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

// An email the player answers with one of two buttons. You write both answers.
// Each key is delivered once per saved game, so it's safe to send it every time your mod starts.
eyefinder::SendEmailWithReplies("Ryder", "You in?", "Meet me at the spot.", EF_NOTIFY_PAGER,
    "mymod_ryder_meeting",                    // your unique key
    "I'm in, homie.", "Forget it, fool.",     // the two answers
    "mymod_good.cs", nullptr);                // optional CLEO script for each answer

// Later, check what the player chose: EF_REPLY_NONE, EF_REPLY_GOOD or EF_REPLY_BAD
int reply = eyefinder::GetReply("mymod_ryder_meeting");

// Start a CLEO script right away
eyefinder::RunCleoScript("mymod_script.cs");

// Values shared with web pages and CLEO scripts, saved with the player's game
eyefinder::SetValueInt("mymod.level", 3);
int level = eyefinder::GetValueInt("mymod.level", 0);
```

- Calls are safe from any thread, and do nothing if EyeFinder92SA isn't installed.
- EyeFinder92SA may load after your mod, so don't call these from `DllMain` or a global constructor.
- The functions are plain `__cdecl` exports of `EyeFinder92SA.asi`, so any language that can call a DLL can use them. See the header for details.
- Text is UTF-8. Email bodies are plain text, and web addresses become links. The pager and help box show ASCII only, so accents are removed.
- Notifications wait for cutscenes and the pause menu to end. The help box also waits for the computer to close, while the pager appears over it.

`examples/ApiDemo` is a complete example. To try it, run `tools\deploy.ps1 -Demo`, then in-game press F9 (email with answers), F10 (email on the pager), F11 (pager message) or Shift+F11 (help box text). To remove it, delete `EyeFinderApiDemo.asi` from the mod folder.

## CLEO scripts

CLEO scripts can start from an email answer, from another mod through `EF_RunCleoScript`, or from a button on a site:

```
sites\www.mysite.com\
  index.html
  scripts\hello.efs
```

```html
<script src="/_game/sa.js"></script>
<button onclick="sa.runScript('hello')">Do it</button>
```

A site's script is a compiled CLEO script renamed from `.cs` to `.efs`. This matters because modloader starts every `.cs` file in a mod folder when the game starts. For the same reason, a script started from an email answer shouldn't be a `.cs` file inside a modloader folder, unless you also want it to run at startup.

Pages can read the game's state with `sa.state()`, and share data with scripts and mods using `sa.get()` and `sa.set()`. The [FAQ](api/FAQ.md) has examples, including reading and writing values from a CLEO script.

A site can only start scripts from its own `scripts` folder, using simple names. Real websites can't start scripts at all. **Installing a site with scripts is like installing a mod, because CLEO scripts can do anything the game can, so only install sites you trust.** CLEO is optional: without it, these requests are logged and ignored. Tested with CLEO 5.4.0.

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
