# Known issues

Keep this list current: remove what gets fixed, and add what you find.

## Must fix before a release

- **Placeholder locations.** In `locations.json`, only the cyber cafe uses a known position. The three police stations and the dark web computer, at Zero's RC shop, are approximate and unverified. Stand at each real computer and correct them.
- **F8 is on by default.** It lets players open any session anywhere. See the [release checklist](release.md#release-checklist).
- **Chromium's sandbox is off.** See [Before shipping with real internet access](release.md#before-shipping-with-real-internet-access).
- **`include/GInputAPI.h` has no stated license.** It's the header GInput publishes for other mods to use, but neither it nor the copy here says what terms apply, so it isn't in `licenses`. Ask its author, or check GInput's own release, before making the repository public.
- **Placeholder content.** The police site, the two sample sites in `public/sites/hidden` and the recycle bin's files are placeholder text.

## Bugs

- **Shared values lose line breaks.** A value set by a page with a line break is stored with `%0A` in its place, because `CefURIDecode` in `HandleGameApi` doesn't decode control characters. Text fields aren't affected.

## Limitations

- **The controller can't close the browser directly.** L1 opens it at a computer, but closing means clicking Log Off.
- **Controller buttons depend on GInput's controls set.** Buttons are read from `CPad`'s fields, so a controls set that moves actions between fields can change which physical button does what. This hasn't been checked for every set.
- **The game also sees controller buttons outside the browser.** Holding D-pad left to open the pager can answer a conversation, and L1 keeps its normal effect.
- **A resolution change loses the open tabs,** because the helper restarts.
- **Recycle bin contents are per session type,** not per computer, so all police stations share one.
- **The recycle bin blanks the page behind it,** because tabs are drawn over the shell.
- **Search only reads each site's `index.html`,** and its first 4000 bytes of text. It can't read text in images, or other pages of a site, so `keywords` in `site.json` is the way around that.
- **The dark web's intro can replay** if the helper restarts during a session, such as after a resolution change, because that sends the session again.
- **Mission locks do nothing yet.** `tools/import_old_pages.py` keeps old mission locks in `site.json` as `unlockAfter`, but nothing reads them.
