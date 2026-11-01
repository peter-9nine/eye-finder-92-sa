# Creating web pages for EyeFinder92SA: FAQ

Sites for EyeFinder92SA are ordinary web pages, shown by a real Chromium browser inside the game. If you can make a web page, you can make a site.

For sending emails and notifications from a mod, see [`EyeFinderApi.h`](EyeFinderApi.h) and the [README](../README.md).

## Getting started

### How do I add a site?

Create a folder named after the site's address inside the mod's public `sites` folder, and put an `index.html` in it:

```
modloader\EyeFinder92SA\sites\
  www.mysite.com\
    index.html
    site.json
    style.css
    images\logo.png
```

Don't use `system\sites`. That folder holds the mod's own pages, such as the EyeFinder home page and mail, and it's replaced when the mod updates.

Restart the game, because new folders are only detected when the game starts. Then open the browser and type `www.mysite.com` in the address bar.

### What can I name it?

Use a lowercase name with at least one dot, such as `www.vinewoodinsider.com`. Your site replaces any real website with the same address, including when the player is connected to the real internet. So avoid real addresses unless that's what you want. The mod's own sites, such as `www.eyefinder.info`, can't be replaced.

### How does my site get listed on the EyeFinder home page?

Automatically. Every public site is listed, in a new random order on each visit, with ads between them. Add a `site.json` to control how yours appears:

```json
{
  "title": "Binco",
  "description": "Discount clothes for the whole family."
}
```

Without it, the site is listed by its address. To keep a site secret, so that it's reached only by typing its address, add `"listed": false`. It then stays out of both the list and search.

### How do players find my site with search?

The search box on the EyeFinder home page looks through every listed public site, in its address, title, description and the text of its `index.html`. An address bar search goes there too. A search only finds a site if all of its words match.

Add `keywords` to your `site.json` for words that aren't on the page:

```json
{
  "title": "Binco",
  "description": "Discount clothes for the whole family.",
  "keywords": "shirts pants fashion ganton"
}
```

Text drawn inside images can't be found, so use `keywords` for image-based pages. Your own pages can search too, with `sa.searchSites('words')`, which returns the matches with the best first.

## Ads

### How do I show an ad on my page?

Add these two lines:

```html
<div data-eyefinder-ad></div>
<script src="/_game/sa.js"></script>
```

The empty `div` becomes a random ad from the shared ads folder, linked to another site. This is what connects the sites into one ecosystem: someone visiting one site finds their way to the next. Your site's own ads are never shown on your site, ads with a link are shown before ads without one, and an ad isn't repeated on a page unless it runs out of others.

Size the `div` with your own CSS. To ask for a particular type of ad, add `data-kind`:

```html
<div data-eyefinder-ad data-kind="banner"></div>
```

If no ad of that kind exists, any other ad is used.

### How do I create an ad?

Create a folder in the mod's public `ads` folder, with an image and an `ad.json`:

```
modloader\EyeFinder92SA\ads\
  banner-mysite\
    banner.png
    ad.json
```

```json
{
  "image": "banner.png",
  "link": "https://www.mysite.com/",
  "kind": "banner",
  "text": "Visit My Site"
}
```

| Field | Meaning |
|---|---|
| `image` | The image file, in the same folder |
| `link` | Where it goes. Without it, the ad is just a picture |
| `kind` | Optional. Any word you like; `banner` is used by the home page, at about 468x60 pixels |
| `text` | Optional. Shown when hovering, and used if the image can't load |

New ads appear right away, with no need to restart the game.

### Can I build my own ad layout?

Yes. `sa.ads()` returns the list of ads and `sa.sites()` the list of public sites, each as a promise, so you can build your own directory or ad rotation. If you add ad slots after the page has loaded, call `sa.fillAds()`.

`http://` and `https://` both work, with no certificate to set up.

### What do I need to write the pages?

Any text editor. There's nothing to install or compile, and no special format.

### How do I see my changes?

Save the file and press **F5** in the in-game browser. New files in an existing site work right away too. You only need to restart the game for a new site folder.

### Can I work without starting the game?

Mostly, yes. Open your `index.html` in Chrome or Edge to design the page. Two things only work in-game:

- `sa.runScript()` and ads, because the `sa` object doesn't exist in a normal browser.
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

