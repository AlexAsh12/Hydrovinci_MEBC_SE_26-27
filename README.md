# Hydrovinci - MEBC SE (Monaco Energy Boat Challenge)

Systeme embarque de telemetrie et controle pour bateau electrique solaire.

Le depot est organise **par saison** : chaque annee a son propre dossier.

## Organisation

```
/
|-- 2025-2026/     # Projet de la saison 2025-2026 (Monaco 2026) - archive
|-- .gitignore     # Commun a toutes les saisons
|-- README.md      # Ce fichier
```

| Dossier | Saison | Statut |
|---|---|---|
| [2025-2026/](2025-2026/) | 2025-2026 (Monaco 2026) | Archive - code tel qu'il etait en fin de saison |

## Utiliser le projet 2025-2026

Toutes les commandes du [README 2025-2026](2025-2026/README.md) se lancent
**depuis le dossier `2025-2026/`** :

```bash
# Pi Zero (embarque)
cd 2025-2026/setup
sudo bash setup_pi.sh
```

```bash
# PC au sol (dashboard)
cd 2025-2026/ground
docker compose up
```

> La branche `main` conserve l'ancienne organisation (fichiers a la racine).
> La branche `saison-2026-2027` repart de cette nouvelle organisation par saison.
