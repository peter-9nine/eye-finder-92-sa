# EyeFinder92SA developer docs

EyeFinder92SA is a GTA San Andreas (1.0 US) ASI plugin that shows a real web browser over the game. Chromium, through CEF, runs in a separate 64-bit process, and the plugin draws its frames.

These docs are for people working on the mod itself. They're also copied to `modloader\EyeFinder92SA\dev`, but paths and links outside this folder refer to the [project on GitHub](https://github.com/peter-9nine/eye-finder-92-sa).

If you're doing something else, you want one of these instead:

| You want to | Read |
|---|---|
| Make a website for the in-game browser | [`api/FAQ.md`](../api/FAQ.md) |
| Send emails, share values or run C++ from a web page, from your own mod | [`api/EyeFinderApi.h`](../api/EyeFinderApi.h), with a working example in [`examples/ApiDemo`](../examples/ApiDemo/main.cpp) |
| Install or play the mod | [`README.md`](../README.md) |

## Where to go

| Doc | What's in it |
|---|---|
| [Getting started](getting-started.md) | What to install, building, deploying, debug keys and logs |
| [Architecture](architecture.md) | The two processes, how they talk, the folders, and which file does what |
| [Input](input.md) | Mouse, keyboard, controller, the on-screen keyboard and the pager |
| [Sessions](sessions.md) | Computers, `locations.json`, payment, restricted sites and the desktop |
| [Mail, saves and the API](data-and-api.md) | The inbox, save files, shared values, CLEO and the public API |
| [Testing](testing.md) | The harness, what it can't see, and what to check in-game |
| [Releasing](release.md) | Building the package, CEF updates and the release checklist |
| [How-to](how-to.md) | Step-by-step changes, such as adding a computer, a session type or a game state field |
| [Known issues](known-issues.md) | What's unfinished or wrong right now |

## Rules that are easy to break

Each of these has a reason, explained in the docs above.

- **Never block the game.** Nothing on the game thread may wait for the helper. The event ring drops events when full, and the game keeps its last frame if the helper stalls.
- **`shared/ipc.h` must have the same layout in 32 and 64 bits.** Use fixed-size types only, and bump `kProtocol` whenever it changes.
- **The public API only grows.** Other mods depend on it, so never change or remove a function.
- **Web pages are untrusted, and so is text from other mods.** Insert their text as text, never as HTML. Real sites must never reach game features such as money.
- **Only the shell can give the helper commands.** `cefQuery` is only injected into `file:` pages, the handler checks the browser and URL, the shell can't navigate away, and tabs can't open `file:` URLs. Keep all of that when adding commands.
- **Local mode stays the default.** The confirmation dialog appears on every switch to the real internet.
- **Don't keep your own files in `system` or `dev` in the game folder.** Every build mirrors both, and deletes anything that isn't in the project. To change these docs, edit them in the project.
- **Look at the captures.** A passing harness doesn't prove that things look right. It once passed with every desktop icon broken.

## Conventions

- Sources are UTF-8 without BOM, and comments are in English. `.sln`, `.vcxproj` and `.filters` files are UTF-8 with BOM and CRLF.
- The plugin is 32-bit, C++latest, with the static CRT. Include project headers with quotes, and don't name one `types.h`, because the SDK's would shadow it.
- To run code around a vanilla function, hook its call sites, so that changes other mods already made there are kept. Prefer a plugin-sdk event, and use `injector::function_hooker` when you need the return value, because events don't provide it. Use `safetyhook::create_inline` only when replacing a function's behavior. All of them take absolute 1.0 US addresses.
- `$(PLUGIN_SDK_DIR)` is shared with other plugins, so changes there affect them too.
- When you change what a site can do, update `api/FAQ.md`, because it promises specific behavior to site creators.
