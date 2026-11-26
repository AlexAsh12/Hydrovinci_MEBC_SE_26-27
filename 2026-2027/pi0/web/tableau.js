/*
 * Tableau de bord : recoit le flux /flux (Server-Sent Events) du serveur C
 * de la Pi et met a jour l'affichage.
 *
 * Economie de CPU (Pi Zero 2W) :
 *   - les vues 3D et compas ne sont redessinees que pendant un mouvement,
 *     puis l'animation s'arrete toute seule ;
 *   - le texte n'est touche que s'il change ;
 *   - la rose du compas est dessinee une seule fois puis tournee.
 */
'use strict';

(function () {
  const $ = (id) => document.getElementById(id);

  if (new URLSearchParams(location.search).has('kiosque')) {
    document.documentElement.classList.add('kiosque');
  }

  function texte(el, v) { if (el.textContent !== v) el.textContent = v; }
  function classe(el, nom, actif) { if (el.classList.contains(nom) !== actif) el.classList.toggle(nom, actif); }

  /* ------------------------------------------------------------------ */
  /* Compas                                                              */
  /* ------------------------------------------------------------------ */

  function creerCompas(canvas) {
    const ctx = canvas.getContext('2d', { alpha: false });
    let largeur = 0, hauteur = 0, ratio = 1, rayon = 0, rose = null, fond = '#111a24';

    function redimensionner() {
      ratio = Math.min(window.devicePixelRatio || 1, 2);
      largeur = canvas.clientWidth;
      hauteur = canvas.clientHeight;
      canvas.width = Math.max(1, Math.round(largeur * ratio));
      canvas.height = Math.max(1, Math.round(hauteur * ratio));
      rayon = Math.max(10, Math.min(largeur, hauteur) / 2 - 14);
      fond = getComputedStyle(canvas.parentElement).backgroundColor || fond;
      dessinerRose();
    }

    /* La rose est dessinee une fois, ensuite on ne fait que la tourner */
    function dessinerRose() {
      const taille = Math.ceil(rayon * 2 * ratio) + 4;
      rose = document.createElement('canvas');
      rose.width = rose.height = taille;
      const c = rose.getContext('2d');
      c.translate(taille / 2, taille / 2);
      c.scale(ratio, ratio);

      c.beginPath();
      c.arc(0, 0, rayon, 0, Math.PI * 2);
      c.fillStyle = '#0c141d';
      c.fill();
      c.lineWidth = 2;
      c.strokeStyle = '#2a3b4f';
      c.stroke();

      for (let d = 0; d < 360; d += 5) {
        const a = (d * Math.PI) / 180;
        const long = d % 30 === 0 ? 0.14 : d % 10 === 0 ? 0.09 : 0.05;
        c.beginPath();
        c.moveTo(Math.sin(a) * rayon * (1 - long), -Math.cos(a) * rayon * (1 - long));
        c.lineTo(Math.sin(a) * rayon * 0.985, -Math.cos(a) * rayon * 0.985);
        c.lineWidth = d % 30 === 0 ? 2 : 1;
        c.strokeStyle = d % 30 === 0 ? '#c9d6e3' : '#5d7088';
        c.stroke();
      }

      c.textAlign = 'center';
      c.textBaseline = 'middle';
      const cardinaux = { 0: 'N', 90: 'E', 180: 'S', 270: 'O' };
      for (let d = 0; d < 360; d += 30) {
        const a = (d * Math.PI) / 180;
        const card = cardinaux[d];
        const r = rayon * (card ? 0.7 : 0.73);
        c.save();
        c.translate(Math.sin(a) * r, -Math.cos(a) * r);
        c.rotate(a);
        c.font = (card ? '700 ' + Math.round(rayon * 0.2) : '500 ' + Math.round(rayon * 0.1)) + 'px system-ui, sans-serif';
        c.fillStyle = card === 'N' ? '#ff4d4f' : card ? '#e9f0f7' : '#8193a8';
        c.fillText(card || String(d), 0, 0);
        c.restore();
      }
    }

    function dessiner(capDeg) {
      if (!largeur) redimensionner();
      ctx.setTransform(ratio, 0, 0, ratio, 0, 0);
      ctx.fillStyle = fond;
      ctx.fillRect(0, 0, largeur, hauteur);

      const cx = largeur / 2, cy = hauteur / 2;
      // La rose tourne : le cap du bateau est toujours en haut
      ctx.save();
      ctx.translate(cx, cy);
      ctx.rotate((-capDeg * Math.PI) / 180);
      ctx.drawImage(rose, -rose.width / (2 * ratio), -rose.height / (2 * ratio), rose.width / ratio, rose.height / ratio);
      ctx.restore();

      // Ligne de foi (fixe, en haut)
      ctx.beginPath();
      ctx.moveTo(cx, cy - rayon - 2);
      ctx.lineTo(cx - 10, cy - rayon - 14);
      ctx.lineTo(cx + 10, cy - rayon - 14);
      ctx.closePath();
      ctx.fillStyle = '#3fb6ff';
      ctx.fill();

      // Silhouette du bateau au centre (fixe, avant vers le haut)
      const s = rayon * 0.22;
      ctx.beginPath();
      ctx.moveTo(cx, cy - s * 1.3);
      ctx.quadraticCurveTo(cx + s * 0.55, cy - s * 0.4, cx + s * 0.45, cy + s);
      ctx.lineTo(cx - s * 0.45, cy + s);
      ctx.quadraticCurveTo(cx - s * 0.55, cy - s * 0.4, cx, cy - s * 1.3);
      ctx.fillStyle = 'rgba(63, 182, 255, 0.25)';
      ctx.strokeStyle = '#3fb6ff';
      ctx.lineWidth = 2;
      ctx.fill();
      ctx.stroke();
    }

    return { dessiner, redimensionner };
  }

  /* ------------------------------------------------------------------ */
  /* Animation de l'assiette et du cap                                   */
  /* ------------------------------------------------------------------ */

  const vue3d = window.Bateau.creer($('vue3d'));
  const compas = creerCompas($('compas'));

  const cible = { roulis: 0, tangage: 0, cap: 0 };
  const affiche = { roulis: 0, tangage: 0, cap: 0 };
  const LISSAGE_S = 0.12;   // constante de temps du lissage entre deux mesures (5 Hz)
  let image = 0, instant = 0;

  function ecartCap(a, b) { return ((a - b + 540) % 360) - 180; }

  function animer(t) {
    image = 0;
    const dt = Math.min(0.1, Math.max(0, (t - instant) / 1000));
    instant = t;
    const k = 1 - Math.exp(-dt / LISSAGE_S);
    affiche.roulis += (cible.roulis - affiche.roulis) * k;
    affiche.tangage += (cible.tangage - affiche.tangage) * k;
    affiche.cap = (affiche.cap + ecartCap(cible.cap, affiche.cap) * k + 360) % 360;

    vue3d.dessiner(affiche.roulis, affiche.tangage);
    compas.dessiner(affiche.cap);

    const reste = Math.abs(cible.roulis - affiche.roulis) + Math.abs(cible.tangage - affiche.tangage) +
      Math.abs(ecartCap(cible.cap, affiche.cap));
    if (reste > 0.02) relancer();
  }

  function relancer() {
    if (!image) {
      if (!instant) instant = performance.now();
      image = requestAnimationFrame(animer);
    }
  }

  function redessiner() {
    vue3d.redimensionner();
    compas.redimensionner();
    instant = performance.now();
    vue3d.dessiner(affiche.roulis, affiche.tangage);
    compas.dessiner(affiche.cap);
  }
  new ResizeObserver(redessiner).observe($('p-imu'));
  new ResizeObserver(redessiner).observe($('p-cap'));

  /* ------------------------------------------------------------------ */
  /* Donnees                                                             */
  /* ------------------------------------------------------------------ */

  const PERIME_MS = 1500;
  const vu = { imu: 0, mouv: 0, bms1: 0, bms2: 0, power: 0, temp: 0, etat: 0, vitesse: 0, sim: 0, gps: 0, hb: 0 };

  function signe(v, decimales) { return (v > 0 ? '+' : '') + v.toFixed(decimales); }

  const CARDINAUX = ['N', 'NNE', 'NE', 'ENE', 'E', 'ESE', 'SE', 'SSE', 'S', 'SSO', 'SO', 'OSO', 'O', 'ONO', 'NO', 'NNO'];
  const PRECISION = [
    ['Cap non calibré : faire des 8', 'faible'],
    ['Calibration faible', 'faible'],
    ['Calibration moyenne', 'moyen'],
    ['Calibration bonne', 'ok'],
  ];

  function surImu(m) {
    vu.imu = performance.now();
    cible.roulis = m.roulis / 100;
    cible.tangage = m.tangage / 100;
    cible.cap = m.cap / 100;
    if (!instant) { affiche.roulis = cible.roulis; affiche.tangage = cible.tangage; affiche.cap = cible.cap; }

    texte($('roulis'), signe(cible.roulis, 1) + '°');
    texte($('roulis-sens'), Math.abs(cible.roulis) < 0.5 ? 'à plat' : cible.roulis > 0 ? 'gîte tribord' : 'gîte bâbord');
    texte($('tangage'), signe(cible.tangage, 1) + '°');
    texte($('tangage-sens'), Math.abs(cible.tangage) < 0.5 ? 'à plat' : cible.tangage > 0 ? 'avant haut' : 'avant bas');
    texte($('cap'), String(Math.round(cible.cap) % 360).padStart(3, '0') + '°');
    texte($('cardinal'), CARDINAUX[Math.round(cible.cap / 22.5) % 16]);

    // Incertitude du cap estimee par le capteur (-1 = non transmise)
    texte($('cap-incertitude'), m.incertitude >= 0 ? '± ' + (m.incertitude / 100).toFixed(m.incertitude < 1000 ? 1 : 0) + '°' : '');

    const badge = $('calib');
    const p = PRECISION[m.precision];
    texte(badge, p ? p[0] : 'Calibration inconnue');
    const niveau = p ? p[1] : '';
    for (const n of ['ok', 'moyen', 'faible']) classe(badge, n, n === niveau);
    relancer();
  }

  function surMouvement(m) {
    vu.mouv = performance.now();
    texte($('acc-avant'), signe(m.acc_avant / 100, 2) + ' m/s²');
    texte($('acc-lateral'), signe(m.acc_lateral / 100, 2) + ' m/s²');
    texte($('acc-vertical'), signe(m.acc_vertical / 100, 2) + ' m/s²');
    texte($('giration'), signe(m.giration / 10, 1) + ' °/s');
    texte($('vit-roulis'), signe(m.vit_roulis / 10, 1) + ' °/s');
    texte($('vit-tangage'), signe(m.vit_tangage / 10, 1) + ' °/s');
  }

  function surBms(m) {
    const b = 'b' + m.id + '-';
    vu['bms' + m.id] = m.ok ? performance.now() : 0;
    if (!m.ok) return;
    const soc = m.soc / 10;
    texte($(b + 'soc'), soc.toFixed(1) + ' %');
    const barre = $(b + 'soc-barre');
    barre.style.width = Math.max(0, Math.min(100, soc)) + '%';
    classe(barre, 'critique', soc < 15);
    classe(barre, 'bas', soc >= 15 && soc < 30);
    texte($(b + 'tension'), (m.tension / 100).toFixed(2) + ' V');
    texte($(b + 'courant'), signe(m.courant / 100, 2) + ' A');
    texte($(b + 'restant'), (m.restant / 100).toFixed(2) + ' Ah');
    if (m.cellules.length) {
      const min = Math.min(...m.cellules), max = Math.max(...m.cellules);
      texte($(b + 'cellules'), (min / 1000).toFixed(3) + ' – ' + (max / 1000).toFixed(3) + ' V');
      texte($(b + 'ecart'), (max - min) + ' mV');
    }
    const temps = m.temps.filter((t) => t !== -32768);
    texte($(b + 'temp'), temps.length ? (Math.max(...temps) / 10).toFixed(1) + ' °C' : '—');
  }

  function surPower(m) {
    vu.power = performance.now();
    texte($('pw-tension'), (m.mv / 1000).toFixed(2) + ' V');
    texte($('pw-courant'), signe(m.ma / 1000, 2) + ' A');
    texte($('pw-puissance'), (m.mw / 1000).toFixed(1) + ' W');
  }

  /* Sondes de temperature : les lignes ne sont recreees que si la liste change */
  const SEUIL_CHAUD_C = 60;
  const TEMP_INVALIDE = -32768;
  let clesSondes = '';
  let lignes = [];

  function surTemp(m) {
    const sondes = m.sondes.slice().sort((a, b) => (a.nom || '~' + a.rom).localeCompare(b.nom || '~' + b.rom));
    vu.temp = sondes.length ? performance.now() : 0;
    texte($('voile-temp'), sondes.length ? 'Pas de données' : 'Aucune sonde détectée');

    const cles = sondes.map((s) => s.rom + (s.nom || '')).join('|');
    if (cles !== clesSondes) {
      clesSondes = cles;
      const conteneur = $('sondes');
      conteneur.replaceChildren();
      lignes = sondes.map((s) => {
        const ligne = document.createElement('div');
        ligne.className = 'sonde';
        const nom = document.createElement('span');
        nom.className = s.nom ? 'nom' : 'nom adresse';
        nom.textContent = s.nom || s.rom;  // sans nom : l'adresse, a recopier dans sondes.conf
        const temp = document.createElement('span');
        temp.className = 'temp';
        ligne.append(nom, temp);
        conteneur.append(ligne);
        return temp;
      });
    }

    sondes.forEach((s, i) => {
      const el = lignes[i];
      const valide = s.c !== TEMP_INVALIDE;
      texte(el, valide ? (s.c / 100).toFixed(1) + ' °C' : 'erreur');
      classe(el, 'erreur', !valide);
      classe(el, 'chaud', valide && s.c / 100 >= SEUIL_CHAUD_C);
    });
  }

  /* ------------------------------------------------------------------ */
  /* Vitesse, GPS, 4G, SMS (module SIM7600)                              */
  /* ------------------------------------------------------------------ */

  const carte = window.Carte.creer($('carte'));
  new ResizeObserver(() => carte.redimensionner()).observe($('p-carte'));

  const NOEUDS = 1.943844;   // m/s -> noeuds
  const SOURCES_VITESSE = { 'gps+imu': 'GPS + IMU', gps: 'GPS seul', imu: 'IMU seule : GPS perdu' };
  let etatSim = null;
  let finMessage = 0;

  function surVitesse(m) {
    if (m.src === 'aucune') { vu.vitesse = 0; return; }
    vu.vitesse = performance.now();
    texte($('vit-noeuds'), (m.ms * NOEUDS).toFixed(1));
    texte($('vit-kmh'), (m.ms * 3.6).toFixed(1));
    const source = $('vit-source');
    texte(source, SOURCES_VITESSE[m.src] || m.src);
    classe(source, 'faible', m.src === 'imu');
    texte($('vit-incertitude'), '± ' + (m.sigma * NOEUDS).toFixed(1) + ' nd');
  }

  function formatPosition(lat, lon) {
    return Math.abs(lat).toFixed(5) + '° ' + (lat >= 0 ? 'N' : 'S') + '   ' +
      Math.abs(lon).toFixed(5) + '° ' + (lon >= 0 ? 'E' : 'O');
  }

  /* Cap du bateau sur la carte : IMU si a jour, sinon route GPS */
  function capBateau(m) {
    if (performance.now() - vu.imu < PERIME_MS) return cible.cap;
    return m.cap >= 0 ? m.cap : null;
  }

  function surGps(m) {
    if (!m.fix) { vu.gps = 0; carte.perdu(); return; }
    vu.gps = performance.now();
    if (m.pt) carte.ajouterPoint(m.lat, m.lon);
    carte.position(m.lat, m.lon, capBateau(m), 1);
    texte($('position'), formatPosition(m.lat, m.lon) + (m.heure ? '   ' + m.heure + ' UTC' : ''));
  }

  function barres(rssi) {
    if (rssi < 0) return '?';
    const n = rssi >= 20 ? 4 : rssi >= 15 ? 3 : rssi >= 10 ? 2 : rssi >= 2 ? 1 : 0;
    return '▂▄▆█'.slice(0, n) + '·'.repeat(4 - n);
  }

  function surSim(m) {
    vu.sim = performance.now();
    etatSim = m;
    let t, niveau = 'attention';
    if (!m.actif) { t = '4G désactivé'; niveau = ''; }
    else if (!m.present) t = '4G absent';
    else if (m.carte === 'absente') t = 'Pas de carte SIM';
    else if (m.carte === 'pin') t = 'SIM : code PIN';
    else if (m.carte === 'bloquee') t = 'SIM bloquée (PUK)';
    else if (!m.reseau) t = 'Pas de réseau ' + barres(m.signal);
    else { t = (m.techno || 'Réseau') + ' ' + barres(m.signal) + (m.operateur ? ' ' + m.operateur : ''); niveau = 'ok'; }
    if (m.attente > 0) t += ' · ' + m.attente + ' SMS en attente';
    const el = $('sim');
    texte(el, t);
    classe(el, 'ok', niveau === 'ok');
    classe(el, 'attention', niveau === 'attention');

    const g = $('gps');
    const present = m.actif && m.present;
    texte(g, !present ? 'GPS —' : m.fix ? 'GPS ok' : m.gps ? 'GPS : recherche' : 'GPS éteint');
    classe(g, 'ok', present && m.fix);
    classe(g, 'attention', present && !m.fix);
  }

  /* Pourquoi pas de vitesse / pas de carte */
  function raisonSansGps() {
    const m = etatSim;
    let raison = 'vitesse inconnue';
    if (!m || performance.now() - vu.sim > 3000) raison = 'serveur injoignable';
    else if (!m.actif) raison = 'module 4G désactivé';
    else if (!m.present) raison = 'module 4G/GPS absent';
    else if (!m.gps) raison = 'GPS éteint';
    else if (!m.fix) raison = 'recherche GPS…';
    return 'Pas de données : ' + raison;
  }

  function surSms(m) {
    texte($('dernier-sms'), 'SMS ' + m.heure + ' : ' + m.texte);
    if (m.ancien) return;   // deja recu avant l'ouverture de la page : pas de bandeau
    const el = $('message');
    texte(el, 'SMS ' + m.de + ' (' + m.heure + ') : ' + m.texte);
    el.hidden = false;
    finMessage = performance.now() + 30000;
  }

  /* Envoye a chaque (re)connexion au serveur : zones de carte et trace complete */
  function surCarte(m) {
    carte.definirZones(m.zones);
    fetch('/trace', { cache: 'no-store' })
      .then((r) => (r.ok ? r.json() : null))
      .then((d) => { if (d && Array.isArray(d.points)) carte.chargerTrace(d.points); })
      .catch(() => {});
  }

  function duree(ms) {
    const s = Math.floor(ms / 1000);
    const h = Math.floor(s / 3600), mi = Math.floor((s % 3600) / 60);
    return h ? h + ' h ' + String(mi).padStart(2, '0') : mi + ' min ' + String(s % 60).padStart(2, '0');
  }

  const liaison = $('liaison');
  function etatLiaison(texteEtat, nom) {
    texte(liaison, texteEtat);
    for (const n of ['ok', 'perdu', 'inconnu']) classe(liaison, n, n === nom);
  }

  const ALERTES = {
    1: (v) => 'Température haute : ' + (v / 100).toFixed(1) + ' °C',
    2: (v) => 'Cellule basse : ' + (v / 1000).toFixed(3) + ' V',
    3: (v) => 'Déséquilibre des cellules : ' + v + ' mV',
    4: () => 'BMS perdu',
    5: () => 'Bus CAN perdu',
  };
  const SOURCES = { 1: 'BMS 1', 2: 'BMS 2' };
  let finAlerte = 0;

  function surAlerte(m) {
    const el = $('alerte');
    const f = ALERTES[m.code];
    const source = SOURCES[m.source] || (m.source >= 0x10 ? 'sonde ' + (m.source - 0x10 + 1) : '');
    texte(el, (f ? f(m.valeur) : 'Alerte ' + m.code) + (source ? ' (' + source + ')' : ''));
    classe(el, 'critique', m.niveau >= 3);
    el.hidden = false;
    finAlerte = performance.now() + 15000;
  }

  function surMessage(ev) {
    let m;
    try { m = JSON.parse(ev.data); } catch (e) { return; }
    switch (m.t) {
      case 'imu': surImu(m); break;
      case 'bms': surBms(m); break;
      case 'power': surPower(m); break;
      case 'temp': surTemp(m); break;
      case 'mouv': surMouvement(m); break;
      case 'hb': vu.hb = performance.now(); texte($('uptime'), 'ESP ' + duree(m.uptime)); break;
      case 'hello': texte($('fw'), 'fw ' + m.fw); break;
      case 'alerte': surAlerte(m); break;
      case 'vitesse': surVitesse(m); break;
      case 'gps': surGps(m); break;
      case 'sim': surSim(m); break;
      case 'sms': surSms(m); break;
      case 'carte': surCarte(m); break;
      case 'etat':
        vu.etat = performance.now();
        texte($('lien'), 'UART ' + m.crc + (m.crc > 1 ? ' erreurs' : ' erreur'));
        if (m.esp) etatLiaison('ESP32 connecté', 'ok');
        else if (!m.uart) etatLiaison('UART indisponible', 'perdu');
        else etatLiaison('ESP32 perdu', 'perdu');
        break;
      default: break;
    }
  }

  function connecter() {
    const flux = new EventSource('/flux');
    flux.onmessage = surMessage;
    flux.onerror = () => etatLiaison('Serveur injoignable', 'perdu');
    // EventSource se reconnecte tout seul (retry: 1000 envoye par le serveur)
  }
  connecter();

  /* Verifications periodiques : donnees perimees, heure, alerte */
  setInterval(() => {
    const t = performance.now();
    classe($('p-imu'), 'perime', t - vu.imu > PERIME_MS);
    classe($('p-cap'), 'perime', t - vu.imu > PERIME_MS);
    classe($('p-mouv'), 'perime', t - vu.mouv > PERIME_MS);
    classe($('p-bms1'), 'perime', t - vu.bms1 > 3000);
    classe($('p-bms2'), 'perime', t - vu.bms2 > 3000);
    classe($('p-power'), 'perime', t - vu.power > 3000);
    classe($('p-temp'), 'perime', t - vu.temp > 3000);
    classe($('p-vitesse'), 'perime', t - vu.vitesse > PERIME_MS);
    texte($('voile-vitesse'), raisonSansGps());
    // Carte : seulement avec une position GPS actuelle
    const sansPosition = t - vu.gps > 3000;
    classe($('p-carte'), 'perime', sansPosition);
    texte($('voile-carte'), raisonSansGps().replace('vitesse inconnue', 'pas de position GPS'));
    if (sansPosition) texte($('position'), '—');

    // Barre du haut : rien de perime
    if (t - vu.hb > 3000) { texte($('uptime'), 'ESP —'); texte($('fw'), 'fw —'); }
    if (t - vu.sim > 3000) {
      texte($('sim'), '4G —');
      texte($('gps'), 'GPS —');
      for (const id of ['sim', 'gps']) { classe($(id), 'ok', false); classe($(id), 'attention', false); }
    }
    if (t - vu.etat > 3000) { etatLiaison('Serveur injoignable', 'perdu'); texte($('lien'), 'UART —'); }
    if (finAlerte && t > finAlerte) { $('alerte').hidden = true; finAlerte = 0; }
    if (finMessage && t > finMessage) { $('message').hidden = true; finMessage = 0; }

    const d = new Date();
    texte($('heure'), [d.getHours(), d.getMinutes(), d.getSeconds()].map((x) => String(x).padStart(2, '0')).join(':'));
  }, 500);

  redessiner();
})();
