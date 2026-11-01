# Mail, saves and the API

## Who owns what

The plugin owns the inbox, in `MailStore`, and writes `save data\current session.json` atomically. The helper only reads that file. Player actions travel back as `ef::MailOp` through a shared memory ring, and the unread count as `EventType::MailChanged`.

## Save files

Each game save has its own file:

| Game save | Mod save |
|---|---|
| `GTASAsf1.b` | `save data\GTASAsf1.json` |

Hooks on the only calls to `GenericSave` (`0x619081`) and `GenericLoad` (`0x53C70B`) copy between the session and that file, using `ms_SaveFileNameJustSaved` and `ms_LoadFileName`. They hook the call rather than the function, so that changes other mods made are kept, and use `injector::function_hooker` because they need the return value: mod data is only written if the game really saved.

`restartGameEvent` fires before `GenericLoad`, so it resets the inbox for both new games and loads, and a successful load then replaces it.

**New mod data** belongs in the same file, under its own top-level key.

A damaged file is set aside as `.damaged` and not loaded, because it must never stop anyone playing.

## Public API

[`api/EyeFinderApi.h`](../api/EyeFinderApi.h) documents it for other mods, and `source/Api.cpp` exports it.

- It's a plain C interface that other mods depend on, so **only add to it. Never change or remove a function.**
- The exports must stay undecorated. Check with `dumpbin /exports`.
- Calls can come from any thread, so they only copy into a queue, and `MailSystem::Update` drains it on the game thread.
- `Api.cpp` also holds copies of replies and values, so that `EF_GetReply` and `EF_GetValue` can answer from any thread.

### Emails with replies

`EF_SendEmailWithReplies` takes a key, two answer texts and optional CLEO scripts. `MailStore` keeps a key-to-answer map that survives deleting the email, and each key is delivered once per saved game.

## Mail site

`sites/mail.eyefinder.info` reads `/api/inbox`, and posts to `/api/read/<id>`, `/api/delete/<id>` and `/api/reply/<id>/<1 or 2>`.

`SiteHandlerFactory` serves these only for that domain, and they need an `X-EyeFinder` header. A custom header forces a CORS preflight that's never granted, so real sites can't reach them.

Mail text is always inserted as text, never HTML, because it comes from other mods.

## Game API for sites

Every local site gets these. The page-facing documentation is in [`api/FAQ.md`](../api/FAQ.md).

| Path | What it does | Needs the header |
|---|---|---|
| `/_game/sa.js` | The library, from `ui/sa.js` | No |
| `/_game/sites.json` | Public sites, for the home page | No |
| `/_game/ads.json`, `/_game/ad/<id>/<file>` | Ads | No |
| `/_game/state.json` | Game state | Yes |
| `/_game/values.json` | Shared values | Yes |
| `/_game/browser.json` | The connection, the session type and its id | Yes |
| `/_game/search.json` | What search can find, with each page's text | Yes |
| `/_game/hidden.json` | The hidden folder, only in a dark web session | Yes |
| `POST /_game/value/<key>` | Sets a shared value | Yes |
| `POST /_game/script/<name>` | Starts `<domain>/scripts/<name>.efs` | Yes |

The header is what keeps real sites out, and `tests.html` checks this with a real cross-origin request.

- **Game state:** `MailSystem::PublishGameState` copies a fixed `ef::GameStateData` into shared memory about five times a second, guarded by a sequence counter.
- **Shared values:** pages send the text in the URI-encoded `X-EyeFinder-Value` header, and it arrives through the `ef::ValueOp` ring. `HandleBrowserOperations` applies these before starting site scripts, so a page can set a value and run the script that reads it. The API updates its copy immediately, and hands changes to `MailStore` on the game thread.
- **Reading values:** the helper reads only `values` from the session file, because that file also holds the mail, which isn't for every site.
- **Flushing:** the session file is written at most four times a second, because scripts can change values every frame.

## CLEO

`MailSystem::RunCleoScript` looks up `_CLEO_CreateCustomScript@12` at run time, because CLEO may not be installed, and passes a null calling script.

- **Never pass one of the game's own scripts,** because CLEO reads parameters from the caller's code.
- **Only call it from `Update`,** outside the game's script loop.

Site scripts are compiled CLEO scripts with an `.efs` extension, because modloader starts every `.cs` file in a mod folder when the game starts. They're limited to simple names in the site's own `scripts` folder.

For hand-assembled scripts, `0AA7` takes its arguments in reverse order, and `pop` counts arguments. `examples/ApiDemo/values_test.txt` is the source of `values_test.efs`.

## Notifications

The help box text goes through `CHud::SetHelpMessage`, which silently refuses at times. So `MailSystem` retries until the text appears in `CHud::m_pHelpMessage`.

Don't use `HelpMessageDisplayed()` to confirm it. It stays false until the box is drawn, which once made a message repeat forever.
