# VitaMoon : guide d'utilisation et de test

VitaMoon est un client Moonlight pour PS Vita. Version 1.0.0 bêta 3 « Nouvelle Lune ». Jusqu'à la
bêta 1, le projet s'appelait Lune.

Téléchargement :
- versions publiées : https://github.com/LouisF37/VitaMoon/releases
- dernière compilation de la branche `phase2` :
  https://github.com/LouisF37/VitaMoon/releases/download/build-phase2/vitamoon-phase2.vpk

L'écran d'accueil affiche la version exacte en bas à gauche, par exemple `v1.0.0-beta.3  phase2-xxxxxxx`.

**Installation à côté de Moonlight.** VitaMoon a son propre identifiant (`VMOON0001`), sa propre bulle et
son propre dossier (`ux0:data/vitamoon`) : il ne touche pas à Moonlight, qui reste installé. Il faut donc
appairer le PC une première fois avec VitaMoon (code à 4 chiffres dans Sunshine). Pour comparer avec le
Moonlight d'origine, réinstaller le VPK officiel de vita-moonlight 0.13.2 sur la bulle Moonlight : une version
d'essai précédente de ce projet y est peut-être installée.

**Si Lune bêta 1 est installé.** Au premier lancement, VitaMoon déplace `ux0:data/lune` vers
`ux0:data/vitamoon` : les PC appairés, les réglages, les statistiques et les journaux suivent, sans
nouvel appairage. La bulle Lune peut ensuite être supprimée.

**État des vérifications.** Le code compile pour la Vita, les tests sur PC passent (affichage
cadencé simulé, fichier de réglages, réécriture du SPS), tous les écrans ont été rendus hors console
(outil `tools/ui-preview`) en anglais, français et espagnol, et le code a été audité (voir
[le point d'étape](checkpoints/2026-11-15.md)). Rien n'a encore tourné sur une vraie Vita : la
fluidité, la latence et le son restent à mesurer avec le plan de test ci-dessous.

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

**Menu rapide en jeu.** L + R + SELECT pendant un stream ouvre un menu par-dessus le jeu : retour au jeu,
statistiques à l'écran (oui / non, enregistré), clavier, pause (menus de VitaMoon), quitter le jeu (Croix
deux fois, pour éviter un accident ; Rond si X et O sont inversés). Le haut de l'écran montre le Wi-Fi,
la batterie et l'heure. Le jeu continue de tourner sur le PC pendant ce temps, mais ne reçoit aucun
bouton tant que le menu est ouvert, ni avant que tous les boutons soient relâchés à sa fermeture. Rond
(Croix si X et O sont inversés) ou SELECT referme le menu.

**Pendant le stream.** START + L + R met le stream en pause et revient aux menus. La pause relâche sur le PC
tout ce qui était maintenu (boutons, sticks, clic souris, doigts sur l'écran tactile). Si des réglages qui ne
s'appliquent qu'au démarrage d'un stream ont été modifiés pendant la pause (définition, débit, affichage,
options de la rubrique Performance...), la reprise propose de relancer le stream : le jeu continue sur le PC,
seul le flux est recommencé.

**Lune d'attente.** Pendant la connexion à un PC et le démarrage d'un stream, une lune se remplit à chaque
étape (préparation, recherche du PC, son, accord sur le stream, commandes, vidéo), avec l'étape écrite
dessous ; elle est pleine, le logo de VitaMoon, au démarrage de la vidéo. Le journal indique combien d'images
de la lune ont été dessinées et le temps qu'elles ont pris (`waiting moon: N pictures drawn in M ms`).

**Reconnexion automatique.** Si le Wi-Fi coupe ou si la Vita se met en veille pendant un stream, VitaMoon
affiche « Connexion perdue », attend le retour du Wi-Fi puis relance le stream du même jeu, qui a continué
sur le PC. Il essaie toutes les 3 secondes pendant une minute, la lune se remplissant au fil de cette
minute ; Rond (ou Croix si X et O sont inversés)
annule. Si le stream retombe trois fois de suite moins de 20 secondes après être reparti, VitaMoon arrête et
le dit. Option : Réglages → Général → Reconnexion automatique (activée par défaut). Une coupure pendant la
pause (dans les menus) ne déclenche pas de reconnexion : reconnectez-vous et choisissez Reprendre.

**Appairage.** La Vita s'appaire sous son pseudo (le nom d'utilisateur de la console) au lieu de « roth » : c'est le nom
affiché dans la liste des appareils de Sunshine. Les appairages existants gardent l'ancien nom.

