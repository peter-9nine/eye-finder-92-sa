// The department's files. To add a person, add an entry here: only name, status and notes are needed.
// status is one of: wanted, clear, sealed, removed, officer. A photo goes in photos/<id>.jpg.
window.SAPD_PEOPLE = [
  {
    id: 'cj', hangout: 'Grove Street, Ganton', name: 'Johnson, Carl', aliases: ['CJ'], gang: 'Grove Street Families', born: '1968', status: 'clear', isPlayer: true,
    charges: [
      { date: '1987', offense: 'Grand theft auto', outcome: 'No charges filed. Left the state' },
      { date: '1992', offense: 'Returned to Los Santos', outcome: 'Not a crime. Noted anyway' },
    ],
    notes: 'Brought in on arrival by officers Tenpenny and Pulaski. Released without paperwork, which is how they prefer it.',
  },
  {
    id: 'sweet', hangout: 'Grove Street, Ganton', name: 'Johnson, Sean', aliases: ['Sweet'], gang: 'Grove Street Families', born: '1963', status: 'clear',
    charges: [
      { date: '1988', offense: 'Disturbing the peace', outcome: 'Fined' },
      { date: '1990', offense: 'Possession of an unlicensed firearm', outcome: 'Served 6 months' },
    ],
    notes: 'Will not talk to officers. Will talk at length about the neighborhood, and how it used to be.',
  },
  {
    id: 'smoke', hangout: 'Idlewood, and any Cluckin\' Bell', name: 'Harris, Melvin', aliases: ['Big Smoke'], gang: 'Grove Street Families', born: '1962', status: 'clear',
    charges: [
      { date: '1986', offense: 'Possession with intent', outcome: 'Dismissed' },
      { date: '1989', offense: 'Failure to pay for two number 9s, a number 9 large, a number 6 with extra dip', outcome: 'Settled with Cluckin\' Bell' },
    ],
    notes: 'Clean record for three years, with a new house and no visible income. Nobody here is looking into it.',
  },
  {
    id: 'ryder', hangout: 'Grove Street, Ganton', name: 'Wilson, Lance', aliases: ['Ryder'], gang: 'Grove Street Families', born: '1962', status: 'wanted', wantedFor: 'Armed robbery of a pizza restaurant', reward: 500,
    charges: [
      { date: '1991', offense: 'Armed robbery', outcome: 'Warrant open' },
      { date: '1992', offense: 'Robbery of a Well Stacked Pizza', outcome: 'Warrant open. He ordered first' },
    ],
    notes: 'Wears the same hat in every photograph. Witnesses describe a strong smell of pineapple.',
  },
  {
    id: 'cesar', hangout: 'East Los Santos, Unity Station', name: 'Vialpando, Cesar', aliases: [], gang: 'Varrios Los Aztecas', born: '1965', status: 'clear',
    charges: [
      { date: '1991', offense: 'Illegal street racing', outcome: 'Fined' },
      { date: '1992', offense: 'Unlicensed hydraulic modifications', outcome: 'Fined. Car bounced out of the impound' },
    ],
    notes: 'Known to be seeing a Johnson. Expect that to be a problem for somebody.',
  },
  {
    id: 'loc', hangout: 'Burger Shot, Marina', name: 'Cross, Jeffrey', aliases: ['OG Loc'], gang: 'Grove Street Families', born: '1970', status: 'clear',
    charges: [
      { date: '1991', offense: 'Possession', outcome: 'Served 1 year, on purpose' },
      { date: '1992', offense: 'Impersonating a rap musician', outcome: 'Not a crime. Several officers wish it were' },
    ],
    notes: 'On parole. Works at Burger Shot, in a uniform his parole officer describes as "the first thing he has earned".',
  },
  {
    id: 'madd', hangout: 'Mulholland', name: 'Madd Dogg', aliases: ['Mad Dog'], gang: 'None', born: '1964', status: 'clear',
    charges: [
      { date: '1990', offense: 'Noise complaint, Mulholland', outcome: 'Fan club paid the fine' },
      { date: '1992', offense: 'Report of a stolen rhyme book', outcome: 'Filed. Not investigated' },
    ],
    notes: 'A famous man, so handle with care, and get an autograph for the captain.',
  },
  {
    id: 'truth', hangout: 'Flint County, in a van', name: 'The Truth', aliases: ['Truth'], gang: 'None', born: 'Says he doesn\'t recall', status: 'wanted', wantedFor: 'Cultivation, across most of a county', reward: 250,
    charges: [
      { date: '1975', offense: 'Cultivation of marijuana', outcome: 'Arresting officer resigned to join him' },
      { date: '1983', offense: 'Cultivation of marijuana', outcome: 'Case file smells odd. Lost' },
    ],
    notes: 'Last seen in a van in Flint County. An officer went to question him and sat in that van for six hours. His report reads, in full, "They know."',
  },
  {
    id: 'woozie', hangout: 'Four Dragons Casino, Las Venturas', name: 'Mu, Wu Zi', aliases: ['Woozie'], gang: 'Mountain Cloud Boys', born: '1960', status: 'clear',
    charges: [
      { date: '1991', offense: 'Suspected getaway driver, Chinatown robbery', outcome: 'Dropped. Suspect is registered blind' },
    ],
    notes: 'Registered blind, with a perfect driving record. No officer has offered a satisfying explanation for this.',
  },
  {
    id: 'catalina', hangout: 'Fern Ridge, Red County', name: 'Catalina', aliases: [], gang: 'None', born: 'Refused to give', status: 'wanted', wantedFor: 'A string of liquor store robberies in Red County', reward: 750,
    charges: [
      { date: '1992', offense: 'Armed robbery, four counts', outcome: 'Warrant open' },
      { date: '1992', offense: 'Threatening an officer at length', outcome: 'Officer has requested a transfer' },
    ],
    notes: 'Approach with backup, and do not mention her boyfriends, past or present.',
  },
  {
    id: 'rosenberg', hangout: 'Caligula\'s Palace, Las Venturas', name: 'Rosenberg, Ken', aliases: ['Ken Rosenberg'], gang: 'None', born: '1955', status: 'clear',
    charges: [
      { date: '1986', offense: 'Possession of cocaine', outcome: 'Represented himself. Lost. Appealed. Won' },
      { date: '1992', offense: 'Failure to appear, Las Venturas', outcome: 'Pending' },
    ],
    notes: 'A lawyer. Has entered rehab eleven times, and billed a client for each one.',
  },
  {
    id: 'paul', hangout: 'Caligula\'s Palace, Las Venturas', name: 'Paul, Kent', aliases: ['Kent Paul'], gang: 'None', born: '1961', status: 'clear',
    charges: [
      { date: '1992', offense: 'Drunk and disorderly, Las Venturas', outcome: 'Released. Could not be understood' },
    ],
    notes: 'Says he manages a band. Travels with an associate, who says the same about himself.',
  },
  {
    id: 'maccer', hangout: 'Wherever Kent Paul is', name: 'Maccer', aliases: [], gang: 'None', born: '1964', status: 'clear',
    charges: [
      { date: '1992', offense: 'Public indecency, Las Venturas', outcome: 'Fined' },
      { date: '1992', offense: 'Public indecency, Las Venturas', outcome: 'Fined, on the way out of court' },
    ],
    notes: 'Please stop bringing him in. The interview room still has not recovered.',
  },
  {
    id: 'zero', hangout: 'Zero\'s RC Shop, San Fierro', name: 'Zero', aliases: [], gang: 'None', born: '1970', status: 'clear',
    charges: [
      { date: '1992', offense: 'Operating a model aircraft near a state office', outcome: 'Warning. Lectured the officer for an hour' },
    ],
    notes: 'Owns an RC shop in San Fierro. Claims to be at war with somebody named Berkley.',
  },
  {
    id: 'toreno', hangout: 'Not known', name: 'Toreno, Mike', aliases: ['Mike Toreno'], gang: 'Not known', status: 'removed',
    notes: 'This file was removed at the request of a federal agency, whose name was also removed. Stop asking about it.',
  },
  {
    id: 'tenpenny', hangout: 'Withheld', name: 'Tenpenny, Frank', aliases: ['Officer Tenpenny'], gang: 'C.R.A.S.H.', status: 'sealed',
    notes: 'Sealed by order of Officer F. Tenpenny, who also signed the order that gave him the authority to seal it.',
  },
  {
    id: 'pulaski', hangout: 'Pershing Square station', name: 'Pulaski, Eddie', aliases: ['Officer Pulaski'], gang: 'C.R.A.S.H.', status: 'officer',
    notes: 'Officer in good standing. See Evidence Locker for the matter of his lunch.',
  },
  {
    id: 'hernandez', hangout: 'Pershing Square station', name: 'Hernandez, Jimmy', aliases: ['Officer Hernandez'], gang: 'C.R.A.S.H.', status: 'officer',
    notes: 'Newest member of the unit. Has asked twice who to speak to about a concern. Nobody has told him.',
  },
];

