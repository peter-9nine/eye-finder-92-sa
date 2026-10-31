# Creating web pages for EyeFinder92SA: FAQ

Sites for EyeFinder92SA are ordinary web pages, shown by a real Chromium browser inside the game. If you can make a web page, you can make a site.

For sending emails and notifications from a mod, see [`EyeFinderApi.h`](EyeFinderApi.h) and the [README](../README.md).

## Getting started

### How do I add a site?

Create a folder named after the site's address inside the mod's `browser\sites` folder, and put an `index.html` in it:

```
modloader\EyeFinder92SA\browser\sites\
  www.mysite.com\
    index.html
    style.css
    images\logo.png
```

Restart the game, because new folders are only detected when the game starts. Then open the browser and type `www.mysite.com` in the address bar.

### What can I name it?

Use a lowercase name with at least one dot, such as `www.vinewoodinsider.com`. Your site replaces any real website with the same address, including when the player is connected to the real internet. So avoid real addresses unless that's what you want.

`http://` and `https://` both work, with no certificate to set up.

### What do I need to write the pages?

Any text editor. There's nothing to install or compile, and no special format.

### How do I see my changes?

Save the file and press **F5** in the in-game browser. New files in an existing site work right away too. You only need to restart the game for a new site folder.

### Can I work without starting the game?

Mostly, yes. Open your `index.html` in Chrome or Edge to design the page. Two things only work in-game:

- `sa.runScript()`, because the `sa` object doesn't exist in a normal browser.
- Addresses starting with `/`, such as `/style.css`, because the page isn't at the root of a site when opened as a file. Relative addresses such as `style.css` work in both places, so prefer them.

To keep your page working in both, check for `sa` first:

```js
if (window.sa) sa.runScript('hello');
```

## What pages can do

### What does the browser support?

It's a current version of Chromium, so modern HTML, CSS and JavaScript all work. Keep in mind:

| Feature | In this browser |
|---|---|
| `alert()`, `confirm()`, `prompt()` | Don't appear, so build your own with HTML |
| Right-click menu | Doesn't appear |
| File upload and download | Not supported or tested |
| Video and audio | Use WebM, Ogg or MP3, because H.264 and AAC (most `.mp4` files) don't play |
| New windows and `target="_blank"` | Open as a new tab |

### How big should my page be?

There's no fixed size, because it depends on the player's resolution. At 1280x720, the page area is about 944x588 pixels, and the player can maximize the window. Use a flexible layout, and test at more than one size. Long pages scroll normally.

### How do I make it look like 1992?

That's up to you, and it's all CSS: try simple tables, `Times New Roman`, beveled borders and tiled backgrounds. Avoid modern web fonts and rounded cards. `sites\www.eyefinder.info` is a small example.

### Can my page load things from the internet?

**Not by default.** The browser starts in local mode, where a page can only load from local sites. Files from the internet are blocked, including fonts, images, scripts and libraries such as jQuery from a CDN. Blocked files don't show any error to the player: your page just appears without them.

So **keep every file your page needs in your site's folder.**

### What happens when a page links to a real website?

The player sees a popup asking whether to connect to the real internet. If they decline, they stay on your page. Your page can't switch to the internet by itself, and can't skip the popup.

### Can my site talk to other local sites?

You can link to them, and show their images. JavaScript can't read data from another site, as on the normal web. Keep shared files in each site, or use a link.

### Can pages store data?

`localStorage` and cookies work, and they survive restarting the game. But **they aren't part of the player's saved game**: they're shared by every save, and loading an older save doesn't undo them. Don't use them for story progress.

Pages have no way to read or write the mod's save files.

### Can a page read the player's money, missions or email?

No, not yet. The only game feature pages have today is starting a CLEO script. Pages can't read anything from the game, and only the built-in mail site can read the inbox.

## CLEO scripts

### Can a button on my page run a CLEO script?

Yes. First, compile your script with Sanny Builder as usual, which gives you a `.cs` file. Then:

1. **Rename it from `.cs` to `.efs`.** See the next question for why.
2. Put it in a `scripts` folder in your site:

   ```
   www.mysite.com\
     index.html
     scripts\give_money.efs
   ```

3. Load `sa.js` in your page and call the script by name, with no extension or folder:

   ```html
   <script src="/_game/sa.js"></script>

   <button onclick="sa.runScript('give_money')">Claim your prize</button>
   ```

The player needs CLEO installed. It's been tested with CLEO 5.4.0.

### Why `.efs` and not `.cs`?

