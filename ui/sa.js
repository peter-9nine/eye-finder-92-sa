// The library for local pages. It's served as /_game/sa.js on every local site.
(() => {
  const shuffle = (items) => {
    const list = [...items];
    for (let i = list.length - 1; i > 0; i--) {
      const j = Math.floor(Math.random() * (i + 1));
      [list[i], list[j]] = [list[j], list[i]];
    }
    return list;
  };

  const getJson = (path) => fetch(path, { cache: 'no-store' }).then((response) => response.json());

  let adsRequest = null;
  const ads = () => {
    adsRequest = adsRequest || getJson('/_game/ads.json').then((data) => data.ads);
    return adsRequest;
  };

  const sites = () => getJson('/_game/sites.json').then((data) => data.sites);

  const hostOf = (link) => {
    try {
      return new URL(link).hostname;
    }
    catch (error) {
      return '';
    }
  };

  function buildAd(ad) {
    const image = document.createElement('img');
    image.src = ad.image;
    image.alt = ad.text || 'Advertisement';
    image.style.maxWidth = '100%';
    image.style.display = 'block';
    image.style.margin = '0 auto';

    if (!ad.link) return image;

    const link = document.createElement('a');
    link.href = ad.link;
    link.title = ad.text || hostOf(ad.link);
    link.append(image);
    return link;
  }

  // Fills each element that has data-eyefinder-ad. A site never advertises itself, and an ad
  // isn't repeated on the same page while others remain.
  async function fillAds(root = document) {
    const slots = [...root.querySelectorAll('[data-eyefinder-ad]:not([data-eyefinder-filled])')];
    if (slots.length === 0) return;

    const available = shuffle(await ads()).filter((ad) => hostOf(ad.link) !== location.hostname);
    if (available.length === 0) return;

    const used = new Set(fillAds.used);
    for (const slot of slots) {
      const kind = slot.getAttribute('data-kind');
      const fits = kind ? available.filter((ad) => ad.kind === kind) : available;
      const pool = fits.length > 0 ? fits : available;

      // Those leading to another site go first: ads are what connects pages to each other.
      const linked = pool.filter((item) => item.link);
      const ad = linked.find((item) => !used.has(item.id)) || pool.find((item) => !used.has(item.id)) ||
        (linked.length > 0 ? linked : pool)[Math.floor(Math.random() * (linked.length > 0 ? linked : pool).length)];

      used.add(ad.id);
      slot.setAttribute('data-eyefinder-filled', '');
      slot.textContent = '';
      slot.append(buildAd(ad));
    }
    fillAds.used = [...used];
  }
  fillAds.used = [];

  function runScript(name) {
    return fetch('/_game/script/' + encodeURIComponent(name), { method: 'POST', headers: { 'X-EyeFinder': '1' } })
      .then((response) => response.ok);
  }

  const gameHeaders = { 'X-EyeFinder': '1' };
  const getGameJson = (path) => fetch(path, { cache: 'no-store', headers: gameHeaders }).then((response) => response.json());

  // What's happening in the game right now.
  const state = () => getGameJson('/_game/state.json');

  // Values shared with mods and CLEO scripts. They're always text.
  const values = () => getGameJson('/_game/values.json').then((data) => data.values);
  const get = (key, fallback = null) => values().then((all) => (key in all ? all[key] : fallback));

  // Whether the real internet is on, the kind of computer in use, and an id that changes with each use.
  const browser = () => getGameJson('/_game/browser.json');

  // Every word must appear somewhere. A match in the address or title ranks above one in the text.
  function rankMatches(list, query) {
    const words = query.toLowerCase().split(/\s+/).filter(Boolean);
    if (!words.length) return [];

    const results = [];
    for (const site of list) {
      const name = (site.domain + ' ' + site.title).toLowerCase();
      const details = (site.description + ' ' + site.keywords).toLowerCase();
      const text = (site.text || '').toLowerCase();
      if (!words.every((word) => name.includes(word) || details.includes(word) || text.includes(word))) continue;

      let score = 0;
      for (const word of words) score += name.includes(word) ? 4 : details.includes(word) ? 2 : 1;

      // What to show under the result: its description or, without one, the text around the match.
      let snippet = site.description;
      if (!snippet) {
        const at = text.indexOf(words[0]);
        if (at >= 0) snippet = (at > 40 ? '...' : '') + site.text.slice(Math.max(at - 40, 0), at + 110).trim() + '...';
      }
      results.push({ domain: site.domain, url: site.url, title: site.title, description: snippet || '', score });
    }
    return results.sort((a, b) => b.score - a.score);
  }

  // Searches the public sites folder.
  const searchSites = (query) => getGameJson('/_game/search.json').then((data) => rankMatches(data.sites, query));

  // Searches the hidden folder, and every hidden site if the query is '*'. It's only allowed on a
  // dark web computer, and returns null anywhere else.
  async function searchHidden(query) {
    const response = await fetch('/_game/hidden.json', { cache: 'no-store', headers: gameHeaders });
    if (!response.ok) return null;
    const data = await response.json();
    if (query === '*') return data.sites.map(({ domain, url, title, description }) => ({ domain, url, title, description }));
    return rankMatches(data.sites, query);
  }

  function set(key, value) {
    return fetch('/_game/value/' + encodeURIComponent(key), {
      method: 'POST',
      headers: { ...gameHeaders, 'X-EyeFinder-Value': encodeURIComponent(String(value)) },
    }).then((response) => response.ok);
  }

  window.sa = { runScript, sites, ads, fillAds, shuffle, state, values, get, set, browser, searchSites, searchHidden };

  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', () => fillAds());
  else fillAds();
})();
