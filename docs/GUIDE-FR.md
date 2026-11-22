# VitaMoon : guide d'utilisation et de test

VitaMoon est un client Moonlight pour PS Vita. Version 1.5.0 « Premier Croissant » (22 novembre 2026) :
nouvel accueil, page Aide, jaquettes plus grandes aux coins arrondis, après les quatre bêtas de la 1.1.0
(détail dans [CHANGELOG.md](../CHANGELOG.md)). La 1.0.0 « Nouvelle Lune » avait été préparée par onze
versions d'essai (bêta 1 à 11, les 15 et 16 novembre 2026 ; la bêta 1 s'appelait Lune), détaillées dans
[history/betas.md](history/betas.md).

Téléchargement :
- versions publiées : https://github.com/LouisF37/VitaMoon/releases (seulement les versions finales ;
  les bêtas de chaque version, avec leurs VPK et leurs notes, sont dans le fichier
  `vitamoon-v<version>-betas.zip` joint à cette version)
- dernière compilation de la branche `main` (et VPK des bêtas en cours) :
  https://github.com/LouisF37/VitaMoon/releases/download/build-main/vitamoon-main.vpk

L'écran d'accueil affiche la version exacte en bas à gauche, par exemple `v1.5.0  main-xxxxxxx`.

**Installation à côté de Moonlight.** VitaMoon a son propre identifiant (`VMOON0001`), sa propre bulle et
son propre dossier (`ux0:data/vitamoon`) : il ne touche pas à Moonlight, qui reste installé. Il faut donc
appairer le PC une première fois avec VitaMoon (code à 4 chiffres dans Sunshine). Pour comparer avec le
Moonlight d'origine, réinstaller le VPK officiel de vita-moonlight 0.13.2 sur la bulle Moonlight : une version
d'essai précédente de ce projet y est peut-être installée.

**Si Lune bêta 1 est installé.** Au premier lancement, VitaMoon déplace `ux0:data/lune` vers
`ux0:data/vitamoon` : les PC appairés, les réglages, les statistiques et les journaux suivent, sans
nouvel appairage. La bulle Lune peut ensuite être supprimée.

**État des vérifications.** Mesuré sur une Vita le 16 novembre (8 sessions, Sunshine, 1280x720 à
60 images/s, 10 Mb/s, affichage *Fluide*, la plus longue de 34 minutes) : 96 % des images envoyées
par le PC sont affichées, 97 ms médian entre l'arrivée d'une image et l'écran (environ 170 ms quand le
PC n'envoie qu'environ 20 images/s, sur un écran fixe), aucun plantage. Reste à comprendre : de
courtes coupures du son, environ 6 par minute. Sur PC : 349 vérifications de la logique, tous les
écrans rendus hors console (outil `tools/ui-preview`) en anglais, français et espagnol, et 29 parcours
complets (ouvrir le PC, jouer, pause, reprise, perte du Wi-Fi, changement de jeu, réglages,
appairage) avec le vrai code de connexion face à une imitation de Sunshine. Détail : [le point
d'étape de la 1.0.0](checkpoints/2026-11-17.md).

**Bibliothèque réseau.** moonlight-common-c est intégré au dépôt (`third_party/moonlight-common-c`, version
amont e2722dc) avec trois correctifs listés dans `third_party/patches/moonlight-common-c/` : attente des
paquets en retard (juanjsebgarcia), crochets Vita (priorité et cœur des fils, compteurs).

## Prise en main

**Accueil.** Un menu à gauche (Accueil, Bibliothèque, Ordinateurs, Réglages, Aide, Quitter) et, à
droite, l'aperçu de l'entrée sous le curseur.
- Un seul élément est sélectionné à la fois : il est légèrement agrandi, éclairé, avec une bordure épaisse
  et un halo de la couleur du thème. Le bouton d'une carte ne prend la couleur du thème que lorsque la
  carte est sélectionnée. Une jaquette sélectionnée (ici comme dans la Bibliothèque) a un cadre blanc,
  séparé d'elle par un trait sombre : il se voit sur une jaquette claire comme sur une sombre.
- Les jaquettes ont les coins arrondis, comme les tuiles de couleur affichées tant qu'elles ne sont pas
  téléchargées.
- Partout pareil : la flèche droite entre dans l'aperçu (Accueil, Bibliothèque, Ordinateurs, Réglages,
  Aide), les flèches s'y déplacent, ✕ agit sur l'élément sélectionné, la flèche gauche depuis la première
  colonne ou ○ revient au menu. ✕ sur une entrée du menu ouvre toujours sa page en plein écran ; dans le
  menu, ○ remet le curseur sur Accueil.