There's no fixed size, because it depends on the player's resolution. At 1280x720, the page area is about 944x586 pixels, and the player can maximize the window. Players can also zoom a page, with the buttons in the status bar, Ctrl and `+` or `-`, or Ctrl and the mouse wheel. Use a flexible layout, and test at more than one size. Long pages scroll normally. With a controller, players point with a stick, and clicking a text field opens an on-screen keyboard. The field only changes when they press Go, which also sends Enter, unless it's a text area or another field in the form is still empty. So your page gets the whole text at once, and it should have a visible submit button for forms with more than one field.

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

### Can a page read the player's inbox?

No. Only the mod's own mail site can read it. A public site named `mail.eyefinder.info` wouldn't get access either.

## Talking to the game

### Can a page know what's happening in the game?

Yes. Load `sa.js`, then call `sa.state()` whenever you need it:

```html
<script src="/_game/sa.js"></script>
<script>
  async function update() {
    const game = await sa.state();
    document.getElementById('cash').textContent = '$' + game.money;
  }
  update();
  setInterval(update, 1000);
</script>
```

| Field | Meaning |
|---|---|
| `inGame` | `true` once a game is running |
| `money` | CJ's money |
| `health`, `armor` | CJ's health and armor |
| `wantedLevel` | Wanted stars, 0 to 6 |
| `day`, `hour`, `minute` | Days passed in the game, and the game clock |
| `x`, `y`, `z` | CJ's position |
| `zone` | The area's name, such as `Ganton` |
| `interior` | The interior number, or 0 when outside |
| `inVehicle`, `onMission` | `true` or `false` |
| `weather` | The game's weather number |
| `lastMission` | Internal name of the last mission passed, such as `SWEET_1`. It can be empty. |

It's read-only, updates about five times a second, and works in every local site. This makes things like a mission lock easy: compare `lastMission` in your page and hide what isn't available yet. [`MissionOrder.txt`](MissionOrder.txt) lists the missions in story order.

### What if I need something that isn't in that list?

Use a CLEO script and shared values. Your script reads anything it can from the game and writes it to a value, and your page reads that value. See the next question.

### What are shared values?

Named pieces of text that pages, CLEO scripts and mods can all read and write. **They're saved with the player's game**, in that save's file in `save data`, so they're the right place for story progress.

From a page:

```js
await sa.set('mymod.visited', 'yes');
const visited = await sa.get('mymod.visited', 'no');   // 'no' is used if it doesn't exist
const everything = await sa.values();
```

- Keys use letters, numbers, `.`, `_` and `-`, up to 63 characters. Start yours with a prefix, such as `mymod.`, because every site and mod shares the same values.
- Values are always text, up to 1023 bytes. Use `Number(value)` in a page when you need a number.
- A value set from a page reaches scripts and mods on the game's next frame. It can take about a quarter of a second to appear in `sa.get()`.

### How does a page send data to a CLEO script, and get a result back?

With shared values. Set the input, run the script, then watch for the result:

```js
await sa.set('casino.bet', 500);
await sa.set('casino.result', '');
await sa.runScript('casino_spin');

const timer = setInterval(async () => {
  const result = await sa.get('casino.result', '');
  if (result) {
    clearInterval(timer);
    show(result);
  }
}, 300);
```

The value is always available to the script before it starts. Your script reads `casino.bet` and writes `casino.result`, as in the next question. Clear the result first, as above, because values are saved and the last result is still there.

### How does a CLEO script read and write shared values?

Through the functions in `EyeFinder92SA.asi`, using CLEO's DLL opcodes. This is `examples\ApiDemo\values_test.txt`, which has been run in the real game with CLEO 5.4.0:

```
0AA2: 0@ = load_dynamic_library "EyeFinder92SA.asi"
0AA4: 1@ = get_proc_address "EF_GetValueInt" library 0@
0AA4: 2@ = get_proc_address "EF_SetValueInt" library 0@
0AA4: 3@ = get_proc_address "EF_SetValue" library 0@

// value = EF_GetValueInt("casino.bet", 0)
0AA7: call_function_return 1@ num_params 2 pop 2 0 "casino.bet" 4@

// EF_SetValueInt("casino.winnings", value)
0AA7: call_function_return 2@ num_params 2 pop 2 4@ "casino.winnings" 5@

// EF_SetValue("casino.result", "You won!")
0AA7: call_function_return 3@ num_params 2 pop 2 "You won!" "casino.result" 5@
```