window.SAPD_EVIDENCE = [
  { item: '4471', what: 'Duffel bag, cash, about $40,000', from: 'Ganton raid', missing: true, signedTo: 'Ofc. Pulaski' },
  { item: '4472', what: 'Duffel bag, cash, about $65,000', from: 'Idlewood raid', missing: true, signedTo: 'Ofc. Tenpenny' },
  { item: '4473', what: 'Sandwich, labelled "Pulaski"', from: 'Break room dispute', signedTo: 'Do not open' },
  { item: '4474', what: 'Duffel bag, cash', from: 'Not recorded', missing: true, signedTo: 'Ofc. Tenpenny' },
  { item: '4475', what: 'Pineapple, one, used in a robbery', from: 'Well Stacked Pizza, Idlewood' },
  { item: '4476', what: 'Pistol, serial number filed off', from: 'Grove Street' },
  { item: '4477', what: 'Rhyme book, partly burned', from: 'Unknown. Nobody will claim it' },
  { item: '4478', what: 'Parachute, slightly used', from: 'Found on the roof of this station' },
  { item: '4479', what: 'Jetpack, reported as "a hoax"', from: 'Area 69 perimeter', missing: true, signedTo: 'a man in sunglasses' },
];

window.SAPD_CALLS = [
  { code: '10-31', text: 'Crime in progress at a Ten Green Bottles', place: 'Ganton' },
  { code: '10-15', text: 'Prisoner in transit, Los Santos to Fort Carson', place: 'Blueberry Acres' },
  { code: '10-54', text: 'Livestock on the highway', place: 'Fort Carson' },
  { code: '10-10', text: 'Fight at a Burger Shot, over a burger', place: 'Idlewood' },
  { code: '10-37', text: 'Suspicious lowrider, bouncing', place: 'East Los Santos' },
  { code: '10-90', text: 'Silent alarm at a bank. It is not very silent', place: 'Commerce' },
  { code: '10-31', text: 'Report of a gas station explosion, again', place: 'Dillimore' },
  { code: '10-55', text: 'Drunk driver on the wrong side of the Gant Bridge', place: 'San Fierro' },
  { code: '10-29', text: 'Check on a man who says he owns the Emerald Isle', place: 'Las Venturas' },
  { code: '10-37', text: 'Lights over the desert. Federal units advise us to ignore them', place: 'Bone County' },
  { code: '10-10', text: 'Two men fighting over a parking space at Pier 69', place: 'San Fierro' },
  { code: '10-64', text: 'Complaint about a hippie. Complainant is also a hippie', place: 'Flint County' },
];

window.SAPD_BULLETINS = [
  { title: 'Paperwork for evidence transfers', from: 'From the Chief\'s office, to all divisions', body: 'Bags of money leaving the locker must be signed out. This is the fourth reminder. "Ofc. Tenpenny" is not an acceptable reason.' },
  { title: 'Cars are not to be left on fire', from: 'From Vehicle Maintenance, to all patrol officers', body: 'We have lost eleven cruisers this month in Ganton alone. A pursuit does not end when you get out of the car and walk away.' },
  { title: 'Roof access', from: 'From Building Services, to all staff', body: 'Whoever is using the roof at night, there is now a parachute in the evidence locker that needs an explanation.' },
  { title: 'Basement', from: 'From the night shift, to the day shift', body: 'Please do not go down there after 2:00 AM. And please stop telling the new guy it\'s funny.' },
];
