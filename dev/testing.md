# Testing

## The harness

`EyeFinderHarness.exe` is a 32-bit program that uses the plugin's game-free code with its own Direct3D 9 device. It starts the real helper and drives real Chromium, through the same events the plugin sends. There's no other test or lint setup.

```powershell
$root = (Get-Location).Path
$url = "file:///$root/helper/build/system/ui/shell.html" -replace '\\','/'
New-Item -ItemType Directory -Force test-output | Out-Null
tools\harness\build\Release\EyeFinderHarness.exe "$root\helper\build\Release\EyeFinderBrowser.exe" $url "$root\test-output"
```

It prints `FAIL: <what>` for each problem, then `PASS` or `FAIL`, and exits with 0 or 1.

Part of it loads a real website, so run it with an internet connection.

### What it covers

- The desktop, icons, tabs, toolbar, dropdowns, zoom and Log Off.
- Local mode and the internet warning.
- The mail site, replies, shared values and game state.
- The site script button, and new files loading without a restart.
- The pager, its memory and the keys file.
- The on-screen keyboard, using simulated pad events.
- Sessions, restricted sites and the recycle bin.
- Search on both webs, the hidden folder, the engine choice, and the confirmation before a real search.
- `locations.json` and the CEF check.
- The public API, using a stand-in module named `EyeFinder92SA.asi`.
- Performance, printed as frame rates and upload times.

### Look at the captures

**A passing run doesn't prove that things look right.** It once passed with every desktop icon broken.

It saves what's really in the back buffer as raw 1280x720 BGRX files. To turn them into images:

```powershell
tools\raw2png.ps1 "$root\test-output"
```

### When it fails after a layout change

Its click coordinates and expected colors match `ui/shell.html`, `sites/www.eyefinder.info/index.html`, `tests.html`, `keyboard-tests.html` and `search-tests.html` at 1280x720. If you move something in those, update the constants at the top of `tools/harness/main.cpp` and in its test functions. Check the captures first, to tell a moved button from a real bug.

### Testing through DXVK

Copy the harness next to DXVK's `d3d9.dll`, and run it from there. Do this for any Direct3D change.

### Visiting extra pages

To get captures of more pages, set `EF_HARNESS_VISIT` to a list of addresses separated by `;`. Each gets one capture at the top and one at the bottom.

## What it can't test

The harness has no game, and doesn't compile the game-specific files. So these need testing in-game:

- Reading the controller, including its real speeds and repeat rates.
- The prompt at a computer, TAB, L1 and the F8 debug keys.
- Finding the right computer from the player's position.
- Taking the player's money, and the receipt.
- The save and load hooks.
- The pause menu, fades and player controls.
- The help box.
- CLEO really running scripts.

## In-game checklist

Run through the parts your change could affect. Remember to deploy first, because the game doesn't see a build until then.

**Opening and closing**
- [ ] F8 opens and closes, and the game's controls come back afterwards.
- [ ] Fades look right in both directions.
- [ ] Esc opens the pause menu, pages go quiet, and everything's still there on return.
- [ ] Changing resolution in the menu recovers.

**Sessions**
- [ ] F8, Shift+F8 and Ctrl+F8 give the right desktop.
- [ ] Each session's icon opens its site, and other sessions refuse it.
- [ ] The timer runs in a paid session, and doesn't run during the pause menu.
- [ ] Closing takes the money and shows the receipt.
- [ ] With too little money, it takes what you have.
- [ ] At a real computer, the prompt appears and names L1 or TAB, and changes when you switch.

**Search**
- [ ] The dark web's typed intro plays once in each session, and a key press skips it.
- [ ] Searching on both webs works with the on-screen keyboard, and Go runs the search.
- [ ] A real search asks first in local mode, and goes straight there after connecting.

**Controller**
- [ ] Cursor, scrolling, click, back, forward, reload and tabs.
- [ ] Sprinting into the browser with A held doesn't click.
- [ ] Leaving the pause menu with A doesn't click.
- [ ] The on-screen keyboard opens on a field, and its buttons all work.
- [ ] Holding D-pad left opens the pager, and it doesn't react while the keyboard is open.

**Saves**
- [ ] Mail survives saving and loading, and each save keeps its own.
- [ ] A new game starts with a new inbox.

**Other mods**
- [ ] `tools\deploy.ps1 -Demo`, then F9, F10, F11 and Shift+F11.
- [ ] A site's CLEO button runs its script.

## Checking local mode

After changing anything about the network, open `https://www.eyefinder.info/tests.html` in both modes. It reports whether its own fetch, WebSocket and WebRTC reach the internet, and all of them must say `blocked` in local mode.