Three details matter:

- **Arguments go in reverse order**, so the function's last parameter comes first.
- **`num_params` and `pop` are both the number of arguments.**
- **The last variable receives the return value**, even if you don't need it.

Scripts that keep running can call these as often as they like, for example to publish a value every second.

### Can a script that's always running react to a page?

Yes. Run your script as a normal `.cs` in the `cleo` folder, have it check a value in a loop, and have your page set that value. This way the page doesn't need `sa.runScript()` at all.

### What can't pages do?

They can't change the game directly: `sa.state()` is read-only. To change something, such as giving money, have your page run a CLEO script, or call a C++ function from your own mod. See [C++ functions](#c-functions).

## C++ functions

### Can my page run C++ code?

Yes, through a mod of yours. The C++ doesn't run inside the page. Your mod registers a function, and your page asks the game to run it and waits for the answer.

In your mod, using [`EyeFinderApi.h`](EyeFinderApi.h):

```cpp
#include <string>
#include "plugin.h"
#include "EyeFinderApi.h"

const char* __cdecl BuyCar(const char* args, void* user) {
    static std::string answer;
    answer = "You asked for model " + std::string(args);
    return answer.c_str();
}

class MyMod {
public:
    MyMod() {
        plugin::Events::initRwEvent += [] {
            eyefinder::RegisterFunction("www.mysite.sa", "buyCar", BuyCar);
        };
    }
} myMod;
```

In your page:

```html
<script src="/_game/sa.js"></script>
<script>
  async function buy() {
    try {
      const answer = await sa.call('buyCar', '411');
      document.getElementById('result').textContent = answer;
    }
    catch (error) {
      document.getElementById('result').textContent = error.message;
    }
  }
</script>
```

`examples/ApiDemo` in the project is a complete example.

### Where does my `.asi` go?

Anywhere in the `modloader` folder, because modloader loads every `.asi` it finds there. It can sit inside your site, or belong to a separate mod that happens to have a site too:

```
modloader\EyeFinder92SA\sites\www.mysite.sa\
  index.html
  mysite.asi
```

```
modloader\MyBigMod\
  mybigmod.asi
modloader\EyeFinder92SA\sites\www.mysite.sa\
  index.html
```

A mod can register functions for more than one site. Your function's site only has to match by name, so the `.asi` and the site don't need to know where the other one is.

### When should I register?

Once the game has started, not from `DllMain` or a global constructor, because EyeFinder92SA may load after your mod. `initRwEvent`, as above, works. If EyeFinder92SA isn't installed, `RegisterFunction` does nothing and returns 0, so your mod still runs without it.

### What do my function's arguments and answer look like?

Text, in both directions, up to 8191 bytes each.

- **Sending:** a string is sent as it is, and anything else is sent as JSON, so `sa.call('buyCar', { model: 411 })` gives your function `{"model":411}`. Leaving out the argument gives it empty text.
- **Answering:** `sa.call()` always gives your page text, and if your function returns `NULL`, it's empty. To answer with JSON, return JSON text and use `JSON.parse()` in your page.
- **Keeping it valid:** your text is copied as soon as your function returns, so a `static std::string` as above is enough. Never return a pointer to a local variable.

For anything larger than that, your function can write a file into your site's folder, for the page to load afterwards.

### When does my function run?

On the game thread, on the next frame. So your function can safely use the game: give money, spawn a car or read anything it likes. Two things follow from this:

- **The game waits for your function.** Keep it short. Long work belongs on your own thread.
- **The page waits too.** `sa.call()` takes at least until the next frame, and answers in the order calls arrive.

If a page stores a value with `sa.set()` and then calls a function, the value is there when the function runs.

### What happens when something goes wrong?

The promise from `sa.call()` rejects, so use `try` and `catch`. `error.message` says which of these happened:

| Problem | Likely cause |
|---|---|
| No mod has registered this function for this site | The `.asi` isn't installed or loaded, or the domain or name is misspelled. The name is case-sensitive |
| The function name or its arguments are invalid or too long | A name with other characters, or arguments over 8191 bytes |
| The answer is too long | Your function returned over 8191 bytes. It ran anyway |
| The function failed | A C++ exception left your function. It's also written to `EyeFinder92SA.log` |
| The game is too busy | Too many calls at once, even after several tries |
| The game did not answer in time | No answer within 10 seconds, for example with the pause menu open. **Your function may still run afterwards** |

