"""Convert the old image-based EyeFinder92 pages and ads into public sites and ads.

    python tools/import_old_pages.py <old mod folder> <new mod folder>

It only reads the old folder. In the new one, it writes to sites and ads, replacing what it
wrote before. Each page becomes an HTML page with the same image, and each clickable box
becomes a link or an ad slot.

Not converted, because the new mod has no equivalent yet: mission locks (they're kept in
site.json as "unlockAfter", but nothing reads them), news text, and money buttons.
"""
import html
import json
import pathlib
import re
import shutil
import sys

# Sites that now belong to the mod itself.
SYSTEM_DOMAINS = {"www.eyefinder.info"}
AD_FOLDERS = {"tiles": "tile", "banners": "banner", "airline": "airline", "police": "police"}
AD_SLOT_KINDS = {1: "tile", 2: "banner", 3: "airline", 4: "police", 5: "news"}
IMAGE_TYPES = {".png", ".jpg", ".jpeg", ".gif", ".webp"}


def number(box, name):
    return float(box.get(name, 0) or 0)


def read_boxes(folder):
    path = folder / "boxes.json"
    if not path.exists():
        return []
    try:
        # utf-8-sig: some of the old files start with a BOM.
        return json.loads(path.read_text(encoding="utf-8-sig")).get("boxes", [])
    except (ValueError, UnicodeDecodeError) as error:
        print(f"  skipping the boxes of {folder}: {error}")
        return []


def local_target(action_data, domains):
    """Turn an old link into a new address, or None if that site doesn't exist."""
    target = (action_data or "").replace("\\", "/").strip()
    target = re.sub(r"^https?://", "", target)
    target = re.sub(r"^hidden/", "", target)
    domain = target.split("/")[0].lower()
    if domain not in domains:
        return None
    return "https://" + target.rstrip("/") + "/"


def hover_style(left, top, width, height):
    """Show the matching part of hover.png, sized to the whole page."""
    if width <= 0 or height <= 0:
        return ""
    size_x = 100 / width
    size_y = 100 / height
    position_x = 0 if width >= 1 else left / (1 - width) * 100
    position_y = 0 if height >= 1 else top / (1 - height) * 100
    return (
        f"--hover-size:{size_x:.3f}% {size_y:.3f}%;"
        f"--hover-position:{position_x:.3f}% {position_y:.3f}%;"
    )


def build_page(folder, title, domains, stats):
    has_hover = (folder / "hover.png").exists()
    parts = []

    for box in read_boxes(folder):
        left, top = number(box, "x"), number(box, "y")
        width, height = number(box, "w"), number(box, "h")
        area = f"left:{left * 100:.3f}%;top:{top * 100:.3f}%;width:{width * 100:.3f}%;height:{height * 100:.3f}%;"

        kind = int(box.get("kind", 0) or 0)
        if kind in AD_SLOT_KINDS:
            parts.append(f'<div class="ad" data-eyefinder-ad data-kind="{AD_SLOT_KINDS[kind]}" style="{area}"></div>')
            stats["ad slots"] += 1
            continue

        action = box.get("action", "")
        if action != "ACTION_LINK":
            if action in ("ACTION_ADD_MONEY", "ACTION_SUB_MONEY"):
                stats["money buttons skipped"] += 1
            continue

        target = local_target(box.get("actionData"), domains)
        if not target:
            stats["links to missing sites"] += 1
            continue

        style = area + (hover_style(left, top, width, height) if has_hover and box.get("hover") else "")
        hover_class = " hover" if has_hover and box.get("hover") else ""
        parts.append(f'<a class="area{hover_class}" href="{html.escape(target)}" style="{style}"></a>')
        stats["links"] += 1

    areas = "\n    ".join(parts)
    return f"""<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<title>{html.escape(title)}</title>
<style>
  body {{ margin: 0; background: #fff; }}
  .page {{ position: relative; }}
  .page img.base {{ display: block; width: 100%; }}
  .area, .ad {{ position: absolute; display: block; overflow: hidden; }}
  .ad img {{ width: 100%; height: 100%; max-width: none !important; object-fit: fill; }}
  .hover:hover {{ background-image: url("hover.png"); background-size: var(--hover-size); background-position: var(--hover-position); }}
</style>
</head>
<body>
  <div class="page">
    <img class="base" src="normal.png" alt="">
    {areas}
  </div>
  <script src="/_game/sa.js"></script>
</body>
</html>
"""


def page_folders(root):
    """The site's own folder and any nested subpages that have an image."""
    for path in sorted(root.rglob("normal.png")):
        yield path.parent


