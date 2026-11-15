# Moonlight Vita : bêta « phase 2 »

Version de test de ce fork de vita-moonlight, branche `phase2`.
Téléchargement (toujours la dernière compilation) :
https://github.com/LouisF37/vita-moonlight/releases/download/build-phase2/moonlight-phase2.vpk

L'écran d'accueil affiche la version exacte en bas à gauche, par exemple `v0.13.2  phase2-xxxxxxx`.

**État des vérifications.** Le code compile pour la Vita et tous les écrans ont été rendus hors console
(outil `tools/ui-preview`) en anglais, français et espagnol. Rien n'a encore tourné sur une vraie Vita :
la fluidité, la latence et le son restent à mesurer avec le plan de test ci-dessous.

## Prise en main

**Accueil.** La liste des PC appairés avec leur état (en ligne, hors ligne, adresse modifiée), et à droite
le détail du PC sélectionné : adresse, appairage, réveil à distance, flux demandé, et un conseil.
- ✕ sur un PC en ligne : connexion directe. ✕ sur un PC hors ligne : ouvre son menu.
- □ : menu du PC (se connecter, réveiller, modifier l'adresse, renommer, oublier).
- △ : relance la recherche des PC.

**Ajouter un PC.** « Rechercher sur le réseau » ou « Saisir une adresse ». La Vita affiche un code à
4 chiffres et l'adresse de la page web de Sunshine (`https://adresse-du-PC:47990`, rubrique PIN) où le saisir.

**Bibliothèque.** Après la connexion, les jeux s'affichent en rangée de jaquettes, avec les chiffres de
la dernière session du jeu sélectionné.
- ✕ jouer ou reprendre, □ quitter le jeu en cours sur le PC, △ ancien menu, START réglages, ○ retour.
- Croix directionnelle ou écran tactile pour choisir ; L et R avancent de 5 jeux.

**Pendant le stream.** START + L + R met le stream en pause et revient aux menus.

**Réglages.** Six rubriques à gauche, les options à droite, une explication de l'option sélectionnée en bas.
- Haut / bas : option. Gauche / droite : changer la valeur. ✕ : basculer, valeur suivante ou ouvrir.
- L / R : rubrique précédente / suivante. ○ : retour (les réglages sont enregistrés).
- Langue : English, Français, Español (rubrique Général).

## Performance (Réglages → Performance)

`Profil` règle d'un coup les trois options qui changent le plus le rendu :

| Profil | Affichage | Son à faible latence | Rattrapage du décodeur |
|---|---|---|---|
| Fluide (par défaut) | Fluide : une image par rafraîchissement, dessinée juste à temps | Oui | Oui |
| Latence minimale | Faible latence : la dernière image tout de suite | Oui | Oui |
| Original | Direct : le code d'origine | Non | Non |

Autres options de la rubrique : vitesse maximale du CPU et du GPU (444 / 222 MHz), suivi de la fréquence
exacte de l'écran, taille des paquets vidéo (1392 par défaut, 1024 à l'origine).

Rubrique Vidéo : 960 x 544 par défaut (la définition de l'écran de la Vita) et débit `Auto`
(8 Mbps en 960 x 544 à 60 img/s, 10 Mbps en 1280 x 720, réduit à 30 img/s).

Rubrique Avancé, deux options de test :
- `Lissage des contours` (activé) : anticrénelage 4x des menus et du stream. Son coût sur le dessin du
  stream n'est pas mesuré ; pour le mesurer, comparer la colonne `render_avg_us` avec et sans
  (redémarrer Moonlight après le changement).
- `Garder la liaison avec le PC ouverte` (désactivé) : évite de rouvrir une connexion sécurisée à chaque
  requête des menus (liste des jeux, jaquettes). Expérimental.

## Plan de test

Même jeu, même endroit, 3 à 5 minutes par session, statistiques affichées (Général → Statistiques
pendant le stream) :
1. Profil **Fluide**.
2. Profil **Latence minimale**.
3. Profil **Original** (référence : le comportement d'avant).
4. Facultatif : Fluide avec `Lissage des contours` désactivé (après redémarrage de Moonlight).

Chaque session crée deux fichiers dans `ux0:data/moonlight/stats/` : un `.csv` (une ligne par seconde)
et un `.txt` (réglages de la session, signal Wi-Fi, légende des colonnes). Ce sont ces fichiers qu'il
faut renvoyer, avec une phrase sur le ressenti (saccades, retard des commandes, son).

Colonnes à regarder en premier : `shown` (images affichées par seconde), `lost`, `r2p_avg_us`
(premier paquet reçu → image visible), `late`, `aud_underrun`, `wifi_pct`.

## Réglages conseillés

**Vita.** Paramètres → Paramètres d'économie d'énergie : décocher l'utilisation du Wi-Fi en mode économie
d'énergie. Moonlight lit ce réglage et affiche un avertissement sur l'accueil s'il le trouve activé
(lecture du registre non encore vérifiée sur console).

**Sunshine** (interface web : `https://localhost:47990`).
- `minimum_fps_target` = 60 : cadence régulière, même quand l'image bouge peu.
- `lan_encryption_mode` = 0 (valeur par défaut) : pas de chiffrement en réseau local.
- À tester ensuite, un par un : `qsv_preset` = veryfast, `qsv_coder` = cavlc, `fec_percentage` = 30 si des images sont perdues.

## Ajouter ses jeux Steam (et PS1) à Sunshine

Le script `tools/sunshine-import.ps1` ajoute les jeux Steam installés, avec leur jaquette, à la liste de
Sunshine. Il ne touche pas aux jeux déjà présents et sauvegarde `apps.json` avant d'écrire.

Dans un PowerShell lancé en administrateur, depuis le dossier du script :
```
powershell -ExecutionPolicy Bypass -File .\sunshine-import.ps1 -DryRun
powershell -ExecutionPolicy Bypass -File .\sunshine-import.ps1
```
`-DryRun` affiche ce qui serait ajouté sans rien écrire.

Pour des jeux PS1 avec DuckStation, ajouter :
```
-DuckStationExe "C:\chemin\duckstation-qt-x64-ReleaseLTCG.exe" -Ps1Folder "D:\PS1"
```
Fichiers reconnus : `.cue`, `.chd`, `.m3u`, `.pbp`. Une image `.png` ou `.jpg` du même nom à côté du jeu sert de jaquette.

## En cas de problème

- Image saccadée, figée ou absente : profil **Original**.
- Son qui craque : désactiver `Son à faible latence`.
- Menus lents ou erreurs de connexion après avoir activé `Garder la liaison avec le PC ouverte` : la désactiver.
- Le PC n'apparaît pas en ligne alors qu'il est allumé : □ sur le PC, puis « Se connecter » (l'essai
  s'arrête au bout de quelques secondes si le PC ne répond pas).