Handling "the page got no answer" matters for things like payments: if your function must only run once, keep track of that in your mod, not in the page.

### What can't a function's page do?

- **Another site's functions are out of reach.** Each function belongs to one site, and the address of the page making the call decides which site is calling. A page can't claim to be another site.
- **Real websites can't call anything,** and neither can other local sites, for the same reason they can't reach the rest of the game API.
- **The call must come from a normal page or frame.** Calls from a service worker or an `<iframe srcdoc>` are refused.

### Is this safe for players?

**A site with an `.asi` is a mod,** and can do anything on the player's computer, like any other `.asi`. Say clearly that your site includes one, and players should only install it from people they trust.

Your function's arguments can hold anything a player typed into your page, so check them as you would any other input before using them, and don't assume they fit in a fixed buffer. A crash in your function takes the whole game down, because it runs inside the game.

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

`true` only means the script was sent to the game. **It doesn't mean the script succeeded.** To get a result back, use shared values. Successes and failures to start are written to `modloader\EyeFinder92SA\EyeFinder92SA.log`, and CLEO writes more detail to `cleo.log` in the game folder.

### Can I pass values to a script, or get values back?

Not directly, because a script starts with no parameters. Use shared values instead, as described in [Talking to the game](#talking-to-the-game).

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

Reloading the page resets a button like this. For something that must truly happen once per saved game, keep track of it with a shared value, which is saved with the game:

```js
if ((await sa.get('mysite.prize_claimed', 'no')) === 'no') {
  await sa.set('mysite.prize_claimed', 'yes');
  await sa.runScript('give_money');
}
```

### Can a real website run my scripts?

No. Only pages in the site's own folder can start its scripts, and requests from any other address are refused.

### Is this safe for players?

**A site with scripts should be treated like a mod.** A CLEO script can do anything the game can do, including editing memory, so players should only install sites from people they trust. If you distribute a site, say clearly that it includes scripts.

### Can a CLEO script send an email?

Yes, in the same way as shared values, with the function's arguments in reverse order. For example, for `EF_SendEmail(from, subject, body, notify)`:

```
0AA4: 1@ = get_proc_address "EF_SendEmail" library 0@
0AA7: call_function_return 1@ num_params 4 pop 4 1 "Meet me at the spot." "You in?" "Ryder" 2@
```

The method has been tested with the shared value functions, but this exact call hasn't been run yet. The functions and their parameters are listed in [`EyeFinderApi.h`](EyeFinderApi.h), and all of them use the `cdecl` calling convention. A single call can pass at most 5 text arguments, so `EF_SendEmailWithReplies` can't be fully used from a script with literals.

### What about scripts for email answers?

Those are for mods rather than pages. See `EF_SendEmailWithReplies` in [`EyeFinderApi.h`](EyeFinderApi.h). A script started this way isn't limited to the `.efs` extension, but the same warning applies: a `.cs` file inside a modloader folder also runs at every game start.

## Sharing your site

### Can I make a site that only works from certain computers?

Yes. Add `session` to your `site.json`, with `pd` for police computers or `dark_web` for dark web computers:

```json
{
  "title": "Secret Site",
  "session": "dark_web"
}
```

From any other computer, every page and file of your site shows "Connection refused", even if the player types the address. The site is also left out of the home page's list and search, so tell players the address some other way, such as an email or a link on another restricted site.

### How do I put a site on the dark web?

Put its folder inside a folder called `hidden`:

```
modloader\EyeFinder92SA\sites\hidden\www.mysite.onion\
  index.html
  site.json
```

It works like any other site, except that it can only be reached on a dark web computer, without needing `session` in its `site.json`. The dark web's home page, `hidden.onion`, searches this folder the same way EyeFinder searches the public one, and `keywords` works here too. Typing `ls` there lists everything in the folder, so nothing in it is truly secret from someone at a dark web computer.

On a dark web computer, your own pages can search with `sa.searchHidden('words')`. Anywhere else it returns `null`.

### How do players install it?

Have them copy your site folder into `modloader\EyeFinder92SA\sites`, and any ad folders into `modloader\EyeFinder92SA\ads`, then restart the game.

### Will updating the mod delete my site?

No. Updates only replace the `system` folder and the browser program, and public sites and ads live outside them.

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
