# Moonlight Vita : bêta « phase 2 »

Version de test de ce fork de vita-moonlight, branche `phase2`.
Téléchargement (toujours la dernière compilation) :
https://github.com/LouisF37/vita-moonlight/releases/download/build-phase2/moonlight-phase2.vpk

L'écran d'accueil affiche la version exacte en bas à gauche, par exemple `v0.13.2  phase2-xxxxxxx`.

**État des vérifications.** Le code compile pour la Vita et tous les écrans ont été rendus hors console
(outil `tools/ui-preview`) en anglais, français et espagnol. Rien n'a encore tourné sur une vraie Vita :
la fluidité, la latence et le son restent à mesurer avec le plan de test ci-dessous.

**Bibliothèque réseau.** moonlight-common-c est intégré au dépôt (`third_party/moonlight-common-c`, version
amont e2722dc) avec trois correctifs listés dans `third_party/patches/moonlight-common-c/` : attente des
paquets en retard (juanjsebgarcia), crochets Vita (priorité et cœur des fils, compteurs).

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

`Profil` règle d'un coup les options qui changent le plus le rendu :

| Profil | Affichage | Son faible latence, rattrapage du décodeur, décodage rapide des couleurs, répartition sur les cœurs | Attente des paquets en retard |
|---|---|---|---|
| Fluide (par défaut) | Fluide : une image par rafraîchissement, dessinée juste à temps | Oui | 20 ms |
| Cadencé | Cadencé : chaque image à l'instant où le PC l'a capturée, plus un petit délai | Oui | 20 ms |
| Latence minimale | Faible latence : la dernière image tout de suite | Oui | 20 ms |
| Original | Direct : le code d'origine | Non | Non |

Options reprises de deux forks orientés performance, avec les mesures publiées par leurs auteurs
(sur leur Vita et leur réseau, pas encore sur les nôtres) :
- `Décodage rapide des couleurs` : le décodeur sort l'image en YUV et la puce graphique fait la conversion
  des couleurs. Décodage mesuré de 4,6 à 2,4 ms par image (juanjsebgarcia/vita-moonlight).
- `Attendre les paquets en retard` (Non, 10, 20, 30 ms) : le Wi-Fi livre des paquets dans le désordre ;
  sans attente, une image complète est jetée et l'image se fige. Avec 20 ms : de 23-37 à 1-3 gels par
  minute avec un signal à environ 60 % (juanjsebgarcia/vita-moonlight).
- `Cadencé` : affichage au rythme des horodatages du PC (dubleyu/vita-moonlight-relay, délai fixe de
  12 ms). Ici le délai part de 12 ms et s'adapte entre 8 et 33 ms selon les retards constatés.
- `Taille des paquets vidéo` : 1024 (valeur de Moonlight) par défaut, 1200 ou 1392 au choix. Mesuré par
  juanjsebgarcia : 1392 ne reçoit pas plus vite et perd plus de paquets (59 contre 43 par minute).
- Rubrique Avancé, `Répartir le travail sur les cœurs` : réception réseau sur le cœur 1, décodage vidéo et
  son sur le cœur 2, affichage et commandes sur le cœur 0 (répartition de dubleyu/vita-moonlight-relay).

Toujours actifs, sans option : décodage vidéo et son hors du fil de réception réseau (le réseau n'attend
plus le décodeur), file de réception de 512 paquets, correction d'erreurs (Reed-Solomon) accélérée NEON,
compilation optimisée (-O2 ; la version précédente était compilée sans optimisation), délais maximaux
sur les requêtes au PC.

Autres options de la rubrique : vitesse maximale du CPU et du GPU (444 / 222 MHz ; désormais aussi pendant
la connexion au PC, et avec le Wi-Fi en mode performance) et suivi de la fréquence exacte de l'écran.

Rubrique Vidéo : 960 x 544 par défaut (la définition de l'écran de la Vita) et débit `Auto`
(8 Mbps en 960 x 544 à 60 img/s, 10 Mbps en 1280 x 720, réduit à 30 img/s).

Rubrique Avancé, deux autres options de test :
- `Lissage des contours` (activé) : anticrénelage 4x des menus et du stream. Son coût sur le dessin du
  stream n'est pas mesuré ; pour le mesurer, comparer la colonne `render_avg_us` avec et sans
  (redémarrer Moonlight après le changement).
- `Garder la liaison avec le PC ouverte` (désactivé) : évite de rouvrir une connexion sécurisée à chaque
  requête des menus (liste des jeux, jaquettes). Expérimental.

## Plan de test

Même jeu, même endroit, 3 à 5 minutes par session, statistiques affichées (Général → Statistiques
pendant le stream) :
1. Profil **Fluide**.
2. Profil **Cadencé**.
3. Profil **Latence minimale**.
4. Profil **Original** (référence : le comportement d'avant).
5. Puis, en partant de Fluide, une option changée à la fois : `Attendre les paquets en retard` à Non puis
   30 ms ; `Décodage rapide des couleurs` désactivé ; `Répartir le travail sur les cœurs` désactivé ;
   `Taille des paquets vidéo` à 1392.
6. Facultatif : Fluide avec `Lissage des contours` désactivé (après redémarrage de Moonlight).

Chaque session crée deux fichiers dans `ux0:data/moonlight/stats/` : un `.csv` (une ligne par seconde)
et un `.txt` (réglages de la session, signal Wi-Fi, légende des colonnes). Ce sont ces fichiers qu'il
faut renvoyer, avec une phrase sur le ressenti (saccades, retard des commandes, son).

Colonnes à regarder en premier : `shown` (images affichées par seconde), `lost`, `r2p_avg_us`
(premier paquet reçu → image visible), `late`, `aud_underrun`, `wifi_pct`. Nouvelles colonnes :
`reorder_held` / `reorder_saved` (paquets attendus / images sauvées par l'attente), `fec_saved` /
`fec_failed` (images réparées / irréparables par la correction d'erreurs), `pkt_ooo` (paquets arrivés
dans le désordre), `pace_delay_us` (délai du mode Cadencé).

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

- Image saccadée, figée ou absente : profil **Original**. Couleurs fausses : désactiver
  `Décodage rapide des couleurs`.
- Son qui craque : désactiver `Son à faible latence`.
- Menus lents ou erreurs de connexion après avoir activé `Garder la liaison avec le PC ouverte` : la désactiver.
- Le PC n'apparaît pas en ligne alors qu'il est allumé : □ sur le PC, puis « Se connecter » (l'essai
  s'arrête au bout de quelques secondes si le PC ne répond pas).
