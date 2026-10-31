// La biblioteca para las páginas locales. Se sirve como /_game/sa.js en todos los sitios locales.
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

  // Rellena cada elemento con data-eyefinder-ad. Un sitio nunca se anuncia a sí mismo, y no se
  // repite un anuncio en la misma página mientras queden otros.
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

      // Primero los que llevan a otro sitio: los anuncios son lo que conecta unas páginas con otras.
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

  // Lo que está pasando en el juego ahora mismo.
  const state = () => getGameJson('/_game/state.json');

  // Valores compartidos con los mods y los scripts de CLEO. Siempre son texto.
  const values = () => getGameJson('/_game/values.json').then((data) => data.values);
  const get = (key, fallback = null) => values().then((all) => (key in all ? all[key] : fallback));

  function set(key, value) {
    return fetch('/_game/value/' + encodeURIComponent(key), {
      method: 'POST',
      headers: { ...gameHeaders, 'X-EyeFinder-Value': encodeURIComponent(String(value)) },
    }).then((response) => response.ok);
  }

  window.sa = { runScript, sites, ads, fillAds, shuffle, state, values, get, set };

  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', () => fillAds());
  else fillAds();
})();
