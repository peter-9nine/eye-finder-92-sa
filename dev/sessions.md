# Sessions

A session is one use of a computer. The computer's location decides its type, and the type decides what the desktop offers, what the player can reach, and whether they pay.

| Type | Desktop | Timer and payment | Extra browser | Its site |
|---|---|---|---|---|
| `normal` | Teal | Yes | None | |
| `pd` | Navy | Free | SAPD Network | `network.sapd.sa` |
| `dark_web` | Black | Free | Dark Web | `hidden.onion` |

## `locations.json`

It lists the computers. It's in the project root, and in the mod folder when deployed.

```json
{
  "locations": [
    {
      "name": "Cyber cafe",
      "type": "normal",
      "position": [1170.4697, -1655.1893, 1014.3158],
      "radius": 3.0,
      "pricePerMinute": 10
    },
    {
      "name": "Los Santos Police Department",
      "type": "pd",
      "position": [246.78, 63.9, 1003.64],
      "radius": 5.0,
      "interior": 6
    }
  ]
}
```

| Field | Required | Meaning |
|---|---|---|
| `name` | No | For people reading the file. It defaults to `Computer` |
| `type` | Yes | `normal`, `pd` or `dark_web` |
| `position` | Yes | `[x, y, z]` |
| `radius` | No | How close the player must be. It defaults to 3 |
| `interior` | No | Only matches in this interior. Use it for interiors, because they share coordinates |
| `pricePerMinute` | No | Normal computers only, and it defaults to 10. Use 0 for a free computer, which also has no timer |

A bad entry is skipped and logged, and the rest still load. With no usable entries, the cyber cafe is used, because a typo must never take away every computer in the game.

`tools\deploy.ps1` never overwrites an installed file that was edited: it warns instead, so that you can tune positions in-game without losing them. Copy your changes back to the project, or a release won't have them.

## From prompt to payment

1. **Prompt:** `CheckForComputers` runs each frame while the browser is closed. Near a computer, it shows the prompt, naming L1 or TAB depending on `PadInput::InHands()`.
2. **Opening:** `BeginOpening` takes the type and price from the computer, and `SendSession` sends `EventType::Session` and `SessionTime`. They're sent again if the helper restarts, for example after a resolution change.
3. **While open:** `UpdateSessionClock` sends `SessionTime` every second. The clock only runs in `Phase::Open`, so fades and the pause menu are free.
4. **Closing:** `FinishClosing` calls `ChargeSession`, which takes the money. The receipt goes through `MailSystem`'s help box queue, which retries, because `CHud::SetHelpMessage` silently refuses at times.

Payment rules, in `SessionCost` and `ChargeSession`:

- Every started minute is paid in full, and a session always costs at least one.
- A player short of money pays what they have, and doesn't go into debt.
- Only a normal close charges. A helper crash or loading a game costs nothing.

The timer's numbers come from the plugin, and the shell only displays them.

## Desktop

`applySession` in `ui/shell.html` handles `ef.onSession`:

- It sets a `session-<type>` class on the body, which picks the desktop's color in CSS.
- It shows that session's icon and corner label.
- It shows the timer if the session is `normal` with a price above 0.
- **If the type changed, it closes every tab,** because a tab from another computer must not stay open.

Icons are in `ui/icons`, in a column that fills from the top. Removing or reordering them moves the ones below, and the harness's click points depend on this.

The timer sits in the top-right corner, and moves onto the title bar next to its buttons when the window leaves no room.

## Restricted sites

Add `session` to a site's `site.json`:

```json
{
  "title": "SAPD Network",
  "session": "pd",
  "listed": false
}
```

`SiteHandlerFactory::Create` checks this before anything else, on every request. That covers pages, images, scripts and `/_game/`, so typing the address or following a link from elsewhere gets "Connection refused". These sites are also left out of `/_game/sites.json`.

**The check is in the helper, not the shell.** The desktop icons only open the site. Hiding an icon doesn't protect anything.

The session lives in `gSessionType`, an atomic, because sites are served on another thread.

## Recycle bin

The window is in `ui/shell.html`, and `ui/recycle_bin.js` lists the files for each session type. Each file only needs a `name` and `text`, and its text is always inserted as text. While the bin is open, the tab is hidden through the layout message, because tabs are drawn over the shell.
