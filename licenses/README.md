# Third-party licenses

EyeFinder92SA itself is under the [PolyForm Noncommercial License 1.0.0](../LICENSE). It's built with the libraries below, which keep their own licenses.

| Library | Used for | Ends up in | License | File |
|---|---|---|---|---|
| [plugin-sdk](https://github.com/DK22Pac/plugin-sdk) | Access to the game | `EyeFinder92SA.asi` | zlib | [plugin-sdk.txt](plugin-sdk.txt) |
| Injector, part of plugin-sdk | Hooking game calls | `EyeFinder92SA.asi` | zlib | [injector.txt](injector.txt) |
| Hooking.Patterns, part of plugin-sdk | Finding code in the game | `EyeFinder92SA.asi` | MIT | [hooking-patterns.txt](hooking-patterns.txt) |
| [safetyhook](https://github.com/cursey/safetyhook), part of plugin-sdk | Replacing a game function | `EyeFinder92SA.asi` | Boost 1.0 | [safetyhook.txt](safetyhook.txt) |
| [Zydis](https://github.com/zyantific/zydis), part of safetyhook | Reading game code | `EyeFinder92SA.asi` | MIT | [zydis.txt](zydis.txt) |
| [JSON for Modern C++](https://github.com/nlohmann/json) | Reading and writing JSON files | `EyeFinder92SA.asi` | MIT | [nlohmann-json.txt](nlohmann-json.txt) |
| [CEF](https://github.com/chromiumembedded/cef) | The browser | `EyeFinderBrowser.exe` | BSD 3-Clause | [cef.txt](cef.txt) |

## Chromium

CEF itself isn't included with the mod: players download it from its official site. That download includes `CREDITS.html`, which lists the licenses of Chromium and everything it's built on.

## When you add a library

Add its license here, and to this table, in the same change.
