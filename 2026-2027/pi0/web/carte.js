/*
 * Carte GPS : trace du bateau, position et cap, nord en haut.
 *
 * Sans fond (pas de carte.conf) : la vue suit la trace, avec une grille et
 * une echelle. Avec fond : l'image du plan d'eau (export OpenStreetMap ou
 * IGN, projection Web Mercator) remplit la vue et la trace s'y superpose.
 *
 * Redessinee seulement quand une position arrive (1 Hz) ou que la taille
 * change : presque rien pour le processeur de la Pi.
 */
'use strict';

window.Carte = (function () {
  const TERRE_M = 40075016.686;   // circonference a l'equateur
  const POINTS_MAX = 4000;
  const ECHELLES_M = [5, 10, 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000, 50000];

  // Web Mercator normalise : x, y entre 0 et 1
  function projeter(lat, lon) {
    const phi = (Math.max(-85, Math.min(85, lat)) * Math.PI) / 180;
    return [(lon + 180) / 360, (1 - Math.log(Math.tan(phi) + 1 / Math.cos(phi)) / Math.PI) / 2];
  }

  function latitude(y) {
    return (Math.atan(Math.sinh(Math.PI * (1 - 2 * y))) * 180) / Math.PI;
  }

  // Metres par unite Mercator a la latitude de y
  function metresParUnite(y) {
    return TERRE_M * Math.cos((latitude(y) * Math.PI) / 180);
  }

  function creer(canvas) {
    const ctx = canvas.getContext('2d', { alpha: false });
    let largeur = 0, hauteur = 0, ratio = 1;
    let fond = null;                 // { image, x0, y0, x1, y1 } en Mercator
    let points = [];                 // [x, y] Mercator
    let bateau = null;               // { x, y, cap, fix }
    let couleurFond = '#0c141d';

    function redimensionner() {
      ratio = Math.min(window.devicePixelRatio || 1, 2);
      largeur = canvas.clientWidth;
      hauteur = canvas.clientHeight;
      canvas.width = Math.max(1, Math.round(largeur * ratio));
      canvas.height = Math.max(1, Math.round(hauteur * ratio));
      dessiner();
    }

    /* Vue : rectangle Mercator a afficher, agrandi pour garder les proportions */
    function vue() {
      let x0, y0, x1, y1;
      if (fond) {
        ({ x0, y0, x1, y1 } = fond);
      } else {
        const tous = bateau ? points.concat([[bateau.x, bateau.y]]) : points;
        if (!tous.length) return null;
        x0 = x1 = tous[0][0];
        y0 = y1 = tous[0][1];
        for (const p of tous) {
          if (p[0] < x0) x0 = p[0];
          if (p[0] > x1) x1 = p[0];
          if (p[1] < y0) y0 = p[1];
          if (p[1] > y1) y1 = p[1];
        }
        // Au moins 150 m de cote, plus une marge de 15 %
        const cx = (x0 + x1) / 2, cy = (y0 + y1) / 2;
        const min = 150 / metresParUnite(cy);
        const dx = Math.max(x1 - x0, min) * 1.3 / 2, dy = Math.max(y1 - y0, min) * 1.3 / 2;
        x0 = cx - dx; x1 = cx + dx; y0 = cy - dy; y1 = cy + dy;
      }
      // Meme echelle en x et en y : on elargit le cote trop court
      const echelle = Math.min(largeur / (x1 - x0), hauteur / (y1 - y0));
      const cx = (x0 + x1) / 2, cy = (y0 + y1) / 2;
      return { echelle, cx, cy };
    }

    function versEcran(v, x, y) {
      return [largeur / 2 + (x - v.cx) * v.echelle, hauteur / 2 + (y - v.cy) * v.echelle];
    }

    function dessinerGrille(v, mParPixel) {
      // Pas de grille : environ 5 lignes sur la largeur
      const cible = mParPixel * largeur / 5;
      const pas = ECHELLES_M.find((e) => e >= cible) || ECHELLES_M[ECHELLES_M.length - 1];
      const pasUnite = pas / (mParPixel * v.echelle);
      const xMin = v.cx - largeur / 2 / v.echelle, yMin = v.cy - hauteur / 2 / v.echelle;
      ctx.strokeStyle = '#18242f';
      ctx.lineWidth = 1;
      ctx.beginPath();
      for (let x = Math.ceil(xMin / pasUnite) * pasUnite; x < xMin + largeur / v.echelle; x += pasUnite) {
        const e = versEcran(v, x, 0)[0];
        ctx.moveTo(e, 0);
        ctx.lineTo(e, hauteur);
      }
      for (let y = Math.ceil(yMin / pasUnite) * pasUnite; y < yMin + hauteur / v.echelle; y += pasUnite) {
        const e = versEcran(v, 0, y)[1];
        ctx.moveTo(0, e);
        ctx.lineTo(largeur, e);
      }
      ctx.stroke();
    }

    function dessinerEchelle(mParPixel) {
      const cible = mParPixel * largeur / 4;
      let m = ECHELLES_M[0];
      for (const e of ECHELLES_M) if (e <= cible) m = e;
      const px = m / mParPixel;
      const x = 12, y = hauteur - 12;
      ctx.strokeStyle = '#e9f0f7';
      ctx.lineWidth = 2;
      ctx.beginPath();
      ctx.moveTo(x, y - 5); ctx.lineTo(x, y); ctx.lineTo(x + px, y); ctx.lineTo(x + px, y - 5);
      ctx.stroke();
      ctx.fillStyle = '#e9f0f7';
      ctx.font = '600 ' + Math.max(11, Math.round(hauteur * 0.035)) + 'px system-ui, sans-serif';
      ctx.textAlign = 'left';
      ctx.textBaseline = 'bottom';
      ctx.fillText(m >= 1000 ? m / 1000 + ' km' : m + ' m', x + 4, y - 6);
    }

    function dessinerNord() {
      const x = largeur - 20, y = 22, t = 9;
      ctx.beginPath();
      ctx.moveTo(x, y - t); ctx.lineTo(x + t * 0.6, y + t); ctx.lineTo(x, y + t * 0.5); ctx.lineTo(x - t * 0.6, y + t);
      ctx.closePath();
      ctx.fillStyle = '#ff4d4f';
      ctx.fill();
      ctx.font = '700 11px system-ui, sans-serif';
      ctx.textAlign = 'center';
      ctx.textBaseline = 'top';
      ctx.fillText('N', x, y + t + 2);
    }

    function dessinerBateau(v) {
      const [x, y] = versEcran(v, bateau.x, bateau.y);
      const couleur = bateau.fix ? '#3fb6ff' : '#8193a8';
      const s = Math.max(8, Math.min(largeur, hauteur) * 0.035);
      ctx.save();
      ctx.translate(x, y);
      if (bateau.cap !== null) {
        ctx.rotate((bateau.cap * Math.PI) / 180);
        ctx.beginPath();
        ctx.moveTo(0, -s * 1.4);
        ctx.lineTo(s * 0.8, s);
        ctx.lineTo(0, s * 0.5);
        ctx.lineTo(-s * 0.8, s);
        ctx.closePath();
      } else {
        ctx.beginPath();
        ctx.arc(0, 0, s * 0.6, 0, Math.PI * 2);
      }
      ctx.fillStyle = couleur;
      ctx.strokeStyle = '#0a0f15';
      ctx.lineWidth = 2;
      ctx.fill();
      ctx.stroke();
      ctx.restore();
      if (!bateau.fix) {
        ctx.fillStyle = '#f5a623';
        ctx.font = '700 ' + Math.max(12, Math.round(hauteur * 0.045)) + 'px system-ui, sans-serif';
        ctx.textAlign = 'center';
        ctx.textBaseline = 'bottom';
        ctx.fillText('GPS perdu', x, y - s * 1.6);
      }
    }

    function dessiner() {
      if (!largeur || !hauteur) return;
      ctx.setTransform(ratio, 0, 0, ratio, 0, 0);
      ctx.fillStyle = couleurFond;
      ctx.fillRect(0, 0, largeur, hauteur);
      const v = vue();
      if (!v) return;
      const mParPixel = metresParUnite(v.cy) / v.echelle;

      if (fond && fond.image.complete && fond.image.naturalWidth) {
        const [ax, ay] = versEcran(v, fond.x0, fond.y0);
        const [bx, by] = versEcran(v, fond.x1, fond.y1);
        ctx.drawImage(fond.image, ax, ay, bx - ax, by - ay);
      } else {
        dessinerGrille(v, mParPixel);
      }

      if (points.length > 1) {
        ctx.beginPath();
        let p = versEcran(v, points[0][0], points[0][1]);
        ctx.moveTo(p[0], p[1]);
        for (let i = 1; i < points.length; ++i) {
          p = versEcran(v, points[i][0], points[i][1]);
          ctx.lineTo(p[0], p[1]);
        }
        if (bateau) {
          p = versEcran(v, bateau.x, bateau.y);
          ctx.lineTo(p[0], p[1]);
        }
        ctx.strokeStyle = fond ? '#ff4d4f' : '#2fd27a';
        ctx.lineWidth = 2.5;
        ctx.lineJoin = 'round';
        ctx.stroke();
      }
      if (bateau) dessinerBateau(v);
      dessinerEchelle(mParPixel);
      dessinerNord();
    }

    function limiter() {
      if (points.length > POINTS_MAX) points = points.filter((_, i) => i % 2 === 0 || i === points.length - 1);
    }

    return {
      redimensionner,
      dessiner,
      /* Bornes en degres ; null = pas de fond */
      definirFond(b) {
        if (!b) { fond = null; dessiner(); return; }
        const [x0, y0] = projeter(b.nord, b.ouest);
        const [x1, y1] = projeter(b.sud, b.est);
        const image = new Image();
        image.onload = dessiner;
        image.src = '/carte/fond';
        fond = { image, x0, y0, x1, y1 };
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
      couleur(c) { couleurFond = c; },
    };
  }

  return { creer };
})();
