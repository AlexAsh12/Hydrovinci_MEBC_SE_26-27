/*
 * Petit bateau 3D dessine en Canvas 2D, sans librairie : une trentaine de
 * faces, eclairage simple, tri par profondeur. Tres leger pour la Pi Zero.
 *
 * Repere du bateau : x = tribord, y = avant, z = haut.
 * Roulis + = tribord vers le bas, tangage + = avant vers le haut.
 */
'use strict';

window.Bateau = (function () {
  /* ---------- Modele ---------- */

  const P = {
    // pont
    pAr: [0.55, -1.25, 0.30], pMi: [0.60, 0.15, 0.30], pEp: [0.40, 0.95, 0.30], etrave: [0, 1.70, 0.32],
    // bouchains (arete coque / fond)
    bAr: [0.52, -1.25, -0.08], bMi: [0.56, 0.15, -0.10], bEp: [0.33, 0.95, -0.02],
    // quille
    qAr: [0, -1.25, -0.30], qMi: [0, 0.15, -0.38], qEp: [0, 0.95, -0.18],
    // cabine
    cArB: [0.32, -0.55, 0.30], cAvB: [0.32, 0.35, 0.30], cArH: [0.28, -0.50, 0.62], cAvH: [0.26, 0.10, 0.62],
  };

  const verts = [];
  const index = {};
  function point(nom, cote) {
    const cle = nom + cote;
    if (!(cle in index)) {
      const p = P[nom];
      index[cle] = verts.length;
      verts.push([p[0] === 0 ? 0 : p[0] * cote, p[1], p[2]]);
    }
    return index[cle];
  }

  const COQUE_B = [214, 64, 64];    // babord : rouge
  const COQUE_T = [52, 186, 106];   // tribord : vert
  const FOND = [58, 66, 80];
  const PONT = [222, 228, 235];
  const CABINE = [44, 92, 150];
  const VITRE = [120, 200, 255];
  const TABLEAU = [190, 196, 205];

  const faces = [];
  function face(noms, couleur, centre) {
    // noms : [nom, cote] ; cote -1 = babord, 1 = tribord
    faces.push({ ids: noms.map((n) => point(n[0], n[1])), couleur, centre });
  }

  const CENTRE_COQUE = [0, 0.1, 0];
  const CENTRE_CABINE = [0, -0.15, 0.45];

  for (const cote of [-1, 1]) {
    const flanc = cote < 0 ? COQUE_B : COQUE_T;
    face([['pAr', cote], ['pMi', cote], ['bMi', cote], ['bAr', cote]], flanc, CENTRE_COQUE);
    face([['pMi', cote], ['pEp', cote], ['bEp', cote], ['bMi', cote]], flanc, CENTRE_COQUE);
    face([['pEp', cote], ['etrave', 1], ['bEp', cote]], flanc, CENTRE_COQUE);
    face([['bAr', cote], ['bMi', cote], ['qMi', 1], ['qAr', 1]], FOND, CENTRE_COQUE);
    face([['bMi', cote], ['bEp', cote], ['qEp', 1], ['qMi', 1]], FOND, CENTRE_COQUE);
    face([['bEp', cote], ['etrave', 1], ['qEp', 1]], FOND, CENTRE_COQUE);
    // cabine : cote, avec la vitre a l'avant
    face([['cArB', cote], ['cAvB', cote], ['cAvH', cote], ['cArH', cote]], CABINE, CENTRE_CABINE);
  }
  // pont
  face([['pAr', -1], ['pMi', -1], ['pEp', -1], ['etrave', 1], ['pEp', 1], ['pMi', 1], ['pAr', 1]], PONT, CENTRE_COQUE);
  // tableau arriere
  face([['pAr', -1], ['pAr', 1], ['bAr', 1], ['qAr', 1], ['bAr', -1]], TABLEAU, CENTRE_COQUE);
  // cabine : toit, pare-brise, arriere
  face([['cArH', -1], ['cAvH', -1], ['cAvH', 1], ['cArH', 1]], CABINE, CENTRE_CABINE);
  face([['cAvH', -1], ['cAvB', -1], ['cAvB', 1], ['cAvH', 1]], VITRE, CENTRE_CABINE);
  face([['cArB', -1], ['cArH', -1], ['cArH', 1], ['cArB', 1]], CABINE, CENTRE_CABINE);

  /* Normales sortantes, calculees une fois */
  function sous(a, b) { return [a[0] - b[0], a[1] - b[1], a[2] - b[2]]; }
  function vect(a, b) { return [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]; }
  function scal(a, b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
  function norme(a) { const l = Math.hypot(a[0], a[1], a[2]) || 1; return [a[0] / l, a[1] / l, a[2] / l]; }

  for (const f of faces) {
    const p = f.ids.map((i) => verts[i]);
    let n = [0, 0, 0];
    for (let i = 1; i + 1 < p.length; ++i) {
      const c = vect(sous(p[i], p[0]), sous(p[i + 1], p[0]));
      n = [n[0] + c[0], n[1] + c[1], n[2] + c[2]];
    }
    const m = [0, 0, 0];
    for (const q of p) { m[0] += q[0] / p.length; m[1] += q[1] / p.length; m[2] += q[2] / p.length; }
    n = norme(n);
    if (scal(n, sous(m, f.centre)) < 0) n = [-n[0], -n[1], -n[2]];
    f.normale = n;
  }

  /* Plan d'eau fixe (reference horizontale) */
  const EAU = [];
  for (let i = 0; i < 48; ++i) {
    const a = (i / 48) * Math.PI * 2;
    EAU.push([Math.cos(a) * 2.1, Math.sin(a) * 2.1, -0.12]);
  }

  /* ---------- Camera ---------- */

  const AZIMUT = (-32 * Math.PI) / 180;    // vue de trois quarts arriere
  const ELEVATION = (20 * Math.PI) / 180;  // un peu au-dessus
  const DISTANCE = 10;
  const LUMIERE = norme([-0.45, -0.55, 0.7]);  // dans le repere camera

  const ca = Math.cos(AZIMUT), sa = Math.sin(AZIMUT);
  const ce = Math.cos(ELEVATION), se = Math.sin(ELEVATION);

  /* Monde -> camera : x a droite, y profondeur, z haut */
  function camera(p, o) {
    const x = p[0] * ca - p[1] * sa;
    const y = p[0] * sa + p[1] * ca;
    o[0] = x;
    o[1] = y * ce - p[2] * se;
    o[2] = p[2] * ce + y * se;
    return o;
  }

  function creer(canvas) {
    const ctx = canvas.getContext('2d', { alpha: false });
    let largeur = 0, hauteur = 0, echelle = 1, ratio = 1, fond = '#111a24';

    const vCam = verts.map(() => [0, 0, 0]);
    const ecran = verts.map(() => [0, 0]);
    const tmp = [0, 0, 0];
    const nCam = [0, 0, 0];
    const visibles = [];

    function redimensionner() {
      ratio = Math.min(window.devicePixelRatio || 1, 2);
      largeur = canvas.clientWidth;
      hauteur = canvas.clientHeight;
      canvas.width = Math.max(1, Math.round(largeur * ratio));
      canvas.height = Math.max(1, Math.round(hauteur * ratio));
      echelle = Math.min(largeur / 4.4, hauteur / 2.7);
      fond = getComputedStyle(canvas.parentElement).backgroundColor || fond;
    }

    function projeter(c, sortie) {
      const f = DISTANCE / (DISTANCE + c[1]);
      sortie[0] = largeur / 2 + c[0] * f * echelle;
      sortie[1] = hauteur * 0.55 - c[2] * f * echelle;
    }

    function dessiner(roulisDeg, tangageDeg) {
      if (!largeur) redimensionner();
      const r = (roulisDeg * Math.PI) / 180, t = (tangageDeg * Math.PI) / 180;
      const cr = Math.cos(r), sr = Math.sin(r), ct = Math.cos(t), st = Math.sin(t);

      ctx.setTransform(ratio, 0, 0, ratio, 0, 0);
      ctx.fillStyle = fond;
      ctx.fillRect(0, 0, largeur, hauteur);

      // Plan d'eau
      ctx.beginPath();
      const e = [0, 0];
      EAU.forEach((p, i) => {
        projeter(camera(p, tmp), e);
        if (i) ctx.lineTo(e[0], e[1]); else ctx.moveTo(e[0], e[1]);
      });
      ctx.closePath();
      ctx.fillStyle = 'rgba(63, 182, 255, 0.10)';
      ctx.fill();
      ctx.strokeStyle = 'rgba(63, 182, 255, 0.45)';
      ctx.lineWidth = 1.5;
      ctx.stroke();

      // Attitude : roulis autour de y, puis tangage autour de x
      for (let i = 0; i < verts.length; ++i) {
        const v = verts[i];
        const x1 = v[0] * cr + v[2] * sr;
        const z1 = -v[0] * sr + v[2] * cr;
        const y2 = v[1] * ct - z1 * st;
        const z2 = v[1] * st + z1 * ct;
        tmp[0] = x1; tmp[1] = y2; tmp[2] = z2;
        camera(tmp, vCam[i]);
        projeter(vCam[i], ecran[i]);
      }

      visibles.length = 0;
      for (const f of faces) {
        const n = f.normale;
        const x1 = n[0] * cr + n[2] * sr;
        const z1 = -n[0] * sr + n[2] * cr;
        tmp[0] = x1; tmp[1] = n[1] * ct - z1 * st; tmp[2] = n[1] * st + z1 * ct;
        camera(tmp, nCam);
        // Centre de la face, vu depuis la camera placee en (0, -DISTANCE, 0)
        let mx = 0, my = 0, mz = 0;
        for (const i of f.ids) { mx += vCam[i][0]; my += vCam[i][1]; mz += vCam[i][2]; }
        const k = f.ids.length;
        mx /= k; my /= k; mz /= k;
        if (nCam[0] * mx + nCam[1] * (my + DISTANCE) + nCam[2] * mz >= 0) continue;
        const lum = 0.38 + 0.62 * Math.max(0, scal(nCam, LUMIERE));
        f.profondeur = my;
        f.teinte = 'rgb(' + Math.round(f.couleur[0] * lum) + ',' + Math.round(f.couleur[1] * lum) + ',' +
          Math.round(f.couleur[2] * lum) + ')';
        visibles.push(f);
      }
      visibles.sort((a, b) => b.profondeur - a.profondeur);

      ctx.lineJoin = 'round';
      ctx.lineWidth = 1;
      for (const f of visibles) {
        ctx.beginPath();
        f.ids.forEach((i, j) => {
          if (j) ctx.lineTo(ecran[i][0], ecran[i][1]); else ctx.moveTo(ecran[i][0], ecran[i][1]);
        });
        ctx.closePath();
        ctx.fillStyle = f.teinte;
        ctx.fill();
        ctx.strokeStyle = 'rgba(8, 12, 18, 0.45)';  // aretes
        ctx.stroke();
      }
    }

    return { dessiner, redimensionner };
  }

  return { creer };
})();
