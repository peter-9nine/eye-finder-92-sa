# How-to

## Add a computer

1. Add an entry to `locations.json`. See [the fields](sessions.md#locationsjson).
2. Use `interior` if it's indoors, because interiors share coordinates.
3. To tune it without rebuilding, edit the installed copy in `modloader\EyeFinder92SA`, restart the game, and copy it back to the project when it's right.

## Add a site

**A site that ships with the mod:** add a folder to `sites/`, named after its domain, with an `index.html`.

**A sample public site:** add it to `public/sites/` instead. Site creators' rules are in [`api/FAQ.md`](../api/FAQ.md).

**A site only some computers can reach:** add `"session": "pd"` or `"dark_web"` to its `site.json`.

New site folders are only detected when the helper starts.

## Add a session type

1. **Protocol:** add it to `SessionType` in `shared/ipc.h`, and bump `kProtocol`.
2. **Locations:** accept its name in `ParseLocation` in `source/Locations.cpp`.
3. **Prompt:** add a case to `ComputerPrompt` in `source/BrowserOverlay.cpp`.
4. **Helper:** add it to `RequiredSession` and `SendSessionToShell` in `helper/main.cpp`.
5. **Desktop:** in `ui/shell.html`, add a `body.session-<type> #desktop` color, an icon if it has its own browser, and handle it in `applySession`.
6. **Recycle bin:** add its files to `ui/recycle_bin.js`.
7. **Debug key:** add a way to reach it in `OpenFromDebugKey`.
8. **Tests:** add it to `TestSessions` in `tools/harness/main.cpp`.

## Add a desktop icon

1. Put the image in `ui/icons`.
2. Add a `.icon` button to `#desktopIcons` in `ui/shell.html`, and an `onclick`.
3. Update the click points in `tools/harness/main.cpp`, because icons below it move: `kIconBin`, `kIconLogOff` and the `ExpectIconDrawn` calls.

## Add a command from the shell to the helper

1. Call `send({ cmd: 'myCommand', ... })` in `ui/shell.html`.
2. Handle it in `ShellQueryHandler::OnQuery` in `helper/main.cpp`.

Keep the checks at the top of `OnQuery`. If the command does anything to a page, remember that only a real player action should trigger it.

## Show something over a page

Tabs are drawn over the shell, so a shell element alone won't appear. Pick one:

- **Hide the tab while it's open:** add your condition to `visible` in `sendLayout`, as the recycle bin does.
- **Draw it over the tab:** send its rectangle to the helper and copy it in `ComposeAndPublish`, as the on-screen keyboard does. Also update `HitTest`, so that clicks reach it.

## Add an event from the plugin to the helper

1. Add it to the end of `EventType` in `shared/ipc.h`, say what `a` and `b` mean, and bump `kProtocol`.
2. Send it with `client_.Send({ ef::EventType::MyEvent, 0, 0, a, b })`.
3. Handle it in `HandleEvent` in `helper/main.cpp`, which runs on CEF's UI thread.

Remember it can be dropped if the ring is full, and that a restarted helper has lost whatever you sent earlier. `EnsureHelper` sends the mail count and session again for this reason, and the shell gets them again in `OnLoadEnd`.

## Add a game state field for pages

1. Add it to `GameStateData` in `shared/ipc.h`, and bump `kProtocol`.
2. Fill it in `MailSystem::PublishGameState`.
3. Add it to `BuildGameStateJson` in `helper/main.cpp`.
4. Add it to the table in `api/FAQ.md`.

## Add a public API function

1. Export it in `source/Api.cpp`, copying into the queue if it needs the game thread.
2. Add the declaration and helper to `api/EyeFinderApi.h`, under a comment saying which version added it, and raise `EF_API_VERSION`.
3. Check that the export is undecorated, with `dumpbin /exports`.
4. Add a test to `TestApi` in `tools/harness/main.cpp`.

Never change or remove an existing function.

## Store new data in save files

Add a top-level key in `Serialize` in `source/MailStore.cpp`, and read it in `LoadFrom`. Old saves won't have it, so give it a default.

## Hook a game function

1. Find every call site, for example with IDA, and check what the caller does with the return value.
2. Use a plugin-sdk event if you don't need the return value. Otherwise, use `injector::function_hooker` as `source/Main.cpp` does.
3. Use `safetyhook::create_inline` only if you're replacing the function's behavior.

Use absolute 1.0 US addresses. Hooking the call site chains whatever another mod put there, which an inline hook on the function can't guarantee.
