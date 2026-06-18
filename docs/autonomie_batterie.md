# Autonomie batterie — Guide complet (MEBC 2026)

Tout ce qu'il faut pour comprendre la batterie, faire les calculs, et savoir si le
système tient les **4h de course**. Écrit pour pouvoir refaire les calculs soi-même
avec ses propres mesures.

---

## 1. Le système d'alimentation (notre montage)

```
┌──────────────────┐   15–21V    ┌──────────────┐   5,1V    ┌─────────────────────┐
│ Pack 5S Li-ion   │ ──────────► │ Buck DFRobot │ ────────► │ PDB (5V) :          │
│ 18650 (5 cell.)  │  variable   │  ~88% rdt    │  stable   │  • Écran 7"         │
│ BCPB3 charge/    │             └──────────────┘           │  • SIM7600          │
│ protection +solr │                                        │  • Pi Zero 2W (HAT) │
└──────────────────┘                                        └─────────────────────┘
```

- **Pack** : 5 cellules 18650 Li-ion en **série** (5S). Tension variable 15–21V.
- **Buck (abaisseur)** : transforme la tension variable du pack en **5,1V stable**.
- **PDB** : distribue le 5V (fusible) à l'écran, au SIM7600 et au Pi.
- **Solaire** : recharge le pack via l'entrée 19V (négligé en cas de pluie).

---

## 2. Comment fonctionne une batterie Li-ion

### 2.1 La tension n'est PAS constante

Une batterie n'est pas une source de tension fixe. Sa tension dépend de son **état de
charge** (et un peu du courant tiré). Pour **une cellule** Li-ion :

| Par cellule | État de charge |
|------------:|----------------|
| 4,20 V      | 100 % (pleine) |
| 3,70 V      | ~50 % (nominale)|
| 3,50 V      | ~20 %          |
| 3,00 V      | 0 % (coupure)  |

### 2.2 Série (S) et parallèle (P)

- **En série** : les **tensions s'additionnent**, la capacité (Ah) **ne change pas**.
- **En parallèle** : les **capacités s'additionnent**, la tension **ne change pas**.

Notre pack est **5S** (5 en série, 1 seul "étage" → 5S1P) :

| Grandeur | 1 cellule | Pack 5S |
|----------|-----------|---------|
| Plein    | 4,20 V    | **21,0 V** |
| Nominal  | 3,70 V    | **18,5 V** |
| Vide     | 3,00 V    | **15,0 V** |
| Capacité | Q (Ah)    | **Q (Ah)** (inchangée en série) |

> ⚠️ **Lecture de jauge** : mesurer la tension du pack au voltmètre = lire sa charge.
> 15,8 V sur ce pack ≈ **3,16 V/cellule ≈ presque vide**. Un pack plein doit lire ~20–21 V.

### 2.3 La tension chute sous charge

Quand on tire du courant, la tension baisse temporairement (résistance interne).
C'est le cas lors des **pics du SIM7600** (jusqu'à ~2 A en émission) : le rail peut
plonger et faire **redémarrer le Pi (brownout)** ou le modem. D'où les
**condensateurs bulk** près du SIM7600 et le réglage du buck à **5,1V** (marge).

### 2.4 La charge : phases CC puis CV

- **CC (Courant Constant)** : le chargeur pousse un courant fixe, la tension monte.
  Remplit ~70–80 % **vite** → la jauge grimpe vite jusqu'à ~4/5.
- **CV (Tension Constante)** : à 4,2 V/cellule (21 V), le chargeur **maintient** la
  tension et le courant **diminue** tout seul. Le dernier ~20–30 % rentre **lentement**
  → le 4/5 → 5/5 traîne. **C'est normal et sain** (protège les cellules, les équilibre).

---

## 3. Les grandeurs et leurs unités

| Symbole | Nom | Unité | C'est quoi |
|---------|-----|-------|-----------|
| `V` | Tension | Volt (V) | "pression" électrique |
| `I` | Courant | Ampère (A) | débit d'électricité |
| `Q` | Capacité | Ampère-heure (Ah) | "réservoir" de charge |
| `E` | Énergie | Watt-heure (Wh) | énergie totale stockée |
| `P` | Puissance | Watt (W) | énergie consommée par seconde |
| `η` | Rendement | % | efficacité du buck (~0,88) |

**Les 3 relations de base :**

```
P = V × I              (puissance = tension × courant)
E = V × Q              (énergie = tension × capacité)
t = E / P              (durée = énergie / puissance)
```

> 💡 On raisonne en **énergie (Wh)** plutôt qu'en courant (A), parce que l'énergie se
> conserve même quand la tension du pack baisse. Quand le pack se vide (V baisse), le
> buck tire **plus de courant** pour garder la même puissance — mais l'énergie totale
> disponible, elle, ne dépend pas de ça.

---

## 4. Les formules d'autonomie (les 5 étapes)

### Étape 1 — Énergie totale du pack
```
E_batt (Wh) = V_nominal (V) × Q (Ah)
```
- `V_nominal` = 3,7 × 5 = **18,5 V**
- `Q` = capacité d'**une** cellule (en 5S1P la capacité ne s'additionne pas)

### Étape 2 — Énergie réellement utilisable
On ne vide pas une Li-ion à 0 (coupure ~3,0 V). On exploite ~85 % :
```
E_utile (Wh) = E_batt × 0,85
```
`0,85` = **profondeur de décharge** (DoD) raisonnable pour préserver les cellules.

