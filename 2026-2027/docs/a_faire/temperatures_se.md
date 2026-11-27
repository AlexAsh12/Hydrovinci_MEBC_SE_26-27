# À faire : températures du système embarqué (SE)

Statut : **demandé, pas encore implémenté**.

## Besoin

Dans le panneau **Températures** de l'écran, sous les sondes DS18B20, un
petit bloc « Système » avec :

1. la température du **module 4G SIM7600** ;
2. plus tard, si on le veut : la température du **processeur de la Pi**.

Règles du tableau de bord à respecter :
- uniquement des valeurs réelles et actuelles : sans mesure récente, la
  ligne affiche « — » (jamais une ancienne valeur) ;
- une panne ici ne doit gêner ni les sondes ni le reste de l'écran ;
- coût négligeable : pas de mémoire en plus, une mesure toutes les 30 s.

## 1. Température du SIM7600

- Commande AT : `AT+CPMUTEMP` → réponse `+CPMUTEMP: 35` (°C entiers).
  **À vérifier sur notre module** dès qu'il répond en USB : la commande
  existe dans le manuel AT SIM7500/SIM7600, mais n'a pas encore été essayée.
- `src/sim_analyse.c` : ajouter `sa_cpmutemp(ligne, &celsius)` (renvoie 1 si
  reconnue, valeur plausible entre -40 et 125).
- `src/sim_moteur.c` : nouvelle commande `CMD_CPMUTEMP`, planifiée toutes les
  30 s quand le module est prêt (priorité basse, après `AT+COPS?`) ; stocker
  `temp_c` et l'instant de la mesure dans `sim_moteur_t` (une valeur, pas de
  tableau). Si le module répond `ERROR`, ne plus la demander (module qui ne
  la connaît pas).
- `src/tableau_bord.c` : ajouter `"temp_sim":35` (ou `-1` si pas de mesure
  depuis 90 s) dans l'événement `sim` déjà envoyé chaque seconde : aucun
  nouvel événement.
- Tests : `tests/test_pi.c` (analyse de la réponse, faux module qui répond
  à `AT+CPMUTEMP`) et `tests/test_robustesse.c` (réponses abîmées).

## 2. Température du processeur de la Pi (optionnel, plus tard)

- Lire `/sys/class/thermal/thermal_zone0/temp` (millièmes de °C) toutes les
  5 s dans `tableau_bord.c` : pas besoin de `vcgencmd`, pas de processus lancé.
- Ajouter `"temp_pi":46.2` dans l'événement `etat` (déjà envoyé chaque seconde).
- Repère : la Pi ralentit d'elle-même vers 80-85 °C ; aujourd'hui 46 °C avec
  l'écran branché.

## 3. Affichage (`web/index.html`, `web/tableau.js`, `web/tableau.css`)

- Dans `#p-temp`, sous `#sondes` : un bloc `Système` avec une ligne par
  valeur, même style que les sondes (`.sonde`), valeur « — » sans mesure.
- Couleurs : orange au-dessus de 70 °C, rouge au-dessus de 80 °C.
- Le bloc Système ne vient pas de l'ESP32 : il doit rester visible sans lui.
  Le voile opaque « Pas de données » ne doit donc couvrir que la liste des
  sondes, pas tout le panneau.
- Mise à jour dans `surSim()` et dans le traitement de `etat`, chacun dans
  son propre `isole(...)`.