**Réglages.** Six rubriques à gauche, les options à droite, une explication de l'option sélectionnée en bas.
- Haut / bas : option. Gauche / droite : changer la valeur. ✕ : basculer, valeur suivante ou ouvrir.
- L / R : rubrique précédente / suivante. ○ : retour (les réglages sont enregistrés).
- Langue : English, Français, Español (rubrique Général).
- Thème : Nuit, Aube, Aurore, Crépuscule, Éclipse (rubrique Général) ; appliqué tout de suite, avec son
  propre ciel lunaire (étoiles et grand croissant ; Éclipse : disque sombre et sa couronne).

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
- `Cadencé` : affichage au rythme des horodatages du PC (idée de dubleyu/vita-moonlight-relay, délai
  fixe de 12 ms). Ici l'ancrage s'ajuste toutes les 2 secondes sur l'image la plus lente : plus tard
  si des images arrivent en retard, plus tôt si elles attendent inutilement, avec une marge de 4 à
  33 ms (12 ms au départ). Testé sur PC par simulation (gigue, dérive d'horloge, pauses), pas encore
  sur Vita. Un peu plus de latence que `Fluide`, en échange d'une cadence régulière.
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

Rubrique Commandes, `Lire les commandes au rythme de la Vita` (activé) : les boutons sont lus une fois par
relevé de la manette (une fois par rafraîchissement de l'écran) au lieu de toutes les 2 ms, ce qui allège le
cœur de l'affichage (juanjsebgarcia ; effet sur la latence non mesuré).

Rubrique Vidéo : 960 x 544 par défaut (la définition de l'écran de la Vita) et débit `Auto`
(8 Mbps en 960 x 544 à 60 img/s, 10 Mbps en 1280 x 720, réduit à 30 img/s).

Rubrique Avancé, deux autres options de test :
- `Lissage des contours` (activé) : anticrénelage 4x des menus et du stream. Son coût sur le dessin du
  stream n'est pas mesuré ; pour le mesurer, comparer la colonne `render_avg_us` avec et sans
  (redémarrer VitaMoon après le changement).
- `Garder la liaison avec le PC ouverte` (désactivé) : évite de rouvrir une connexion sécurisée à chaque
  requête des menus (liste des jeux, jaquettes). Expérimental.

## Plan de test

Le plan complet, avec les essais de robustesse (pauses répétées, coupure du Wi-Fi, mise en veille),
est dans [le point d'étape](checkpoints/2026-11-15.md). L'essentiel :

Même jeu, même endroit, 3 à 5 minutes par session, statistiques affichées (Général → Statistiques
pendant le stream) :
1. Profil **Fluide**.
2. Profil **Cadencé**.
3. Profil **Latence minimale**.
4. Profil **Original** (référence : le comportement d'avant).
5. Puis, en partant de Fluide, une option changée à la fois : `Attendre les paquets en retard` à Non puis
   30 ms ; `Décodage rapide des couleurs` désactivé ; `Répartir le travail sur les cœurs` désactivé ;
   `Taille des paquets vidéo` à 1392 ; `Lire les commandes au rythme de la Vita` désactivé. Le plus
   simple : START + L + R, changer l'option, reprendre et accepter la relance du stream (chaque stream a
   ses propres fichiers de statistiques).
6. Facultatif : Fluide avec `Lissage des contours` désactivé (après redémarrage de VitaMoon).

Chaque session crée deux fichiers dans `ux0:data/vitamoon/stats/` : un `.csv` (une ligne par seconde)
et un `.txt` (réglages de la session, signal Wi-Fi, légende des colonnes). Ce sont ces fichiers qu'il
faut renvoyer, avec une phrase sur le ressenti (saccades, retard des commandes, son).

Colonnes à regarder en premier : `shown` (images affichées par seconde), `lost`, `r2p_avg_us`
(premier paquet reçu → image visible), `late`, `aud_underrun`, `wifi_pct`. Nouvelles colonnes :
`reorder_held` / `reorder_saved` (paquets attendus / images sauvées par l'attente), `fec_saved` /
`fec_failed` (images réparées / irréparables par la correction d'erreurs), `pkt_ooo` (paquets arrivés
dans le désordre), `pace_delay_us` (marge du mode Cadencé).

À la fin du `.txt`, des centiles sur toute la session (p50, p95, p99) : délai premier paquet → image
visible, intervalle entre deux images affichées, durée du décodage matériel, et la part des intervalles
de plus de 1,5 image (images affichées en retard ou répétées). Ce sont les chiffres à comparer d'un
profil à l'autre : les moyennes cachent les saccades.

## Réglages conseillés

**Vita.** Paramètres → Paramètres d'économie d'énergie : décocher l'utilisation du Wi-Fi en mode économie
d'énergie. Couper aussi le Bluetooth si aucun appareil Bluetooth ne sert : il partage la bande 2,4 GHz
avec le Wi-Fi (juanjsebgarcia : pertes d'images d'environ 50 par minute à 0 avec un signal fort et le
Bluetooth coupé). VitaMoon lit ces deux réglages et affiche un avertissement sur l'accueil s'il les
trouve activés (lecture du registre non encore vérifiée sur console).

**Sunshine** (interface web : `https://localhost:47990`), en résumé ; le pas-à-pas complet est dans le
tutoriel Sunshine fourni avec cette version.
- Installer **ViGEmBus** (1.17 ou plus récent), puis *Input* > *Pilotes de la manette Windows* = ViGEmBus :
  sans pilote, Sunshine 2026.914 ne crée pas de manette virtuelle.
- *Input* > *Mapper Retour/Sélection au clic du pavé tactile* : décoché (sinon SELECT appuie aussi sur
  le pavé de la DualShock 4 virtuelle quand VitaMoon annonce une manette PlayStation).
- *Audio / Vidéo* > options des périphériques d'affichage : *Vérifier que l'écran est activé*, puis une
  entrée de remplacement 960x544 → 1280x720 ; côté VitaMoon, *Laisser le PC adapter son écran* activé.
- *Encodeur Intel QuickSync* > préréglage : la **première** ligne (veryfast ; la traduction donne le
  même libellé à la deuxième). `cavlc` et `fec_percentage` = 30 restent des essais, un par un.
- `lan_encryption_mode` = 0 et `minimum_fps_target` = 0 : les valeurs par défaut.

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

Chaque lancement écrit `ux0:data/vitamoon/vitamoon.log` (les quatre précédents : `vitamoon.1.log` à `vitamoon.4.log`) :
démarrage, PC contactés et version de Sunshine, chaque stream (réglages, étapes chronométrées, fin et
cause), reconnexions, erreurs, et une ligne de santé par minute de jeu. L'heure des lignes est celle de
la Vita, pour la rapprocher du journal de Sunshine (*Dépannage* > *Journaux*). *Réglages* > *Général* >
*Journal détaillé* ajoute tous les détails. Pour un rapport : `vitamoon.log` et les fichiers `stats/session-*`.

- Image saccadée, figée ou absente : profil **Original**. Couleurs fausses : désactiver
  `Décodage rapide des couleurs`.
- Son qui craque : désactiver `Son à faible latence`.
- Tactile qui semble moins réactif : désactiver `Lire les commandes au rythme de la Vita`.
- Menus lents ou erreurs de connexion après avoir activé `Garder la liaison avec le PC ouverte` : la désactiver.
- Le PC n'apparaît pas en ligne alors qu'il est allumé : □ sur le PC, puis « Se connecter » (l'essai
  s'arrête au bout de quelques secondes si le PC ne répond pas).
