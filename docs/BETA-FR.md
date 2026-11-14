# Moonlight Vita : bêta « phase 2 »

Version de test de ce fork de vita-moonlight, branche `phase2`.
Téléchargement (toujours la dernière compilation) :
https://github.com/LouisF37/vita-moonlight/releases/download/build-phase2/moonlight-phase2.vpk

Le menu principal affiche la version exacte, par exemple `Moonlight v0.13.2 (phase2-xxxxxxx)`.

## Ce qui change

**Interface**
- Nouveau thème : fond à vagues, panneaux translucides, police Roboto, symboles des touches PlayStation en couleur (palette « Ocean » de VitaRPS5).
- Bibliothèque : après la connexion au PC, les jeux s'affichent en rangée de jaquettes. Le jeu sélectionné apparaît en grand avec les chiffres de sa dernière session.
  - ✕ jouer ou reprendre, □ quitter le jeu en cours sur le PC, △ ancien menu (appairage, déconnexion), START réglages, ○ retour.
  - Croix directionnelle ou écran tactile pour choisir ; L et R pour avancer de 5 jeux.
  - Les jaquettes viennent de Sunshine. Elles sont téléchargées en arrière-plan et gardées sur la carte mémoire (`ux0:data/moonlight/boxart/`).
- Réglages : nouvelle rubrique « Performance », avec une explication de l'option sélectionnée sous le panneau.

**Performance** (Réglages → Performance)
- `Display mode` : *smooth* par défaut (une image par rafraîchissement, plus régulier), *low latency* (dernière image tout de suite), *direct* (fonctionnement d'origine).
- `Low-latency audio` : son par tranches de 5 ms au lieu de 20 ms.
- `Max CPU/GPU clocks while streaming` : 444 / 222 MHz pendant le stream.
- `Send Vita refresh rate to host` : le PC encode à la fréquence exacte de l'écran de la Vita.
- `Video packet size` : 1392 par défaut (1024 à l'origine).
- Toujours actifs : mémoire réseau 8 Mo (1 Mo à l'origine), réception vidéo et audio en priorité haute.

**Mesures**
- Chaque session crée un fichier `.csv` (une ligne par seconde) et un fichier `.txt` (réglages et légende des colonnes) dans `ux0:data/moonlight/stats/`.
- « Display streaming FPS » affiche ces chiffres pendant le stream.

## Réglages Sunshine conseillés (mini-PC Intel)

Interface web de Sunshine : `https://localhost:47990`.
- `minimum_fps_target` = 60 : cadence régulière, même quand l'image bouge peu.
- `lan_encryption_mode` = 0 (valeur par défaut) : pas de chiffrement en réseau local.
- À tester ensuite, un par un : `qsv_preset` = veryfast, `qsv_coder` = cavlc, `fec_percentage` = 30 si des images sont perdues.

## Ajouter ses jeux Steam (et PS1) à Sunshine

Le script `tools/sunshine-import.ps1` ajoute les jeux Steam installés, avec leur jaquette, à la liste de Sunshine. Il ne touche pas aux jeux déjà présents et sauvegarde `apps.json` avant d'écrire.

Dans un PowerShell lancé en administrateur, depuis le dossier du script :
```
powershell -ExecutionPolicy Bypass -File .\sunshine-import.ps1 -DryRun
powershell -ExecutionPolicy Bypass -File .\sunshine-import.ps1
```
`-DryRun` affiche ce qui serait ajouté sans rien écrire.

Pour des jeux PS1 avec DuckStation, ajoute :
```
-DuckStationExe "C:\chemin\duckstation-qt-x64-ReleaseLTCG.exe" -Ps1Folder "D:\PS1"
```
Fichiers reconnus : `.cue`, `.chd`, `.m3u`, `.pbp`. Une image `.png` ou `.jpg` du même nom à côté du jeu sert de jaquette.

## Limites connues

- Rien n'a encore été testé sur une vraie Vita : en cas de problème d'image, repasse `Display mode` sur *direct* ; en cas de son qui craque, désactive `Low-latency audio`.
- Les réglages restent en anglais ; seuls les nouveaux écrans sont en français.