### Étape 3 — Puissance consommée par les appareils (côté 5V)
```
P_charge (W) = P_ecran + P_pi + P_sim
```

### Étape 4 — Puissance tirée du pack (avec pertes du buck)
Le buck gaspille ~12 %, donc le pack fournit **plus** que la charge :
```
P_pack (W) = P_charge / η_buck     avec η_buck ≈ 0,88
```
> **`P_pack`** = puissance fournie par la batterie elle-même (avant le buck).
> **`P_charge`** = puissance reçue par les appareils (après le buck).
> `P_pack` > `P_charge` à cause des pertes.

### Étape 5 — Autonomie
```
Autonomie (h) = E_utile (Wh) / P_pack (W)
```

---

## 5. Exemple chiffré (à adapter avec tes valeurs)

Hypothèses (à remplacer par tes mesures) :
- Cellules 18650 : **2,0 Ah** (hypothèse prudente, non marquées)
- Conso : écran 3,0 W + Pi 1,0 W + SIM 0,4 W = **4,4 W**

```
Étape 1 : E_batt   = 18,5 × 2,0      = 37,0 Wh
Étape 2 : E_utile  = 37,0 × 0,85     = 31,4 Wh
Étape 3 : P_charge = 3,0 + 1,0 + 0,4 = 4,4 W
Étape 4 : P_pack   = 4,4 / 0,88      = 5,0 W
Étape 5 : Autonomie = 31,4 / 5,0     = 6,3 h   ✅ > 4h
```

Scénario pessimiste (2,0 Ah + charge lourde 6 W) :
```
P_pack    = 6,0 / 0,88   = 6,8 W
Autonomie = 31,4 / 6,8   = 4,6 h            ✅ encore > 4h
```

---

## 6. "Est-ce que ça va le faire ?" — le critère 4h

### Méthode directe (de combien j'ai besoin)
```
E_besoin (Wh) = P_pack × 4h
E_besoin = 5,0 × 4 = 20 Wh utiles
```
Capacité minimale nécessaire :
```
Q_min (Ah) = E_besoin / 0,85 / V_nominal
Q_min = 20 / 0,85 / 18,5 ≈ 1,3 Ah
```
👉 **Conclusion : toute cellule 18650 saine ≥ 2 Ah tient les 4h.** La capacité n'est
pas le risque. Les vrais risques : pack **dégradé** (< ~1,7 Ah réel) ou pack **pas
plein au départ**.

---

## 7. Comment MESURER les vraies valeurs (mieux que les estimations)

Les conso estimées (§5) sont à ±30 %. Pour du concret :

### 7.1 Mesurer `P_pack` (côté batterie) — le plus utile
Brancher un **wattmètre inline** sur le fil **batterie → buck**. Il affiche
directement `V`, `I`, `P` (et souvent `Wh` cumulés).
```
P_pack = V_batterie × I_batterie
```

### 7.2 Mesurer `P_charge` (côté 5V)
Un **wattmètre USB** sur la ligne 5V donne la conso des appareils. Puis :
```
P_pack = P_charge / 0,88
```

### 7.3 Test de décharge complet (LE test définitif)
Mesure capacité + conso + pertes **d'un coup**, et vérifie la santé du pack :
1. Charger à fond (5/5 ≈ 21 V).
2. Wattmètre inline (cumul Wh) sur la ligne batterie.
3. Faire tourner le **système réel** jusqu'à coupure (~15 V).
4. Lire le **total Wh** = ton `E_utile` réelle, sans hypothèse.
5. `Autonomie = E_utile_mesurée / P_pack`.

> Si le pack sort **≥ 20 Wh**, les 4h sont garanties.
> S'il sort **beaucoup moins que ~30 Wh**, des cellules sont fatiguées.

---

## 8. Recommandations pour la course

1. **Vérifier la santé du pack** : à pleine charge il doit lire ~20–21 V. Sinon,
   cellules dégradées → c'est ce qui ferait rater les 4h.
2. **Partir à 5/5** (pas 4/5) : la phase CV ajoute un vrai bout d'autonomie.
3. **Buck réglé à ~5,1 V** (pas 4,9 V) : marge contre les chutes de tension. Le Pi
   tolère jusqu'à 5,25 V.
4. **Condensateurs bulk** près du SIM7600 (déjà présents sur la PDB) : absorbent les
   pics de courant en émission → évitent les brownouts/reboots.
5. **Faire le test de décharge §7.3 une fois** avant la course : ça lève tous les doutes.

---

## 9. Aide-mémoire (toutes les formules)

```
P = V × I                              puissance
E = V × Q                              énergie d'un pack
                                       (série : V s'additionne, Q inchangée)

E_batt    = V_nominal × Q              énergie totale       [V_nom 5S = 18,5 V]
E_utile   = E_batt × 0,85             énergie utilisable   [DoD 85 %]
P_charge  = P_ecran + P_pi + P_sim    conso appareils (5V)
P_pack    = P_charge / 0,88           conso côté batterie  [rdt buck 88 %]
Autonomie = E_utile / P_pack          en heures

E_besoin  = P_pack × heures_visées    énergie requise
Q_min     = E_besoin / 0,85 / V_nom   capacité minimale
```