Modloader automatically runs every `.cs` file it finds in a mod folder when the game starts. If you leave it as `.cs`, your script runs at every game start, whether or not anyone clicks your button. The `.efs` extension avoids that. The contents are exactly the same compiled script.

### What are the rules for script names?

Letters, numbers, `-` and `_` only, up to 64 characters. A page can only start scripts in its own site's `scripts` folder, so there's no way to reach another folder.

### How do I know if it worked?

`sa.runScript()` returns a promise that resolves to `true` if the request was accepted, or `false` if the file wasn't found or the name isn't allowed:

```js
const accepted = await sa.runScript('give_money');
if (!accepted) status.textContent = 'Something went wrong.';
```

`true` only means the script was sent to the game. **It doesn't mean the script succeeded**, and no result comes back to the page. Successes and failures to start are written to `modloader\EyeFinder92SA\EyeFinder92SA.log`, and CLEO writes more detail to `cleo.log` in the game folder.

### Can I pass values to a script, or get values back?

No. A script starts with no parameters and can't send anything back to the page. To offer different actions, write a separate script for each.

### When does the script run?

On the game's next frame, usually right away. The game keeps running behind the browser, so:

- Effects happen immediately, such as money changing, but the player can't see them until they close the computer.
- Text your script shows on screen appears behind the browser, and may be gone by the time they close it. If the player needs to see it, have your script wait first. Tell the player to close the computer, then use a `wait` in the script.

### Can the player run it more than once?

Yes. Each click starts the script again, even if the previous run hasn't finished. Nothing is remembered between clicks. If something should only happen once, your page and your script have to handle that:

```js
button.onclick = () => {
  button.disabled = true;
  sa.runScript('give_money');
};
```

Reloading the page resets a button like this. For something that must truly happen once per saved game, keep track of it inside your CLEO script using CLEO's own save features.

### Can a real website run my scripts?

No. Only pages in the site's own folder can start its scripts, and the browser blocks requests from other addresses.

### Is this safe for players?

**A site with scripts should be treated like a mod.** A CLEO script can do anything the game can do, including editing memory, so players should only install sites from people they trust. If you distribute a site, say clearly that it includes scripts.

### Can a CLEO script send an email?

Yes, in principle, because the API is made of plain functions in `EyeFinder92SA.asi`. A script can use CLEO's `0AA2` (`LOAD_DYNAMIC_LIBRARY`), `0AA4` (`GET_DYNAMIC_LIBRARY_PROCEDURE`) and `0AA7` (`CALL_FUNCTION_RETURN`) to call `EF_SendEmail`. **This hasn't been tested yet.** The functions and their parameters are listed in [`EyeFinderApi.h`](EyeFinderApi.h), and all of them use the `cdecl` calling convention.

### What about scripts for email answers?

Those are for mods rather than pages. See `EF_SendEmailWithReplies` in [`EyeFinderApi.h`](EyeFinderApi.h). A script started this way isn't limited to the `.efs` extension, but the same warning applies: a `.cs` file inside a modloader folder also runs at every game start.

## Sharing your site

### How do players install it?

Have them copy your site folder into `modloader\EyeFinder92SA\browser\sites`, and restart the game.

### Will updating the mod delete my site?

It can. A new version of the mod may replace the `browser` folder, so keep your own copy of your site and reinstall it afterward if needed.

## Troubleshooting

### My site says "This page does not exist"

Check that:

- The folder name matches the address exactly, in lowercase.
- There's an `index.html` directly inside the folder, not in a subfolder.
- You restarted the game after creating the folder.

### Images or styles are missing

- Check that the file is in your site's folder and not on the internet.
- Check upper and lower case. Windows ignores case, but other browsers and tools may not, so it's a good habit to match exactly.
- Open your page in Chrome, and use its developer tools to find files that fail to load.

### My page looks fine in Chrome but not in the game

- It probably needs something from the internet, such as a font or library. Move it into your site's folder.
- Check how it looks at a small size, such as 944x588.

### `sa` is undefined

Add `<script src="/_game/sa.js"></script>` before your own script, and test in the game rather than in a normal browser.

### My script runs when the game starts

It's still named `.cs`. Rename it to `.efs`, and make sure the old `.cs` file is gone.

### My script doesn't run

1. Check `EyeFinder92SA.log` to see whether it started.
2. Check `cleo.log` for errors from CLEO.
3. Check that `sa.runScript()` returned `true`, and that the name doesn't include `.efs`.
4. Check that the script works when placed in the `cleo` folder as a normal `.cs` file.
5. Check that CLEO is installed.
