// Draws one page at a time, chosen by the address, so the browser's back button works everywhere:
// #home, #records, #results/<search>, #records/<id>, #wanted, #dispatch, #evidence, #bulletins, #crash.
// Search text comes from whoever is typing, so everything goes in as text, never as HTML.
(() => {
  const content = document.getElementById('content');
  const people = window.SAPD_PEOPLE;
  const bulletins = window.SAPD_BULLETINS;

  const el = (tag, props = {}, ...children) => {
    const node = Object.assign(document.createElement(tag), props);
    node.append(...children);
    return node;
  };
  const link = (href, className, ...children) => el('a', { href, className }, ...children);
  const dollars = (amount) => '$' + amount.toLocaleString('en-US');
  const stars = (level) => '★'.repeat(level);

  // --- Photos: photos/<id>.jpg if there's one, and a silhouette until then ---
  function silhouette() {
    const NS = 'http://www.w3.org/2000/svg';
    const svg = document.createElementNS(NS, 'svg');
    svg.setAttribute('viewBox', '0 0 80 100');
    svg.setAttribute('preserveAspectRatio', 'xMidYMax meet');
    svg.setAttribute('aria-hidden', 'true');
    const figure = document.createElementNS(NS, 'path');
    figure.setAttribute('d', 'M40 16c-10 0-17 8-17 19 0 8 4 15 9 19v7c-13 3-23 9-27 17v22h70V78c-4-8-14-14-27-17v-7c5-4 9-11 9-19 0-11-7-19-17-19z');
    figure.setAttribute('fill', '#737a82');
    svg.append(figure);
    return svg;
  }

  function photo(person) {
    const frame = el('span', { className: 'photo' }, silhouette());
    const image = new Image();
    image.alt = person.name;
    image.onload = () => frame.replaceChildren(image);
    image.src = 'photos/' + person.id + '.jpg';
    return frame;
  }

  // --- The game ---
  let game = null;
  const refreshGame = () => sa.state().then((state) => { game = state; }).catch(() => {});
  const playerIsWanted = () => !!game && game.wantedLevel > 0;
  const lastSeen = () => `Last reported in ${(game && game.zone) || 'an unknown location'}`;

  const STATUS = {
    wanted: 'Wanted',
    clear: 'No warrants',
    officer: 'Officer',
    sealed: 'Sealed',
    removed: 'Removed',
  };
  const statusOf = (person) => (person.isPlayer && playerIsWanted() ? 'wanted' : person.status);
  const statusBadge = (person) => {
    const status = statusOf(person);
    return el('span', { className: 'status status-' + status }, STATUS[status]);
  };

  // Who's wanted, with the player first and their reward growing with the stars.
  function wantedList() {
    const list = people.filter((person) => person.status === 'wanted')
      .map((person) => ({ person, wantedFor: person.wantedFor, reward: person.reward, level: 0 }));
    if (playerIsWanted()) {
      list.unshift({ person: people.find((person) => person.isPlayer), wantedFor: lastSeen(), reward: game.wantedLevel * 1000, level: game.wantedLevel });
    }
    return list;
  }
  const shownName = (person) => person.aliases[0] || person.name;

  // --- Home ---
  function showHome() {
    const wanted = wantedList().slice(0, 3).map((entry) => el('li', {},
      link('#records/' + entry.person.id, '', el('span', {}, shownName(entry.person)), el('span', { className: 'reward' }, dollars(entry.reward)))));
    const latest = bulletins.slice(0, 3).map((memo) => el('li', {}, link('#bulletins', '', memo.title)));

    const seal = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
    seal.setAttribute('class', 'doorSeal');
    seal.setAttribute('aria-hidden', 'true');
    const use = document.createElementNS('http://www.w3.org/2000/svg', 'use');
    use.setAttribute('href', '#seal');
    seal.append(use);

    content.append(el('div', { className: 'home' },
      el('section', { className: 'door' },
        el('span', { className: 'unitNumber', ariaHidden: 'true' }, '14'),
        el('span', { className: 'doorSeam' }),
        seal,
        el('div', { className: 'doorWords' },
          el('h1', {}, 'POLICE'),
          el('p', { className: 'doorDepartment' }, 'San Andreas Police Department'),
          el('p', { className: 'doorMotto' }, 'To protect and to serve'))),
      el('div', { className: 'homeColumns' },
        el('section', { className: 'welcome' },
          el('h2', {}, 'Welcome, officer'),
          el('p', {}, 'This is the department\'s network, for sworn officers and cleared staff. Look up anyone on file, see who\'s wanted, and follow the day\'s calls.'),
          el('p', {}, 'Reminder from the Chief\'s office: what you do on this terminal is logged, unless you\'re in C.R.A.S.H.'),
          link('#records', 'primaryButton', 'Search records')),
        el('div', {},
          el('section', { className: 'homeSection' }, el('h2', {}, 'Most wanted'), el('ul', { className: 'homeList' }, ...wanted)),
          el('section', { className: 'homeSection' }, el('h2', {}, 'Latest bulletins'), el('ul', { className: 'homeList' }, ...latest))))));
  }

  // --- Records ---
  const LOOKING_SHORTEST_MS = 900;
  const LOOKING_EXTRA_MS = 700;
  let lookingFor = null;  // A search just sent, which waits. Going back to a list doesn't.
  let lookingTimer = 0;
  let lastSearch = '';

  const collator = new Intl.Collator('en');
  const byName = [...people].sort((a, b) => collator.compare(a.name, b.name));

  function matches(query) {
    const words = query.toLowerCase().split(/\s+/).filter(Boolean);
    return byName.filter((person) => {
      const text = [person.name, ...person.aliases, person.gang, person.wantedFor, person.hangout, ...(person.charges || []).map((charge) => charge.offense)].join(' ').toLowerCase();
      return words.every((word) => text.includes(word));
    });
  }

  function searchForm(query) {
    const input = el('input', { type: 'search', value: query, autocomplete: 'off', spellcheck: false, placeholder: 'Name, alias, gang or crime', ariaLabel: 'Name, alias, gang or crime' });
    const form = el('form', { className: 'searchForm' }, input, el('button', { className: 'primaryButton' }, 'Search'));
    form.addEventListener('submit', (event) => {
      event.preventDefault();
      const text = input.value.trim();
      if (!text) {
        input.focus();
        return;
      }
      lookingFor = text;
      const hash = '#results/' + encodeURIComponent(text);
      // Searching for the same thing again doesn't change the address, so nothing else would redraw.
      if (location.hash === hash) route();
      else location.hash = hash;
    });
    return { form, input };
  }

  function showSearch() {
    const { form, input } = searchForm('');
    content.append(
      el('h1', { className: 'pageTitle' }, 'Records'),
      el('p', { className: 'pageNote' }, 'Search everyone on file, then select a name to open the record.'),
      form);
    input.focus();
  }

  function resultRow(person) {
    const detail = [person.aliases.join(', '), person.gang === 'None' ? '' : person.gang].filter(Boolean).join(', ');
    return link('#records/' + person.id, 'result',
      photo(person),
      el('span', { className: 'resultText' },
        el('span', { className: 'resultName' }, person.name),
        el('span', { className: 'resultDetail' }, detail)),
      statusBadge(person));
  }

  function showResultList(area, query) {
    const found = matches(query);
    if (!found.length) {
      area.replaceChildren(el('p', { className: 'noMatch' }, `No record for "${query}". Check the spelling, or try an alias or a gang.`));
      return;
    }
    area.replaceChildren(
      el('p', { className: 'resultCount' }, found.length === 1 ? '1 person found' : `${found.length} people found`),
      el('div', { className: 'results' }, ...found.map(resultRow)));
  }

  function showResults(query) {
    lastSearch = query;
    const area = el('div', {});
    content.append(el('h1', { className: 'pageTitle' }, 'Records'), el('p', { className: 'pageNote' }, 'Select a name to open the record.'), searchForm(query).form, area);

    if (lookingFor !== query) {
      showResultList(area, query);
      return;
    }
    lookingFor = null;
    area.append(el('div', { className: 'looking', role: 'status' }, 'Looking...', el('span', { className: 'lookingBar' })));
    lookingTimer = setTimeout(() => showResultList(area, query), LOOKING_SHORTEST_MS + Math.random() * LOOKING_EXTRA_MS);
  }

  function showRecord(person) {
    const facts = el('dl', { className: 'facts' });
    const fact = (name, value) => {
      if (value) facts.append(el('dt', {}, name), el('dd', {}, value));
    };
    fact('Hangout', person.hangout);
    fact('Wanted for', person.isPlayer && playerIsWanted() ? 'Whatever he did in the last few minutes' : person.wantedFor);
    fact('Affiliation', person.gang === 'None' ? '' : person.gang);
    fact('Born', person.born);

    const details = el('div', {},
      el('h1', { className: 'recordName' }, person.name),
      el('p', { className: 'recordAlias' }, person.aliases.join(', ')),
      statusBadge(person),
      facts);
    if (person.isPlayer && playerIsWanted()) {
      details.append(el('p', { className: 'liveAlert' }, `Wanted now ${stars(game.wantedLevel)} ${lastSeen()}.`));
    }
    details.append(el('h2', { className: 'subhead' }, 'Notes'), el('p', { className: 'notes' }, person.notes));
    if (person.charges) {
      details.append(
        el('h2', { className: 'subhead' }, 'Arrest record'),
        el('table', { className: 'table' },
          el('thead', {}, el('tr', {}, el('th', {}, 'Year'), el('th', {}, 'Offense'), el('th', {}, 'Outcome'))),
          el('tbody', {}, ...person.charges.map((charge) => el('tr', {}, el('td', {}, charge.date), el('td', {}, charge.offense), el('td', {}, charge.outcome))))));
    }

    const back = lastSearch ? link('#results/' + encodeURIComponent(lastSearch), 'backLink', 'Back to results') : link('#records', 'backLink', 'Search records');
    content.append(back, el('div', { className: 'record' }, photo(person), details));
  }

  function showRecords(id) {
    const person = people.find((entry) => entry.id === id);
    if (person) showRecord(person);
    else showSearch();
  }

  // --- Most Wanted ---
  function showWanted() {
    const posters = wantedList().map((entry) => {
      const text = el('span', { className: 'posterText' },
        el('span', { className: 'posterName' }, shownName(entry.person)),
        el('span', { className: 'posterFor' }, entry.wantedFor));
      if (entry.level) text.append(el('span', { className: 'stars' }, stars(entry.level)));
      text.append(el('span', { className: 'reward' }, dollars(entry.reward) + ' reward'));
      return link('#records/' + entry.person.id, 'poster' + (entry.level ? ' player' : ''), photo(entry.person), text);
    });
    content.append(
      el('h1', { className: 'pageTitle' }, 'Most Wanted'),
      el('p', { className: 'pageNote' }, 'Do not approach. Call for backup.'),
      el('div', { className: 'posters' }, ...posters));
  }

  // --- Dispatch ---
  function callTime(minutesAgo) {
    const total = ((game ? game.hour * 60 + game.minute : 12 * 60) - minutesAgo + 24 * 60) % (24 * 60);
    return String(Math.floor(total / 60)).padStart(2, '0') + ':' + String(total % 60).padStart(2, '0');
  }

  function showDispatch() {
    const rows = [];
    if (playerIsWanted()) {
      rows.push(el('tr', { className: 'urgent' },
        el('td', { className: 'nowrap' }, callTime(0)), el('td', { className: 'nowrap' }, '10-99'),
        el('td', {}, `All units, Carl Johnson wanted ${stars(game.wantedLevel)}`), el('td', {}, game.zone || 'Unknown')));
    }
    sa.shuffle(window.SAPD_CALLS).slice(0, 9).forEach((call, index) => {
      rows.push(el('tr', {}, el('td', { className: 'nowrap' }, callTime(4 + index * 7)), el('td', { className: 'nowrap' }, call.code), el('td', {}, call.text), el('td', {}, call.place)));
    });
    content.append(
      el('h1', { className: 'pageTitle' }, 'Dispatch'),
      el('table', { className: 'table' },
        el('thead', {}, el('tr', {}, el('th', {}, 'Time'), el('th', {}, 'Code'), el('th', {}, 'Call'), el('th', {}, 'Location'))),
        el('tbody', {}, ...rows)));
  }

  // --- Evidence ---
  function showEvidence() {
    const rows = window.SAPD_EVIDENCE.map((entry) => {
      const where = el('td', { className: entry.missing ? 'missing' : '' }, entry.missing ? `Missing, last with ${entry.signedTo}` : entry.signedTo || '');
      if (!entry.missing && !entry.signedTo) {
        const button = el('button', { className: 'smallButton', type: 'button' }, 'Sign out');
        button.onclick = () => { where.textContent = 'Signed out to this terminal'; };
        where.append(button);
      }
      return el('tr', {}, el('td', { className: 'nowrap' }, entry.item), el('td', {}, entry.what), el('td', {}, entry.from), where);
    });
    content.append(
      el('h1', { className: 'pageTitle' }, 'Evidence'),
      el('table', { className: 'table' },
        el('thead', {}, el('tr', {}, el('th', {}, 'Item'), el('th', {}, 'Description'), el('th', {}, 'Source'), el('th', {}, 'Where it is now'))),
        el('tbody', {}, ...rows)));
  }

  // --- Bulletins ---
  function showBulletins() {
    content.append(el('h1', { className: 'pageTitle' }, 'Bulletins'));
    for (const memo of bulletins) {
      content.append(el('section', { className: 'memo' }, el('h3', {}, memo.title), el('p', { className: 'memoFrom' }, memo.from), el('p', {}, memo.body)));
    }
  }

  // --- C.R.A.S.H.: no password is ever right, and trying one sets off the alarm ---
  const CLOSE_SECONDS = 5;

  function triggerAlarm(panel) {
    fetch('/_game/alarm', { method: 'POST', headers: { 'X-EyeFinder': '1' } }).catch(() => {});

    document.body.classList.add('alarm');
    const count = el('span', { className: 'countdown' }, String(CLOSE_SECONDS));
    panel.className = 'crash denied';
    panel.replaceChildren(
      el('h1', {}, 'Access denied'),
      el('p', {}, 'This attempt has been reported to Officer Tenpenny. Units are on their way to this terminal.'),
      count);

    let left = CLOSE_SECONDS;
    const timer = setInterval(() => {
      left--;
      if (left <= 0) clearInterval(timer);
      count.textContent = String(Math.max(left, 0));
    }, 1000);
  }

  function showCrash() {
    const input = el('input', { type: 'password', autocomplete: 'off' });
    const form = el('form', {},
      el('label', {}, 'Password', input),
      el('button', { className: 'primaryButton' }, 'Log in'));
    const panel = el('div', { className: 'crash' },
      el('h1', {}, 'C.R.A.S.H. files'),
      el('p', {}, 'Community Resources Against Street Hoodlums. Members of the unit only.'),
      form);
    form.addEventListener('submit', (event) => {
      event.preventDefault();
      if (input.value) triggerAlarm(panel);
      else input.focus();
    });
    content.append(panel);
    input.focus();
  }

  // --- Which page is showing ---
  const PAGES = { home: showHome, records: showRecords, results: showResults, wanted: showWanted, dispatch: showDispatch, evidence: showEvidence, bulletins: showBulletins, crash: showCrash };
  const TAB_OF = { results: 'records' };

  let routeNumber = 0;

  async function route() {
    const number = ++routeNumber;
    const [section, ...rest] = location.hash.slice(1).split('/');
    const name = Object.hasOwn(PAGES, section) ? section : 'home';
    await refreshGame();
    if (number !== routeNumber) return;

    clearTimeout(lookingTimer);
    if (name === 'home' || name === 'crash') lastSearch = '';
    content.textContent = '';
    window.scrollTo(0, 0);

    const tab = TAB_OF[name] || name;
    for (const entry of document.querySelectorAll('.tabs a')) {
      if (entry.dataset.section === tab) entry.setAttribute('aria-current', 'page');
      else entry.removeAttribute('aria-current');
    }
    PAGES[name](decodeURIComponent(rest.join('/')));
  }

  addEventListener('hashchange', route);
  route();
})();
