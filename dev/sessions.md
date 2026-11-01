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

## The hidden folder

A site inside a `hidden` folder, in `sites\hidden` or `system\sites\hidden`, can only be reached in a dark web session. `RegisterSitesIn` registers these with `SessionType::DarkWeb`, so they get the same check on every request as restricted sites, without needing `session` in their `site.json`.

`/_game/hidden.json` lists them, and answers 403 outside a dark web session, because the list itself is a secret.

## Search

Both webs search the same way: `rankMatches` in `ui/sa.js` needs every word to appear, and ranks a match in the address or title above one in the description or keywords, and those above one in the page's text. The helper supplies the data, with the visible text of each site's `index.html` from `VisibleText`, cut to 4000 bytes.

| | EyeFinder | Dark web |
|---|---|---|
| Page | `sites/www.eyefinder.info/index.html` | `sites/hidden.onion/index.html` |
| Function | `sa.searchSites` | `sa.searchHidden` |
| Endpoint | `/_game/search.json` | `/_game/hidden.json` |
| Looks in | Public sites, except unlisted and restricted ones | The hidden folders |

**EyeFinder** has a toggle between San Andreas and the real internet, and a choice of engine. The toggle starts on whichever matches the connection, from `sa.browser()`. The engine is remembered in the page's `localStorage`. A real search just navigates to the engine, so in local mode the usual confirmation appears: that's the only thing keeping it local, and it must stay that way.

**The address bar** sends anything that isn't an address to the home page of the web that tab is on, as `?q=`, in `toUrl` in `ui/shell.html`. That page then applies its own rules, so the shell needs no search logic.

## The dark web's intro

`sites/hidden.onion/index.html` plays a typed prompt the first time it's opened in each session. `sa.browser()` returns a `sessionId`, which the helper changes on each `EventType::Session`, and the page keeps the last one it played for in `localStorage`. It checks again on `visibilitychange`, because a tab left open from the last session is already loaded. A key press or click skips it. It opens straight, with the commands to connect to the Area 69 server, from `CONNECTING`. Then the humor follows, from `CONFESSIONS`, `THOUGHTS` and `ANSWERS`.

## The 1992 effects

**Dial-up popup.** `OnBeforeBrowse` tells the shell about each page a tab enters, and `startDial` in `ui/shell.html` sometimes shows a popup with a slow speed and San Andreas flavor, from `DIAL_STEPS` and `DIAL_SPEEDS`. It's meant as a rare touch of nostalgia, so `DIAL_CHANCE` is 1 page in 10, and moving within one site never shows it. It lasts 1.5 to 2.7 seconds, and longer if the page still hasn't loaded, up to `DIAL_LONGEST_MS` more. `playDialSound` synthesizes the modem handshake, with no audio files, and the helper mutes the shell on `Hide` so that it stops with the pause menu. The page isn't delayed: it loads hidden, through the layout message with `keepFocus`, so the keyboard still belongs to it afterwards. Only main frames count, so what loads inside a page doesn't bring the popup back. The harness turns it off with `EventType::ModemEffect` because its clicks need pages at once, and one test forces it on with `a = 2`.

Pages with an intro wait until they can be seen, so that they don't play it behind the popup.

**Mail logon.** `playLogon` in `sites/mail.eyefinder.info/index.html` types CJ's details into a logon screen, the first time mail opens in each session, using the same `sessionId` as the dark web's intro. It's only for show, and nothing checks the details. A key press or click skips it.

## The easter egg

**Spoiler, so don't put this anywhere players read it.** A misspelled EyeFinder address lands on `sites/www.eyefnder.info`, a dull parked-domain page that asks "Did you mean www.eyefinder.info?". Answering "No" opens the dark web from any computer.

- **The typos** are recognized by `IsEyeFinderTypo` in `shared/AddressTypo.h`, not listed: any one letter missing, wrong, extra or swapped, and two for `.info`, with or without `www.`. Wrong endings count too, like `.inf` or `.com`. It's stricter on `.com`, `.net` and `.org`, so that real sites such as `eyeliner.com` keep working. `TypoHandlerFactory` is registered with no domain, so it's asked about every host without a site of its own, and it keeps the address the player typed.
- **The way in** is `POST /_game/backdoor`, which only that site can call. It sets `gBackdoorOpen`, and `SessionReaches` then allows dark web sites and `/_game/hidden.json`.
- **It ends with the session.** The next `EventType::Session` closes it, and tells the shell to close every tab through `closeTabs`.
- **Nothing else changes.** The player's still in a normal session, so the desktop has no dark web icon, and the timer keeps charging.

It stays out of search and the home page's list, because it's a mod site marked as unlisted.

## Recycle bin

The window is in `ui/shell.html`, and `ui/recycle_bin.js` lists the files for each session type. Each file only needs a `name` and `text`, and its text is always inserted as text. While the bin is open, the tab is hidden through the layout message, because tabs are drawn over the shell.
