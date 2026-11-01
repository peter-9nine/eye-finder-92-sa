# Input

## Mouse and keyboard

- **Mouse:** polled with `GetCursorPos` and `GetAsyncKeyState`, because the game reads the mouse through DirectInput. The wheel comes from `CPad`.
- **Cursor recentering:** the game recenters the cursor every frame, so the hook at `0x007453F0` stops it while the browser is open.
- **Keyboard:** comes from a window procedure subclass, `BrowserOverlay::WindowProc`. `WM_KEYUP` also reaches the game, so that a key held on opening doesn't stick. Escape reaches the game too, because it opens the pause menu.
- **Focus:** navigating can replace Chromium's internal widget, which then has no focus and silently refuses to open dropdowns. So the helper reasserts focus on every click.

**The cursor** is drawn by `OverlayRenderer`, as an arrow, a pointing hand or a text cursor, depending on what Chromium reports for the view under it. Browsers show an arrow over buttons by default, so `OnLoadEnd` adds a low-priority style to every page that makes them show a hand. A page's own `cursor` style still wins.

Shortcuts such as Alt+Left, F5 and Ctrl+Tab are handled in `HandleShortcut` in the helper, and the controller reuses them.

## Controller

`PadInput` needs GInput, and without it there's no controller support.

GInput's API only says whether the pad is in use and how it's configured. It doesn't provide buttons. So:

- `HasPadInHands()` says whether the player is using the pad.
- The buttons and sticks are read from `CPad::GetPad(0)->NewState`, which the game keeps updating even with the player's controls disabled.

The check matters because the mouse also feeds into `NewState`, so a click could otherwise look like a button.

| Input | In the browser | In the on-screen keyboard |
|---|---|---|
| Left stick | Moves the cursor | Moves between keys |
| Right stick | Scrolls | |
| D-pad up, down | Arrow keys, with repeat | Moves between keys |
| D-pad left, right | The pager | Moves between keys |
| A / Cross | Click | Press the selected key |
| B / Circle | Back | Close, changing nothing |
| X / Square | Reload | Delete |
| Y / Triangle | Forward | Space |
| LB, RB / L1, R1 | Previous and next tab | Move the text cursor |
| L3 | | Shift |
| Back / Select | | Go |
| Start | Pause menu | Pause menu |

And outside the browser:

| Input | What it does |
|---|---|
| L1 at a computer | Opens the browser |
| Hold D-pad left for half a second | Opens or closes the pager |
| D-pad left, right, with the pager up | Older and newer messages |

Things to keep in mind:

- **The left stick moves the real Windows cursor.** `UpdateMouse` then reads it like the mouse, so the two can't disagree.
- **Held buttons don't count.** A button held when the browser appears, when the pause menu closes, or when the on-screen keyboard opens or closes is ignored until released. Otherwise the A used to sprint or leave the menu would click whatever's under the cursor. That's `padIgnored_`.
- **The game also sees the buttons.** Player controls are disabled while the browser is open, which blocks most effects. Outside it, D-pad left and right also answer conversations, which is why opening the pager needs a hold.
- **Start isn't available,** because it must reach the game. The triggers aren't used, because GInput's controls set may route them elsewhere.

To change the speeds, see `kPadCursorScreensPerSecond` and `kPadWheelPerSecond` in `source/BrowserOverlay.cpp`.

## On-screen keyboard

It opens only after a pad click, so mouse users never see it.

```
A on a field
  -> plugin: MouseDown with b = 1
  -> helper, 60 ms after the click: "ef.queryField" to the tab's focused frame
  -> render process: reads the focused element with CefV8Context::Eval
  -> helper: "ef.fieldInfo" -> window.ef.keyboardOpen in the shell
```

A click on the shell goes to `ef.padClicked` instead, since the address bar is its only field. The 60 ms gives Chromium time to move the focus.

**The page doesn't change until Go.** The text lives in the shell's `#kbText`. Go sends `ef.applyField`, which writes the text with `execCommand('insertText')`, so pages that listen for input notice. If that doesn't take, it sets the value directly and sends the events.

**Go then presses Enter,** with a real key event, unless the field has several lines or its form has another empty field. That stops a username from submitting before its password is filled in.

Close, B or a click outside changes nothing.

While it's open:

- The tab keeps Chromium's focus, so that the page doesn't see a blur.
- The shell gets the physical keyboard, in `gKeyboard`, and the tab gets it back on closing.
- The helper copies the keyboard's rectangle over the tab in `ComposeAndPublish`.
- `Header::keyboardOpen` hands the pad to it: `UpdatePadKeyboard` sends `EventType::PadKeyboard` instead of moving the cursor, and the pager stops listening.

The keys are defined in `keyboardRows` in `ui/shell.html`, and field types `text`, `search`, `url`, `email`, `tel`, `password` and `number` open it, as do text areas and editable content.

## Pager

`PagerRenderer` is game-free and drawn after the browser, so it shows over it.

- **Keyboard:** F6, Page Up and Page Down by default, from `EyeFinder92SA.ini`. `MailSystem::HandleKey` runs from the window procedure through `BrowserOverlay::SetKeyFilter`, and only swallows a key when it acts on the pager, so scroll keys still reach the game and pages when the pager is down.
- **Controller:** `MailSystem::UpdatePad`.
- **Memory:** `MailStore` keeps the last 10 messages in the save file's `pager` array, added when they arrive.

## Keys file

`KeyConfig` reads `EyeFinder92SA.ini` from the mod folder, and creates it if it's missing. A bad name keeps the default and is logged. Changes need a game restart.
