/*
 * Carte GPS : vraie carte (Plan IGN), position et direction du bateau,
 * trace, nord en haut.
 *
 * Les tuiles de carte sont stockees sur la Pi (carte/tuiles) pour les seules
 * zones de navigation (carte/zones.conf) et servies par tableau_bord :
 * aucun acces Internet. Hors des zones, la carte montre une grille.
 *
 * La vue suit le bateau. Le zoom est entier (tuiles nettes, pixel pour
 * pixel) et choisi pour garder la trace recente a l'ecran. Une tuile
 * absente est remplacee par une tuile moins detaillee agrandie.
 *
 * Redessinee seulement quand une position ou une tuile arrive, ou que la
 * taille change : presque rien pour le processeur de la Pi.
 */
'use strict';

window.Carte = (function () {
  const TERRE_M = 40075016.686;   // circonference a l'equateur
  const TUILE = 256;
  const POINTS_MAX = 4000;
  const RECENTS = 150;            // points de trace gardes dans la vue (~450 m a 3 m le point)
  const ZOOM_MIN = 12, ZOOM_MAX = 18;
  const CACHE_MAX = 300;          // tuiles gardees en memoire
  const ECHELLES_M = [5, 10, 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000, 50000];

  // Web Mercator normalise : x, y entre 0 et 1
  function projeter(lat, lon) {
    const phi = (Math.max(-85, Math.min(85, lat)) * Math.PI) / 180;
    return [(lon + 180) / 360, (1 - Math.log(Math.tan(phi) + 1 / Math.cos(phi)) / Math.PI) / 2];
  }

  function latitude(y) {
    return (Math.atan(Math.sinh(Math.PI * (1 - 2 * y))) * 180) / Math.PI;
  }

  function creer(canvas) {
    const ctx = canvas.getContext('2d', { alpha: false });
    let largeur = 0, hauteur = 0, ratio = 1;
    let zones = [];                  // { x0, y0, x1, y1, zmin, zmax } en Mercator
    let points = [];                 // trace, [x, y] Mercator
    let bateau = null;               // { x, y, cap, fix }
    let zoom = 16;                   // zoom affiche (entier)
    const tuiles = new Map();        // "z/x/y" -> { etat: 'charge'|'ok'|'absente', image }

    function redimensionner() {
      ratio = Math.min(window.devicePixelRatio || 1, 2);
      largeur = canvas.clientWidth;
      hauteur = canvas.clientHeight;
      canvas.width = Math.max(1, Math.round(largeur * ratio));
      canvas.height = Math.max(1, Math.round(hauteur * ratio));
      dessiner();
    }

    /* ---------------- Tuiles ---------------- */

    // La tuile (z, x, y) touche-t-elle une zone telechargee a ce zoom ?
    function dansUneZone(z, x, y) {
      const n = 2 ** z;
      const tx0 = x / n, tx1 = (x + 1) / n, ty0 = y / n, ty1 = (y + 1) / n;
      return zones.some((zo) => z >= zo.zmin && z <= zo.zmax &&
        tx1 > zo.x0 && tx0 < zo.x1 && ty1 > zo.y0 && ty0 < zo.y1);
    }

    function demander(z, x, y) {
      const cle = z + '/' + x + '/' + y;
      if (tuiles.has(cle)) return tuiles.get(cle);
      const t = { etat: 'charge', image: null };
      tuiles.set(cle, t);
      if (tuiles.size > CACHE_MAX) {
        // Les plus anciennes d'abord (ordre d'insertion), jamais une en cours
        for (const [k, v] of tuiles) {
          if (tuiles.size <= CACHE_MAX * 0.7) break;
          if (v.etat !== 'charge') tuiles.delete(k);
        }
      }
      fetch('/tuiles/' + cle + '.png')
        .then((r) => {
          if (r.status === 404) { t.etat = 'absente'; return null; }
          if (!r.ok) throw new Error(String(r.status));
          return r.blob();
        })
        .then((b) => (b ? createImageBitmap(b) : null))
        .then((img) => { if (img) { t.image = img; t.etat = 'ok'; dessiner(); } })
        .catch(() => { tuiles.delete(cle); });  // erreur reseau : nouvel essai au prochain dessin
      return t;
    }

    // Image a dessiner pour la case (z, x, y) : la tuile elle-meme, ou un
    // morceau d'une tuile moins detaillee. null si rien.
    function trouver(z, x, y) {
      for (let k = 0; z - k >= 0 && k <= 6; ++k) {
        const zz = z - k, xx = x >> k, yy = y >> k;
        if (!dansUneZone(zz, xx, yy)) continue;
        const t = demander(zz, xx, yy);
        if (t.etat === 'ok') {
          const part = TUILE / 2 ** k;
          return { image: t.image, sx: (x - (xx << k)) * part, sy: (y - (yy << k)) * part, taille: part };
        }
      }
      return null;
    }

    /* ---------------- Vue ---------------- */

    // Centre de la vue : le bateau, sinon le dernier point de trace
    function centre() {
      if (bateau) return [bateau.x, bateau.y];
      if (points.length) return points[points.length - 1];
      return null;
    }

    // Zoom entier qui garde a l'ecran la trace recente autour du centre
    function choisirZoom(c) {
      let dx = 0, dy = 0;
      for (let i = Math.max(0, points.length - RECENTS); i < points.length; ++i) {
        dx = Math.max(dx, Math.abs(points[i][0] - c[0]));
        dy = Math.max(dy, Math.abs(points[i][1] - c[1]));
      }
      // Pixels du monde a un zoom z : TUILE * 2^z par unite Mercator
      const marge = 0.8;
      let ideal = ZOOM_MAX;
      if (dx > 0 || dy > 0) {
        const zx = dx > 0 ? Math.log2((largeur / 2) * marge / (dx * TUILE)) : ZOOM_MAX;
        const zy = dy > 0 ? Math.log2((hauteur / 2) * marge / (dy * TUILE)) : ZOOM_MAX;
        ideal = Math.min(zx, zy);
      }
      ideal = Math.max(ZOOM_MIN, Math.min(17, ideal));
      // Hysteresis : pas de va-et-vient entre deux zooms
      if (ideal < zoom - 0.05 || ideal >= zoom + 1.25) zoom = Math.floor(ideal);
      zoom = Math.max(ZOOM_MIN, Math.min(ZOOM_MAX, zoom));
    }

    /* ---------------- Dessin ---------------- */

    function dessinerTuiles(cx, cy, echelle) {
      const n = 2 ** zoom;
      const gauche = cx * echelle - largeur / 2, haut = cy * echelle - hauteur / 2;
      const tx0 = Math.floor(gauche / TUILE), ty0 = Math.floor(haut / TUILE);
      const tx1 = Math.floor((gauche + largeur) / TUILE), ty1 = Math.floor((haut + hauteur) / TUILE);
      let aucune = true;
      for (let ty = ty0; ty <= ty1; ++ty) {
        if (ty < 0 || ty >= n) continue;
        for (let tx = tx0; tx <= tx1; ++tx) {
          if (tx < 0 || tx >= n) continue;
          const t = trouver(zoom, tx, ty);
          if (!t) continue;
          aucune = false;
          ctx.drawImage(t.image, t.sx, t.sy, t.taille, t.taille,
            Math.round(tx * TUILE - gauche), Math.round(ty * TUILE - haut), TUILE, TUILE);
        }
      }
      return !aucune;
    }

    function dessinerGrille(cx, cy, echelle, mParPixel) {
      const cible = mParPixel * largeur / 5;
      const pas = ECHELLES_M.find((e) => e >= cible) || ECHELLES_M[ECHELLES_M.length - 1];
      const pasUnite = pas / (mParPixel * echelle);
      const xMin = cx - largeur / 2 / echelle, yMin = cy - hauteur / 2 / echelle;
      ctx.strokeStyle = '#1e2b3a';
      ctx.lineWidth = 1;
      ctx.beginPath();
      for (let x = Math.ceil(xMin / pasUnite) * pasUnite; x < xMin + largeur / echelle; x += pasUnite) {
        const e = Math.round((x - xMin) * echelle) + 0.5;
        ctx.moveTo(e, 0);
        ctx.lineTo(e, hauteur);
      }
      for (let y = Math.ceil(yMin / pasUnite) * pasUnite; y < yMin + hauteur / echelle; y += pasUnite) {
        const e = Math.round((y - yMin) * echelle) + 0.5;
        ctx.moveTo(0, e);
        ctx.lineTo(largeur, e);
      }
      ctx.stroke();
    }

    // Texte sur un fond sombre, lisible sur la carte comme sur la grille
    function etiquette(texte, x, y, alignement, taille) {
      ctx.font = '600 ' + taille + 'px system-ui, sans-serif';
      const l = ctx.measureText(texte).width;
      const x0 = alignement === 'right' ? x - l - 8 : x;
      ctx.fillStyle = 'rgba(10, 15, 21, 0.75)';
      ctx.fillRect(x0, y - taille - 4, l + 8, taille + 8);
      ctx.fillStyle = '#e9f0f7';
      ctx.textAlign = 'left';
      ctx.textBaseline = 'alphabetic';
      ctx.fillText(texte, x0 + 4, y + 1);
    }

    function dessinerEchelle(mParPixel) {
      const cible = mParPixel * largeur / 4;
      let m = ECHELLES_M[0];
      for (const e of ECHELLES_M) if (e <= cible) m = e;
      const px = m / mParPixel;
      const x = 10, y = hauteur - 10, taille = Math.max(11, Math.round(hauteur * 0.035));
      ctx.fillStyle = 'rgba(10, 15, 21, 0.75)';
      ctx.fillRect(x - 4, y - taille - 12, px + 8 + 50, taille + 16);
      ctx.strokeStyle = '#e9f0f7';
      ctx.lineWidth = 2;
      ctx.beginPath();
      ctx.moveTo(x, y - 6); ctx.lineTo(x, y); ctx.lineTo(x + px, y); ctx.lineTo(x + px, y - 6);
      ctx.stroke();
      ctx.fillStyle = '#e9f0f7';
      ctx.font = '600 ' + taille + 'px system-ui, sans-serif';
      ctx.textAlign = 'left';
      ctx.textBaseline = 'alphabetic';
      ctx.fillText(m >= 1000 ? m / 1000 + ' km' : m + ' m', x + px + 6, y);
    }

    function dessinerNord() {
      const x = largeur - 22, y = 24, t = 10;
      ctx.beginPath();
      ctx.arc(x, y + 4, t + 8, 0, Math.PI * 2);
      ctx.fillStyle = 'rgba(10, 15, 21, 0.75)';
      ctx.fill();
      ctx.beginPath();
      ctx.moveTo(x, y - t); ctx.lineTo(x + t * 0.6, y + t); ctx.lineTo(x, y + t * 0.5); ctx.lineTo(x - t * 0.6, y + t);
      ctx.closePath();
      ctx.fillStyle = '#ff4d4f';
      ctx.fill();
    }

    function dessinerTrace(versEcran) {
      if (points.length < 2) return;
      ctx.beginPath();
      let p = versEcran(points[0]);
      ctx.moveTo(p[0], p[1]);
      for (let i = 1; i < points.length; ++i) {
        p = versEcran(points[i]);
        ctx.lineTo(p[0], p[1]);
      }
      if (bateau) {
        p = versEcran([bateau.x, bateau.y]);
        ctx.lineTo(p[0], p[1]);
      }
      ctx.lineJoin = 'round';
      ctx.lineCap = 'round';
      ctx.strokeStyle = 'rgba(255, 255, 255, 0.85)';   // liseré : visible sur l'eau comme a terre
      ctx.lineWidth = 6;
      ctx.stroke();
      ctx.strokeStyle = '#d6006e';
      ctx.lineWidth = 3;
      ctx.stroke();
    }

    function dessinerBateau(versEcran) {
      const [x, y] = versEcran([bateau.x, bateau.y]);
      const s = Math.max(9, Math.min(largeur, hauteur) * 0.04);
      const couleur = bateau.fix ? '#0a84ff' : '#8193a8';
      ctx.save();
      ctx.translate(x, y);
      if (bateau.cap !== null) {
        ctx.rotate((bateau.cap * Math.PI) / 180);
        // Ligne de cap : ou va l'etrave
        ctx.beginPath();
        ctx.moveTo(0, -s * 1.4);
        ctx.lineTo(0, -Math.max(largeur, hauteur));
        ctx.setLineDash([8, 6]);
        ctx.strokeStyle = 'rgba(10, 15, 21, 0.8)';
        ctx.lineWidth = 2;
        ctx.stroke();
        ctx.setLineDash([]);
        // Fleche du bateau
        ctx.beginPath();
        ctx.moveTo(0, -s * 1.4);
        ctx.lineTo(s * 0.85, s);
        ctx.lineTo(0, s * 0.45);
        ctx.lineTo(-s * 0.85, s);
        ctx.closePath();
      } else {
        ctx.beginPath();
        ctx.arc(0, 0, s * 0.65, 0, Math.PI * 2);
      }
      ctx.fillStyle = couleur;
      ctx.strokeStyle = '#ffffff';
      ctx.lineWidth = 2.5;
      ctx.fill();
      ctx.stroke();
      ctx.restore();
      if (!bateau.fix) etiquette('GPS perdu', x - 40, y - s * 1.8, 'left', Math.max(12, Math.round(hauteur * 0.04)));
    }

    function dessiner() {
      if (!largeur || !hauteur) return;
      ctx.setTransform(ratio, 0, 0, ratio, 0, 0);
      ctx.fillStyle = '#0c141d';
      ctx.fillRect(0, 0, largeur, hauteur);
      const c = centre();
      if (!c) return;
      choisirZoom(c);
      const echelle = TUILE * 2 ** zoom;   // pixels par unite Mercator
      const mParPixel = (TERRE_M * Math.cos((latitude(c[1]) * Math.PI) / 180)) / echelle;
      const versEcran = (p) => [largeur / 2 + (p[0] - c[0]) * echelle, hauteur / 2 + (p[1] - c[1]) * echelle];

      dessinerGrille(c[0], c[1], echelle, mParPixel);
      const carte = dessinerTuiles(c[0], c[1], echelle);
      dessinerTrace(versEcran);
      if (bateau) dessinerBateau(versEcran);
      dessinerEchelle(mParPixel);
      dessinerNord();
      if (carte) etiquette('© IGN Plan IGN', largeur - 4, hauteur - 6, 'right', 10);
    }

    function limiter() {
      if (points.length > POINTS_MAX) points = points.filter((_, i) => i % 2 === 0 || i === points.length - 1);
    }

    return {
      redimensionner,
      dessiner,
      /* Zones telechargees, annoncees par le serveur */
      definirZones(liste) {
        zones = (liste || []).map((z) => {
          const [x0, y0] = projeter(z.n, z.o);
          const [x1, y1] = projeter(z.s, z.e);
          return { x0, y0, x1, y1, zmin: z.zmin, zmax: z.zmax };
        });
        tuiles.clear();
        dessiner();
      },
      /* Points du serveur : [[lat_e7, lon_e7], ...] */
      chargerTrace(liste) {
        points = liste.map((p) => projeter(p[0] / 1e7, p[1] / 1e7));
        limiter();
        dessiner();
      },
      ajouterPoint(lat, lon) {
        points.push(projeter(lat, lon));
        limiter();
      },
      /* cap en degres ou null ; fix = 0 : derniere position connue */
      position(lat, lon, cap, fix) {
        const [x, y] = projeter(lat, lon);
        bateau = { x, y, cap, fix };
        dessiner();
      },
      perdu() {
        if (bateau) { bateau.fix = 0; dessiner(); }
      },
      aDesDonnees() { return points.length > 0 || bateau !== null; },
    };
  }

  return { creer };
})();