- Accueil : une grande carte qui dit ce que fait ✕ (Continuer le dernier jeu joué sur le dernier PC
  utilisé, sans passer par la bibliothèque ; Reprendre un stream en pause ; Réveiller le PC quand la
  recherche le voit éteint et que son adresse réseau est connue ; Options du PC s'il ne répond pas ou
  répond d'une nouvelle adresse ; Ouvrir la bibliothèque tant qu'elle n'a pas été ouverte une fois),
  puis une rangée de quatre jaquettes : jusqu'à deux autres jeux joués récemment, les favoris, les autres
  jeux récents et la suite de la bibliothèque, et « Tout voir ». ✕ sur une jaquette lance le jeu.
- Bibliothèque : les dix premiers jeux (deux rangées de cinq) ; ✕ sur une jaquette lance le jeu.
- Ordinateurs : les PC appairés (un seul en détail, plusieurs en liste), « Rechercher sur le réseau » et
  « Saisir une adresse » ; ✕ connecte un PC en ligne ou ouvre le menu d'un PC qui ne répond pas.
- Réglages : une carte par rubrique avec ses valeurs principales ; ✕ ouvre les réglages directement sur
  cette rubrique, sur sa première option. Avancé indique « Par défaut » tant qu'aucune de ses options
  modifiables ne s'écarte des valeurs de départ de VitaMoon.
- Aide : raccourcis pendant le stream, tactile, ajout des jeux dans Sunshine, Wi-Fi (voir plus bas).
- □ : menu du PC sélectionné dans Ordinateurs, sinon du PC affiché (se connecter, réveiller, modifier
  l'adresse, renommer, oublier). △ : relance la recherche des PC. START : réglages.
- L'accueil connaît les jeux d'un PC grâce à un petit fichier que la bibliothèque laisse quand on la
  quitte (`ux0:data/vitamoon/<nom du PC>/home.txt`) : tant que la bibliothèque d'un PC n'a pas été
  ouverte une fois avec cette version, l'accueil propose de l'ouvrir.

**Ajouter ses jeux.** VitaMoon affiche les applications que Sunshine publie ; au départ, Sunshine n'en a
que deux, Desktop et Steam Big Picture (il n'importe pas la bibliothèque Steam). Pour voir un jeu (FF9,
FF8…) et le lancer directement depuis la Vita, l'ajouter une fois sur le PC : page web de Sunshine
(https://localhost:47990) → Applications → Ajouter une application ; Nom de l'application : le jeu ;
Commandes détachées : `steam://rungameid/` suivi du numéro du jeu (le nombre dans l'adresse de sa page
sur le Steam Store) ; Trouver une jaquette, puis Sauvegarder. Le jeu apparaît ensuite dans la
bibliothèque avec sa jaquette. Si un jeu ouvre d'abord un lanceur, il se passe au tactile. La même
marche à suivre est dans Aide → Ajouter des jeux.

**Aide.** Quatre sujets (haut / bas ou L / R pour changer, ○ pour revenir) : pendant le stream
(L + R + SELECT menu rapide, START + L + R pause, START + gauche clavier, touche PS envoyée au PC, deux
appuis rapprochés pour l'accueil de la Vita) ; tactile (mode de l'écran avant, pavé arrière L2 / R2 en
haut et L3 / R3 en bas, raccourcis des coins) ; ajouter des jeux ; Wi-Fi (économie d'énergie et
Bluetooth tels que la Vita les indique, bande 2,4 GHz seulement, câble pour le PC).

**Ordinateurs.** La liste des PC appairés avec leur état (en ligne, hors ligne, adresse modifiée ;
« Vérification… » pendant les quelques secondes de la première recherche), et à droite le détail du PC
sélectionné : adresse, appairage, réveil à distance, flux demandé, et un conseil.
- ✕ sur un PC en ligne : connexion directe. ✕ sur un PC hors ligne : ouvre son menu.
- □ : menu du PC (se connecter, réveiller, modifier l'adresse, renommer, oublier).
- △ : relance la recherche des PC. ○ : retour à l'accueil.

**Ajouter un PC.** Dans Ordinateurs, « Rechercher sur le réseau » ou « Saisir une adresse ». La Vita affiche un code à
4 chiffres et l'adresse de la page web de Sunshine (`https://adresse-du-PC:47990`, rubrique PIN) où le saisir.
Rond annule l'attente. Sunshine garde une demande 5 minutes : après une annulation, supprimez-la sur sa
page PIN avant de réessayer (sinon il refuse une nouvelle demande de la même Vita jusque-là).

**Bibliothèque.** Après la connexion, les jeux s'affichent en rangée de jaquettes. Sous le titre du jeu
sélectionné : « Dernière partie : aujourd'hui à 19:46 · 7 min » (hier, il y a 2 à 6 jours, puis la date),
ou « Jamais lancé depuis cette Vita ».
- Ordre : les favoris d'abord, puis les jeux joués du plus récent au plus ancien, puis ceux jamais lancés
  depuis cette Vita, dans l'ordre de Sunshine.
- SELECT : ajoute le jeu aux favoris (étoile sur la jaquette) ou l'en retire. Les favoris sont gardés par
  jeu (`ux0:data/vitamoon/favorites.txt`).
- Les chiffres de la dernière session (images affichées, délai, images perdues) sont une option :
  Réglages → Général → Statistiques dans la bibliothèque (désactivée par défaut).
- ✕ jouer ou reprendre, □ quitter le jeu en cours sur le PC, △ ancien menu, START réglages, ○ retour.
- Croix directionnelle ou écran tactile pour choisir ; L et R avancent de 5 jeux.

**Menu rapide en jeu.** L + R + SELECT pendant un stream ouvre un menu par-dessus le jeu : retour au jeu,
statistiques à l'écran (oui / non, enregistré), clavier, mesure du délai (page de test, voir les essais
de la 1.1), pause (menus de VitaMoon), quitter le jeu (Croix deux fois, pour éviter un accident ; Rond si
X et O sont inversés). Le haut de l'écran montre le Wi-Fi, la batterie et l'heure ; sous le nom du PC,
la durée du stream et l'autonomie restante estimée par la Vita. Le jeu continue de tourner sur le PC pendant ce temps, mais ne reçoit aucun
bouton tant que le menu est ouvert, ni avant que tous les boutons soient relâchés à sa fermeture. Rond
(Croix si X et O sont inversés) ou SELECT referme le menu.

**Pendant le stream.** Les statistiques à l'écran (menu rapide ou Réglages → Général) tiennent sur une
ligne : images affichées par seconde, délai réception → écran, débit. Tout le reste est dans les fichiers
de statistiques. START + L + R met le stream en pause et revient aux menus. La pause relâche sur le PC
tout ce qui était maintenu (boutons, sticks, clic souris, doigts sur l'écran tactile). Si des réglages qui ne
s'appliquent qu'au démarrage d'un stream ont été modifiés pendant la pause (définition, débit, affichage,
options de la rubrique Performance...), la reprise propose de relancer le stream : le jeu continue sur le PC,
seul le flux est recommencé.

**Démarrage.** Dès la première image, VitaMoon affiche le ciel du thème choisi et une lune qui croît pendant
le chargement ; une fois tout chargé, elle devient le logo, qui rejoint avec le nom la barre du haut de
l'écran d'accueil, puis les menus apparaissent. Le réseau, les commandes et la liste des PC se préparent en
parallèle des polices. Le journal indique la durée de chaque étape et le moment où l'accueil est apparu
(`start-up: first frame ... ms after launch; ... Home at ... ms after launch`). Les définitions acceptées
par le décodeur de la Vita sont recherchées en arrière-plan dès le démarrage : les Réglages s'ouvrent sans
attendre (`decoder sizes probed in ... ms` dans le journal).

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

**Réglages.** Sept rubriques à gauche, les options à droite, une explication de l'option sélectionnée en bas.
- Haut / bas : option. Gauche / droite : changer la valeur. ✕ : basculer, valeur suivante ou ouvrir.
- L / R : rubrique précédente / suivante. ○ : retour (les réglages sont enregistrés).
- Langue : English, Français, Español (rubrique Général).
- Thème : Nuit, Aube, Aurore, Crépuscule, Éclipse (rubrique Général ; Éclipse par défaut depuis la 1.1.0-bêta.3) ; appliqué tout de suite, avec son
  propre ciel lunaire (étoiles et grand croissant ; Éclipse : disque sombre et sa couronne).
- Étoiles scintillantes (Général, activé par défaut) : chaque petite étoile du fond s'éclaire doucement
  une fois toutes les 2,5 à 6 secondes, chacune à son moment.
- Ciel en mouvement (Général, Moyen par défaut depuis la 1.1.0-bêta.3) : les étoiles tournent autour de la lune, un tour en
  32 min (Très lent), 16 min (Lent), 8 min (Moyen) ou 4 min (Rapide). Sens de rotation : Horaire (elles
  montent en bas à gauche et passent en haut vers la droite) ou Antihoraire. Changer la vitesse ou le sens
  ne fait pas sauter les étoiles ; le ciel ne tourne que dans les menus, rien n'est dessiné pendant un
  stream. Le scintillement s'y ajoute s'il est activé.
- Appui pour cliquer (Tactile, en mode Pavé tactile) : un toucher relâché avant 100, 150, 200 ou 250 ms
  (250 par défaut) sans bouger de plus de 20 pixels est un clic ; au-delà, ou dès que le doigt bouge
  davantage, le pointeur suit le doigt. Plus long : on clique plus facilement ; plus court : moins de
  clics involontaires.
- Touche PS envoyée au PC (Commandes) : un appui va au PC (le menu de Steam, par exemple), deux
  appuis rapprochés ramènent à l'écran d'accueil de la Vita.

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
est dans [le point d'étape du 16 novembre](checkpoints/2026-11-16.md) ; les premières mesures sur la
Vita sont dans [celui de la 1.0.0](checkpoints/2026-11-17.md). L'essentiel :

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

## Essais de la 1.1 (bêta)

La 1.1.0-beta.1 ajoute des options à mesurer, toutes dans **Réglages → Essais (bêta)** et réglées
par défaut comme en 1.0 : rien ne change tant qu'on n'y touche pas. Le détail technique est dans
[design/tests-1.1.md](design/tests-1.1.md) (en anglais).

| Option | Ce qu'elle change | Où regarder |
|---|---|---|
| Mode de délai du décodeur | Non, Faible, Mode 1, Mode 2, Élevé : demande au décodeur de la Vita de rendre les images plus tôt (le Moonlight Vita 1.0 d'origine utilise Faible). Une fois utilisé, Non ne revient qu'après un redémarrage de VitaMoon | journal (`decoder delay mode`), colonnes `hold_avg_x100` et `r2p_avg_us` |
| Images de référence du décodeur | 4 (comme en 1.0), 2 ou 1. Si le décodeur refuse, VitaMoon repasse à 4 tout seul | journal (`reference frames`), image cassée ou non |
| Sauter les images en retard | après un gel du Wi-Fi, les images arrivées avec plus de 100 ms de retard sont décodées mais pas affichées : l'image saute au présent au lieu de rejouer le passé en accéléré | colonne `late_hidden`, journal (`late frames`) |
| Afficher les images tout de suite | affichage **Faible latence** seulement : l'image part à l'écran sans attendre le rafraîchissement (jusqu'à 16 ms, 8 en moyenne), au risque d'une image coupée en deux | `r2p_avg_us`, déchirures à l'écran |
| Paquets de son de 5 ms | le PC envoie le son toutes les 5 ms au lieu de 10 | `aud_underrun`, `aud_wait_max_us`, journal (`sound gap`) |

Une option changée pendant une pause (START + L + R) s'applique en relançant le stream : VitaMoon le
propose en reprenant.

Nouveau aussi, sans option : chaque coupure du son écrit une ligne `sound gap` dans le journal (depuis
combien de temps le dernier paquet de son était arrivé, combien attendaient, combien étaient perdus).
Une coupure longtemps après le dernier paquet désigne le Wi-Fi ou le PC ; une coupure avec un paquet
récent désigne la Vita. C'est à lire avant d'essayer les paquets de 5 ms.

Ailleurs : **Réglages → Tactile → Pavé tactile arrière** coupe le pavé arrière (L2/R2 en haut, L3/R3
en bas). Le Moonlight Vita d'origine avait mis une zone morte de 80 points par défaut, puis l'a
retirée : la zone morte de VitaMoon reste à 0, réglable sur sa page. Dans le menu rapide, sous le nom
du PC : depuis combien de temps dure le stream et l'autonomie restante estimée par la Vita (le
pourcentage et l'heure sont en haut à droite).

### Mesurer le délai ressenti (appui → image)

1. Copier [tools/latency/latency.html](../tools/latency/latency.html) sur le mini-PC, par exemple dans
   `C:\VitaMoon\latency.html`.
2. Dans Sunshine, *Applications* → *Ajouter* : nom `VitaMoon délai`, commande :
   `"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe" --kiosk "file:///C:/VitaMoon/latency.html" --edge-kiosk-type=fullscreen --no-first-run --user-data-dir=C:\VitaMoon\edge-profile`
   (Edge en plein écran ; le profil à part lui fait ouvrir sa propre fenêtre, que Sunshine ferme quand
   on quitte le « jeu » depuis la Vita).
3. Lancer `VitaMoon délai` depuis la Vita : écran noir avec une petite barre qui bouge en bas (elle fait
   envoyer 60 images par seconde au PC, comme un jeu).
4. Menu rapide (L + R + SELECT) → **Mesurer le délai (page de test)**, puis ne plus toucher aux
   commandes pendant 25 secondes environ. Seulement sur la page de test : lancé par erreur sur un jeu,
   VitaMoon s'arrête après ses deux premiers appuis sans réponse, mais le jeu les reçoit.

VitaMoon appuie tout seul sur A (2 appuis d'échauffement, puis 20 mesurés) et regarde le centre de
l'image : la page devient blanche tant que A est enfoncé. Le résultat s'affiche en bas de l'écran
(médiane, minimum, maximum) et part dans `ux0:data/vitamoon/stats/latency-<date>.txt`, avec chaque appui
découpé en trois : appui envoyé → premier paquet de l'image blanche (trajet vers le PC, réaction de la
page, capture, encodage, trajet retour), premier paquet → sortie du décodeur, décodeur → écran.

Non compté : la Vita lit ses boutons environ 60 fois par seconde, donc un vrai appui attend en plus 0 à
17 ms (8 en moyenne) ; et le centre de l'écran s'affiche environ 8 ms après le haut. Le chiffre sert
surtout à **comparer** : même page, mêmes réglages Sunshine, une option changée à la fois.

### Protocole

1. Session de référence : 1.1.0-beta.1 sans rien changer (5 minutes de FF9 au même endroit), puis une
   mesure du délai.
2. Mode de délai du décodeur à **Faible**, redémarrer VitaMoon entre deux modes, même session et même
   mesure ; puis Mode 1, Mode 2, Élevé si Faible change quelque chose.
3. Images de référence à 2, puis 1 (avec le meilleur mode de délai si l'image reste propre).
4. Sauter les images en retard : utile seulement s'il y a des gels du Wi-Fi pendant la session.
5. Profil Latence minimale, avec et sans Afficher les images tout de suite.
6. Paquets de son de 5 ms, après avoir lu les lignes `sound gap` des sessions précédentes.

À renvoyer pour chaque session : les fichiers `stats/session-*` et `stats/latency-*`, et le journal
du lancement (`logs/`).

## Réglages conseillés

**Vita.** Paramètres → Paramètres d'économie d'énergie : décocher l'utilisation du Wi-Fi en mode économie
d'énergie. Couper aussi le Bluetooth si aucun appareil Bluetooth ne sert : il partage la bande 2,4 GHz
avec le Wi-Fi (juanjsebgarcia : pertes d'images d'environ 50 par minute à 0 avec un signal fort et le
Bluetooth coupé). VitaMoon lit ces deux réglages et affiche un avertissement sur la page Ordinateurs s'il les
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
- `lan_encryption_mode` = 0 : la valeur par défaut.
- `minimum_fps_target` (0 par défaut : la moitié des images par seconde du stream au minimum, d'après
  la documentation de Sunshine). **Essai** : 60. Sur un écran fixe, le PC envoyait environ 20 images
  par seconde dans les sessions de la 1.0.0, et le décodeur de la Vita garde 3 à 4 images : environ
  170 ms au lieu de 97. Sunshine précise que le minimum réel peut différer de la valeur choisie : son
  journal l'indique (`Minimum FPS target set to ...`), et la colonne `recv` des statistiques le montre
  sur un écran fixe.

**Essais côté PC, un à la fois** (aucun code) : le mini-PC en Ethernet plutôt qu'en Wi-Fi si c'est
possible ; le canal Wi-Fi 2,4 GHz de la box (1, 6 puis 11, la Vita n'utilise que le 2,4 GHz) ;
960 x 544 contre 1280 x 720 (Réglages → Vidéo).

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

Chaque lancement écrit son journal, `ux0:data/vitamoon/logs/vitamoon-<date>.log` (daté de son
démarrage ; les 30 derniers sont gardés) : démarrage, PC contactés et version de Sunshine, chaque stream (réglages, étapes chronométrées, fin et
cause), reconnexions, erreurs, et une ligne de santé par minute de jeu. L'heure des lignes est celle de
la Vita, pour la rapprocher du journal de Sunshine (*Dépannage* > *Journaux*). *Réglages* > *Général* >
*Journal détaillé* ajoute tous les détails. Pour un rapport : le journal du lancement concerné et les
fichiers `stats/session-*` de la session. Les journaux des bêtas (`vitamoon.log`, `vitamoon.1.log` à
`.4.log`) sont rangés dans `logs/` au premier lancement de la 1.0.0.

- Image saccadée, figée ou absente : profil **Original**. Couleurs fausses : désactiver
  `Décodage rapide des couleurs`.
- Son qui craque : désactiver `Son à faible latence`.
- Tactile qui semble moins réactif : désactiver `Lire les commandes au rythme de la Vita`.
- Menus lents ou erreurs de connexion après avoir activé `Garder la liaison avec le PC ouverte` : la désactiver.
- Le PC n'apparaît pas en ligne alors qu'il est allumé : □ sur le PC, puis « Se connecter » (l'essai
  s'arrête au bout de quelques secondes si le PC ne répond pas).