# How the known sites appear on the EyeFinder home page.
KNOWN_SITES = {
    "www.burger-shot.lc.com": ("Burger Shot", "Meat, on the go. Order fast food the fast way."),
    "www.cluckinbellhappychicken.com": ("Cluckin' Bell", "Our chickens don't die in vain. Guaranteed!"),
    "www.dragonbrainthemovie.com": ("Dragon Brain: The Movie", "The action event of the year."),
    "www.epsilonprogram.com": ("The Epsilon Program", "You are not alone. Find out what you've been missing."),
    "www.lootandwank.com": ("Loot & Wank", "A very special kind of shopping."),
    "www.pokerintherear.com": ("Poker in the Rear", "Seriously, who wouldn't want to play?"),
    "www.sprunksoda.com": ("Sprunk", "The essence of life. Detonate the taste explosion."),
    "www.victimstores.com": ("Victim", "Fashion for people who've got it all taken away."),
    "www.vinewoodinsider.com": ("Vinewood Insider", "All the gossip from the hills of Vinewood."),
    "www.vinylcountdown.com": ("Vinyl Countdown", "The very best records in San Andreas."),
    "www.westcoastraplegends.com": ("West Coast Rap Legends", "The sound of the streets, from the people who lived it."),
}


def pretty_title(domain):
    if domain in KNOWN_SITES:
        return KNOWN_SITES[domain][0]
    name = re.sub(r"^www\.", "", domain)
    name = re.sub(r"\.[a-z.]+$", "", name)
    return re.sub(r"[-_.]+", " ", name).title()


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)

    old = pathlib.Path(sys.argv[1])
    new = pathlib.Path(sys.argv[2])
    pages = old / "pages"
    if not pages.is_dir():
        sys.exit(f"No pages folder in {old}")

    # Hidden sites are real sites, just not listed on the home page.
    site_roots = {}
    for folder in sorted(pages.iterdir()):
        if folder.is_dir() and folder.name != "hidden" and folder.name.lower() not in SYSTEM_DOMAINS:
            site_roots[folder.name.lower()] = (folder, True)
    hidden = pages / "hidden"
    if hidden.is_dir():
        for folder in sorted(hidden.iterdir()):
            if folder.is_dir():
                site_roots[folder.name.lower()] = (folder, False)

    domains = set(site_roots) | SYSTEM_DOMAINS
    stats = {"sites": 0, "pages": 0, "links": 0, "ad slots": 0, "ads": 0, "links to missing sites": 0, "money buttons skipped": 0}

    for domain, (root, listed) in site_roots.items():
        target_root = new / "sites" / domain
        title = pretty_title(domain)
        converted = 0

        for folder in page_folders(root):
            target = target_root / folder.relative_to(root)
            target.mkdir(parents=True, exist_ok=True)
            for name in ("normal.png", "hover.png"):
                if (folder / name).exists():
                    shutil.copyfile(folder / name, target / name)
            (target / "index.html").write_text(build_page(folder, title, domains, stats), encoding="utf-8")
            converted += 1

        if converted == 0:
            print(f"  {domain}: no pages, skipped")
            continue

        details = {"title": title, "description": KNOWN_SITES.get(domain, ("", ""))[1], "listed": listed}
        meta = root / "meta.json"
        if meta.exists():
            try:
                unlock = json.loads(meta.read_text(encoding="utf-8-sig")).get("unlock_after_mission")
                if unlock:
                    details["unlockAfter"] = unlock
            except ValueError:
                pass
        (target_root / "site.json").write_text(json.dumps(details, indent=2), encoding="utf-8")

        stats["sites"] += 1
        stats["pages"] += converted
        print(f"  {domain}: {converted} pages{'' if listed else ' (hidden)'}")

    # Ads: the old file name holds the link, with "#" standing for "\".
    for folder_name, kind in AD_FOLDERS.items():
        folder = old / "files" / folder_name
        if not folder.is_dir():
            continue
        for image in sorted(folder.iterdir()):
            if image.suffix.lower() not in IMAGE_TYPES:
                continue

            link_text = image.stem.lstrip("#").replace("#", "/")
            link = local_target(link_text, domains) if link_text.lower().startswith("www.") else None
            safe_name = re.sub(r"[^A-Za-z0-9_-]+", "-", image.stem).strip("-").lower() or "ad"

            target = new / "ads" / f"{kind}-{safe_name}"
            target.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(image, target / ("image" + image.suffix.lower()))
            details = {"image": "image" + image.suffix.lower(), "kind": kind}
            if link:
                details["link"] = link
            (target / "ad.json").write_text(json.dumps(details, indent=2), encoding="utf-8")
            stats["ads"] += 1

    print("Imported:", ", ".join(f"{count} {name}" for name, count in stats.items()))


if __name__ == "__main__":
    main()
