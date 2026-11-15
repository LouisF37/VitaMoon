# -*- coding: utf-8 -*-
# Interface strings added with the redesigned menus: English -> (French, Spanish).
# Keys are written as in the C source (\\n stands for the two characters \n).
NB = " "  # non-breaking space before French double punctuation
T2 = {
# --- home screen and hosts ---
"Online": ("En ligne", "En línea"),
"Offline": ("Hors ligne", "Sin conexión"),
"Address changed": ("Adresse modifiée", "Dirección cambiada"),
"Computers": ("Ordinateurs", "Ordenadores"),
"Add a computer": ("Ajouter un ordinateur", "Añadir un ordenador"),
"Search the network": ("Rechercher sur le réseau", "Buscar en la red"),
"Enter an address": ("Saisir une adresse", "Introducir una dirección"),
"PC options": ("Options du PC", "Opciones del PC"),
"Refresh": ("Actualiser", "Actualizar"),
"Address": ("Adresse", "Dirección"),
"Pairing": ("Appairage", "Emparejamiento"),
"Wake-on-LAN": ("Réveil à distance", "Encendido remoto"),
"Ready": ("Prêt", "Listo"),
"Not ready yet": ("Pas encore prêt", "Aún no disponible"),
"Stream": ("Flux", "Transmisión"),
"Ready to play.": ("Prêt à jouer.", "Listo para jugar."),
"This PC answers from a new address: open it to update.":
    ("Ce PC répond depuis une nouvelle adresse" + NB + ": ouvrez-le pour la mettre à jour.",
     "Este PC responde desde otra dirección: ábrelo para actualizarla."),
"No answer. Is the PC on, with Sunshine running?":
    ("Pas de réponse. Le PC est-il allumé, avec Sunshine lancé" + NB + "?",
     "Sin respuesta. ¿Está el PC encendido y con Sunshine en marcha?"),
"Wi-Fi power save is on and adds lag: turn it off in the Vita's Settings, Power Save Settings.":
    ("L'économie d'énergie Wi-Fi est active et ajoute du retard" + NB + ": désactivez-la dans Paramètres, Paramètres d'économie d'énergie de la Vita.",
     "El ahorro de energía del Wi-Fi está activo y añade retardo: desactívalo en Ajustes, Ajustes de ahorro de energía de la Vita."),
"Stream paused": ("Stream en pause", "Stream en pausa"),
"The game is still running on the PC. Resume to go back to it.":
    ("Le jeu tourne toujours sur le PC. Reprenez pour y revenir.",
     "El juego sigue en marcha en el PC. Reanuda para volver a él."),
"Lists the PCs running Sunshine on your Wi-Fi network. Pick one, then type the PIN shown on the Vita into Sunshine's web page.":
    ("Liste les PC où Sunshine tourne sur votre réseau Wi-Fi. Choisissez-en un, puis saisissez le code affiché par la Vita dans la page web de Sunshine.",
     "Muestra los PC con Sunshine en tu red Wi-Fi. Elige uno y escribe el PIN que muestra la Vita en la página web de Sunshine."),
"Type the IP address of the PC, as shown in Sunshine or in the network settings of Windows.":
    ("Saisissez l'adresse IP du PC, indiquée par Sunshine ou dans les paramètres réseau de Windows.",
     "Escribe la dirección IP del PC, la que muestra Sunshine o los ajustes de red de Windows."),
"Language, video quality, performance profile, controls and touch screen.":
    ("Langue, qualité vidéo, profil de performance, commandes et écran tactile.",
     "Idioma, calidad de vídeo, perfil de rendimiento, controles y pantalla táctil."),
"Closes Lune and returns to the Vita home screen.":
    ("Ferme Lune et revient à l'écran d'accueil de la Vita.",
     "Cierra Lune y vuelve a la pantalla de inicio de la Vita."),
"paired": ("appairé", "emparejado"),
"not paired": ("non appairé", "no emparejado"),
"Wake the PC (Wake-on-LAN)": ("Réveiller le PC (Wake-on-LAN)", "Encender el PC (Wake-on-LAN)"),
"Connect even if it looks offline": ("Se connecter même s'il semble hors ligne", "Conectar aunque parezca sin conexión"),
"Change the address": ("Modifier l'adresse", "Cambiar la dirección"),
"Rename": ("Renommer", "Cambiar el nombre"),
"Forget this PC": ("Oublier ce PC", "Olvidar este PC"),
"Forget %s?\\nIts pairing is removed from this Vita.":
    ("Oublier %s" + NB + "?\\nSon appairage est supprimé de cette Vita.",
     "¿Olvidar %s?\\nSu emparejamiento se borra de esta Vita."),
"The network address of this PC is not known yet. Connect to it once while it is on.":
    ("L'adresse réseau de ce PC n'est pas encore connue. Connectez-vous une fois pendant qu'il est allumé.",
     "Aún no se conoce la dirección de red de este PC. Conéctate una vez mientras esté encendido."),
"Wake-up signal sent. The PC needs a moment to start.":
    ("Signal de réveil envoyé. Le PC met un moment à démarrer.",
     "Señal de encendido enviada. El PC tarda un momento en arrancar."),
"This name cannot be used (already taken, or it contains / : * ? < > |).":
    ("Ce nom est inutilisable (déjà pris, ou il contient / : * ? < > |).",
     "Este nombre no se puede usar (ya existe o contiene / : * ? < > |)."),
"Computer": ("Ordinateur", "Ordenador"),
"PCs found on the network": ("PC trouvés sur le réseau", "PC encontrados en la red"),
"Searching... Sunshine must be running on the PC, on the same network.":
    ("Recherche... Sunshine doit tourner sur le PC, sur le même réseau.",
     "Buscando... Sunshine debe estar en marcha en el PC, en la misma red."),
"Search again": ("Relancer la recherche", "Buscar de nuevo"),
"%d.%d": ("%d,%d", "%d,%d"),

# --- touch sub-menus ---
"Top": ("Haut", "Arriba"),
"Bottom": ("Bas", "Abajo"),
"Choose a key or an action": ("Choisir une touche ou une action", "Elegir una tecla o una acción"),
"Distance from the corner": ("Distance du coin", "Distancia a la esquina"),
"Corner shortcuts": ("Raccourcis dans les coins", "Atajos en las esquinas"),

# --- settings: categories and hints ---
"Video": ("Vidéo", "Vídeo"),
"Controls": ("Commandes", "Controles"),
"Touch": ("Tactile", "Táctil"),
"Advanced": ("Avancé", "Avanzado"),
"On": ("Oui", "Sí"),
"Off": ("Non", "No"),
"Open": ("Ouvrir", "Abrir"),
"Category": ("Catégorie", "Categoría"),
"Change": ("Modifier", "Cambiar"),
"Switch": ("Basculer", "Alternar"),
"Next": ("Suivant", "Siguiente"),

# --- settings: General ---
"Statistics while streaming": ("Statistiques pendant le stream", "Estadísticas durante el stream"),
"Frames, delay, network and sound at the top of the screen, updated every second.":
    ("Images, retard, réseau et son en haut de l'écran, mis à jour chaque seconde.",
     "Imágenes, retardo, red y sonido en la parte superior de la pantalla, cada segundo."),
"Swap X and O in the menus": ("Inverser X et O dans les menus", "Intercambiar X y O en los menús"),
"O confirms and X goes back, as on Japanese consoles.":
    ("O valide et X revient en arrière, comme sur les consoles japonaises.",
     "O confirma y X vuelve atrás, como en las consolas japonesas."),
"Keep the Vita awake": ("Garder la Vita éveillée", "Mantener la Vita activa"),
"The screen does not dim and the Vita does not go to sleep during a stream.":
    ("L'écran ne s'assombrit pas et la Vita ne se met pas en veille pendant un stream.",
     "La pantalla no se atenúa y la Vita no entra en reposo durante un stream."),
"Save a debug log": ("Enregistrer un journal de débogage", "Guardar un registro de depuración"),
"Detailed log in Lune's folder. It slows the stream down: for troubleshooting only.":
    ("Journal détaillé dans le dossier de Lune. Il ralentit le stream" + NB + ": à réserver au dépannage.",
     "Registro detallado en la carpeta de Lune. Ralentiza el stream: solo para diagnosticar."),

# --- settings: Video ---
"960 x 544 is the Vita screen itself: the sharpest picture and the lightest stream.":
    ("960 x 544 est la définition de l'écran de la Vita" + NB + ": l'image la plus nette et le flux le plus léger.",
     "960 x 544 es la pantalla de la Vita: la imagen más nítida y el stream más ligero."),
"Frame rate": ("Images par seconde", "Imágenes por segundo"),
"60 matches the screen. 30 halves the data on a weak Wi-Fi link.":
    ("60 correspond à l'écran. 30 divise les données par deux si le Wi-Fi est faible.",
     "60 coincide con la pantalla. 30 reduce los datos a la mitad con un Wi-Fi débil."),
"Auto suits the Vita's Wi-Fi. Lower it if frames get lost. X: type a value.":
    ("Auto convient au Wi-Fi de la Vita. Baissez-le si des images se perdent. X" + NB + ": saisir une valeur.",
     "Auto se ajusta al Wi-Fi de la Vita. Bájalo si se pierden imágenes. X: escribir un valor."),
"Crop to fill the screen": ("Rogner pour remplir l'écran", "Recortar para llenar la pantalla"),
"When the PC picture is not 16:9, shows its centre instead of black bars.":
    ("Si l'image du PC n'est pas en 16:9, affiche son centre au lieu de bandes noires.",
     "Si la imagen del PC no es 16:9, muestra su centro en lugar de bandas negras."),
"Sound on the PC too": ("Son aussi sur le PC", "Sonido también en el PC"),
"The PC keeps playing the sound that is sent to the Vita.":
    ("Le PC continue de jouer le son envoyé à la Vita.",
     "El PC sigue reproduciendo el sonido que se envía a la Vita."),
"%s Mbps": ("%s Mb/s", "%s Mbps"),
"%d fps": ("%d img/s", "%d fps"),
"Auto (%s)": ("Auto (%s)", "Auto (%s)"),
"Bitrate in kbps (for example 8000)": ("Débit en kb/s (par exemple 8000)", "Bitrate en kbps (por ejemplo 8000)"),
"Cannot be changed while a stream is paused.":
    ("Non modifiable tant qu'un stream est en pause.", "No se puede cambiar con un stream en pausa."),

# --- settings: Performance ---
"Profile": ("Profil", "Perfil"),
"Sets the options below at once. Smooth is the default; Original is the app as it was before.":
    ("Règle d'un coup les options ci-dessous. Fluide est le réglage par défaut" + NB + "; Original correspond à l'appli d'avant.",
     "Ajusta de una vez las opciones de abajo. Fluido es el valor por defecto; Original es la app de antes."),
"Display": ("Affichage", "Visualización"),
"Smooth: one frame per screen refresh, drawn just in time. Low latency: newest frame at once. Direct: original code.":
    ("Fluide" + NB + ": une image par rafraîchissement, dessinée juste à temps. Faible latence" + NB + ": la dernière image tout de suite. Direct" + NB + ": code d'origine.",
     "Fluido: una imagen por refresco, dibujada justo a tiempo. Baja latencia: la última imagen al instante. Directo: código original."),
"Low-latency sound": ("Son à faible latence", "Sonido de baja latencia"),
"5 ms sound blocks and a buffer that adapts to the Wi-Fi link. Off: original 20 ms blocks.":
    ("Blocs de son de 5 ms et tampon qui s'adapte au Wi-Fi. Non" + NB + ": blocs de 20 ms d'origine.",
     "Bloques de sonido de 5 ms y un búfer que se adapta al Wi-Fi. No: bloques originales de 20 ms."),
"Decoder catch-up": ("Rattrapage du décodeur", "Recuperación del decodificador"),
"Takes back the frames the video decoder sometimes keeps: each one is 17 ms of delay.":
    ("Récupère les images que le décodeur vidéo garde parfois" + NB + ": chacune représente 17 ms de retard.",
     "Recupera las imágenes que el decodificador a veces retiene: cada una supone 17 ms de retardo."),
"Maximum CPU and GPU speed": ("Vitesse maximale du CPU et du GPU", "Velocidad máxima de CPU y GPU"),
"Match the Vita screen rate": ("Suivre la fréquence de l'écran", "Seguir la frecuencia de la pantalla"),
"The PC encodes at the exact rate of the Vita screen, which avoids regular skipped frames.":
    ("Le PC encode à la fréquence exacte de l'écran de la Vita, ce qui évite des images sautées à intervalles réguliers.",
     "El PC codifica a la frecuencia exacta de la pantalla de la Vita, lo que evita saltos de imagen periódicos."),
"1392: fewer packets per frame, best on a home network. 1024: original value.":
    ("1392" + NB + ": moins de paquets par image, le mieux sur un réseau domestique. 1024" + NB + ": valeur d'origine.",
     "1392: menos paquetes por imagen, lo mejor en una red doméstica. 1024: valor original."),
"Direct (original)": ("Direct (origine)", "Directo (original)"),
"Low latency": ("Faible latence", "Baja latencia"),
"Smooth": ("Fluide", "Fluido"),
"Lowest latency": ("Latence minimale", "Latencia mínima"),
"Original": ("Original", "Original"),
"Custom": ("Personnalisé", "Personalizado"),
"Only used by the Direct display.": ("Utilisé seulement par l'affichage Direct.", "Solo se usa con la visualización Directo."),

# --- settings: Controls ---
"Controller seen by the PC": ("Manette vue par le PC", "Mando que ve el PC"),
"Games show the button symbols of this controller.":
    ("Les jeux affichent les symboles de touches de cette manette.",
     "Los juegos muestran los símbolos de botones de este mando."),
"Swap L1/R1 with L2/R2": ("Échanger L1/R1 et L2/R2", "Intercambiar L1/R1 y L2/R2"),
"L and R send the triggers, the rear touch pad sends L1 and R1. Turns the mapping file off.":
    ("L et R envoient les gâchettes, le pavé tactile arrière envoie L1 et R1. Désactive le fichier de mappage.",
     "L y R envían los gatillos, el panel táctil trasero envía L1 y R1. Desactiva el archivo de mapeo."),
"Button mapping file": ("Fichier de mappage des touches", "Archivo de mapeo de botones"),
"Uses mappings/vita.conf from Lune's folder. Turns the swap above off.":
    ("Utilise mappings/vita.conf du dossier de Lune. Désactive l'échange ci-dessus.",
     "Usa mappings/vita.conf de la carpeta de Lune. Desactiva el intercambio de arriba."),
"PS button sent to the PC": ("Touche PS envoyée au PC", "Botón PS enviado al PC"),
"The PS button is sent to the game instead of opening the Vita home screen.":
    ("La touche PS est envoyée au jeu au lieu d'ouvrir l'écran d'accueil de la Vita.",
     "El botón PS se envía al juego en lugar de abrir la pantalla de inicio de la Vita."),
"Double tap to sprint": ("Double appui pour courir", "Doble toque para correr"),
"Pushing the left stick twice in a row holds the sprint button.":
    ("Pousser deux fois de suite le stick gauche maintient la touche de course.",
     "Empujar dos veces seguidas el stick izquierdo mantiene el botón de correr."),
"Double tap delay": ("Délai du double appui", "Tiempo del doble toque"),
"Longest time between the two pushes. X: type a value in milliseconds.":
    ("Durée maximale entre les deux appuis. X" + NB + ": saisir une valeur en millisecondes.",
     "Tiempo máximo entre los dos toques. X: escribir un valor en milisegundos."),
"Motion sensors": ("Capteurs de mouvement", "Sensores de movimiento"),
"Sends the gyroscope and the accelerometer to the PC.":
    ("Envoie le gyroscope et l'accéléromètre au PC.", "Envía el giroscopio y el acelerómetro al PC."),
"Motion sensitivity, horizontal": ("Sensibilité horizontale", "Sensibilidad horizontal"),
"Motion sensitivity, vertical": ("Sensibilité verticale", "Sensibilidad vertical"),
"X: type a value. 1 is the normal sensitivity.":
    ("X" + NB + ": saisir une valeur. 1 est la sensibilité normale.", "X: escribir un valor. 1 es la sensibilidad normal."),
"Sensitivity (1 = normal)": ("Sensibilité (1 = normale)", "Sensibilidad (1 = normal)"),
"Layout of the PC keyboard, used when typing with the Vita's on-screen keyboard.":
    ("Disposition du clavier du PC, utilisée pour la saisie avec le clavier à l'écran de la Vita.",
     "Distribución del teclado del PC, usada al escribir con el teclado en pantalla de la Vita."),
"Turn on Double tap to sprint first.": ("Activez d'abord Double appui pour courir.", "Activa primero Doble toque para correr."),
"Turn on Motion sensors first.": ("Activez d'abord Capteurs de mouvement.", "Activa primero Sensores de movimiento."),

# --- settings: Touch ---
"Front touch screen": ("Écran tactile avant", "Pantalla táctil frontal"),
"Trackpad: swipe moves the pointer, tap clicks. Or a PlayStation touch pad, direct pointing, a pen tablet (Sunshine).":
    ("Pavé tactile" + NB + ": glisser déplace le pointeur, toucher clique. Ou pavé PlayStation, pointage direct, tablette à stylet (Sunshine).",
     "Panel táctil: deslizar mueve el puntero, tocar hace clic. O panel PlayStation, puntero directo, tableta gráfica (Sunshine)."),
"Trackpad": ("Pavé tactile", "Panel táctil"),
"PS touch pad": ("Pavé tactile PS", "Panel táctil PS"),
"Direct pointing": ("Pointage direct", "Puntero directo"),
"Pen tablet": ("Tablette à stylet", "Tableta gráfica"),
"Pointer acceleration": ("Accélération du pointeur", "Aceleración del puntero"),
"Trackpad mode: the pointer travels further for the same swipe. 0: off.":
    ("Mode pavé tactile" + NB + ": le pointeur va plus loin pour un même geste. 0" + NB + ": désactivé.",
     "Modo panel táctil: el puntero llega más lejos con el mismo gesto. 0: desactivado."),
"Only used when the front touch screen is a trackpad.":
    ("Utilisé seulement quand l'écran tactile avant sert de pavé tactile.",
     "Solo se usa cuando la pantalla táctil frontal hace de panel táctil."),
"Touching a corner of the screen sends a key or runs an action.":
    ("Toucher un coin de l'écran envoie une touche ou lance une action.",
     "Tocar una esquina de la pantalla envía una tecla o ejecuta una acción."),
"Set up the corner shortcuts": ("Configurer les raccourcis des coins", "Configurar los atajos de las esquinas"),
"Size and position of the corners, and what each one does.":
    ("Taille et position des coins, et ce que fait chacun.",
     "Tamaño y posición de las esquinas, y qué hace cada una."),
"Turn on Corner shortcuts first.": ("Activez d'abord Raccourcis dans les coins.", "Activa primero Atajos en las esquinas."),
"Rear touch pad dead zone": ("Zone morte du pavé tactile arrière", "Zona muerta del panel táctil trasero"),
"Part of the rear touch pad that is ignored, to avoid accidental presses.":
    ("Partie du pavé tactile arrière qui est ignorée, pour éviter les appuis involontaires.",
     "Parte del panel táctil trasero que se ignora, para evitar toques accidentales."),

# --- settings: Advanced ---
"Optimise for the Internet": ("Optimiser pour Internet", "Optimizar para Internet"),
"For playing away from home. Leave off on your home network.":
    ("Pour jouer hors de chez vous. À laisser sur Non sur votre réseau domestique.",
     "Para jugar fuera de casa. Déjalo en No en tu red doméstica."),
"Reference frame invalidation": ("Invalidation des images de référence", "Invalidación de imágenes de referencia"),
"Recovers from lost frames without a key frame. NVIDIA hosts only: leave off otherwise.":
    ("Récupère après des images perdues sans image clé. Hôtes NVIDIA uniquement" + NB + ": sinon, laisser sur Non.",
     "Se recupera de imágenes perdidas sin imagen clave. Solo hosts NVIDIA: en otro caso, déjalo en No."),
"Let the PC adjust game settings": ("Laisser le PC régler les jeux", "Dejar que el PC ajuste los juegos"),
"GeForce Experience only: the PC changes the game's graphics settings for the stream.":
    ("GeForce Experience uniquement" + NB + ": le PC modifie les réglages graphiques du jeu pour le stream.",
     "Solo GeForce Experience: el PC cambia los ajustes gráficos del juego para el stream."),
"Frame pacer": ("Cadenceur d'images", "Regulador de imágenes"),
"Original frame pacing. Only used by the Direct display.":
    ("Cadencement d'origine. Utilisé seulement par l'affichage Direct.",
     "Regulación original. Solo se usa con la visualización Directo."),
"Wait for the screen refresh": ("Attendre le rafraîchissement", "Esperar el refresco de pantalla"),
"Direct display only: less tearing, a little more delay.":
    ("Affichage Direct uniquement" + NB + ": moins de déchirures, un peu plus de retard.",
     "Solo visualización Directo: menos desgarros, algo más de retardo."),

# --- connection, pairing and errors ---
"Pairing with %s": ("Appairage avec %s", "Emparejando con %s"),
"On the PC, open the Sunshine web page, choose PIN and type this code.":
    ("Sur le PC, ouvrez la page web de Sunshine, choisissez PIN et saisissez ce code.",
     "En el PC, abre la página web de Sunshine, elige PIN y escribe este código."),
"Waiting for the PC...": ("En attente du PC...", "Esperando al PC..."),
"Resume on %s": ("Reprendre sur %s", "Reanudar en %s"),
"This PC now answers from another address.\\nSaved: %s\\nNow: %s\\nUse the new address?":
    ("Ce PC répond désormais depuis une autre adresse.\\nEnregistrée" + NB + ": %s\\nActuelle" + NB + ": %s\\nUtiliser la nouvelle adresse" + NB + "?",
     "Este PC responde ahora desde otra dirección.\\nGuardada: %s\\nActual: %s\\n¿Usar la nueva dirección?"),
"Keep the link to the PC open": ("Garder la liaison avec le PC ouverte", "Mantener abierto el enlace con el PC"),
"Experimental: the menus and covers load faster. Turn off if the PC stops answering.":
    ("Expérimental" + NB + ": menus et jaquettes se chargent plus vite. À désactiver si le PC ne répond plus.",
     "Experimental: los menús y las carátulas cargan más rápido. Desactívalo si el PC deja de responder."),
"Unexpected answer from %s:\\n%s": ("Réponse inattendue de %s" + NB + ":\\n%s", "Respuesta inesperada de %s:\\n%s"),
"This Sunshine or GameStream version is not supported:\\n%s":
    ("Cette version de Sunshine ou de GameStream n'est pas prise en charge" + NB + ":\\n%s",
     "Esta versión de Sunshine o GameStream no es compatible:\\n%s"),
"The PC answered with an error:\\n%s": ("Le PC a répondu par une erreur" + NB + ":\\n%s", "El PC respondió con un error:\\n%s"),
"No answer from %s.\\nIs the PC on, with Sunshine running?":
    ("Aucune réponse de %s.\\nLe PC est-il allumé, avec Sunshine lancé" + NB + "?",
     "Sin respuesta de %s.\\n¿Está el PC encendido y con Sunshine en marcha?"),
"The PC cannot stream in 4K.": ("Le PC ne peut pas diffuser en 4K.", "El PC no puede transmitir en 4K."),
"The PC cannot stream at %d x %d, %d fps.\\nChoose another resolution in Settings.":
    ("Le PC ne peut pas diffuser en %d x %d, %d img/s.\\nChoisissez une autre résolution dans les Réglages.",
     "El PC no puede transmitir a %d x %d, %d fps.\\nElige otra resolución en Ajustes."),
"The PC could not start the game (error %d).\\n%s":
    ("Le PC n'a pas pu lancer le jeu (erreur %d).\\n%s", "El PC no pudo iniciar el juego (error %d).\\n%s"),
"The stream could not start.\\nFailed step: %s (error %d)":
    ("Le stream n'a pas pu démarrer.\\nÉtape en échec" + NB + ": %s (erreur %d)",
     "El stream no pudo iniciarse.\\nPaso fallido: %s (error %d)"),
"Could not quit the game (error %d).\\n%s":
    ("Impossible de quitter le jeu (erreur %d).\\n%s", "No se pudo cerrar el juego (error %d).\\n%s"),
"Pairing failed (error %d).\\n%s\\nCheck the code typed on the PC, then try again.":
    ("Échec de l'appairage (erreur %d).\\n%s\\nVérifiez le code saisi sur le PC, puis réessayez.",
     "Error al emparejar (error %d).\\n%s\\nComprueba el código escrito en el PC e inténtalo de nuevo."),
"%s is paired with this Vita.": ("%s est appairé avec cette Vita.", "%s está emparejado con esta Vita."),
"Could not get the list of games (error %d).\\n%s":
    ("Impossible d'obtenir la liste des jeux (erreur %d).\\n%s", "No se pudo obtener la lista de juegos (error %d).\\n%s"),
"Contacting %s...": ("Contact de %s...", "Contactando con %s..."),
"Could not save this PC.": ("Impossible d'enregistrer ce PC.", "No se pudo guardar este PC."),
"Address of the PC (for example 192.168.1.20)":
    ("Adresse du PC (par exemple 192.168.1.20)", "Dirección del PC (por ejemplo 192.168.1.20)"),
"Name for this PC": ("Nom de ce PC", "Nombre de este PC"),
"%s is not paired with this Vita.\\nAdd it again from the home screen.":
    ("%s n'est pas appairé avec cette Vita.\\nAjoutez-le de nouveau depuis l'écran d'accueil.",
     "%s no está emparejado con esta Vita.\\nAñádelo de nuevo desde la pantalla de inicio."),
"Connecting to %s...": ("Connexion à %s...", "Conectando con %s..."),
"Smooth edges": ("Lissage des contours", "Suavizado de bordes"),
"4x anti-aliasing of the menus and the stream. Restart Lune to apply a change.":
    ("Anticrénelage 4x des menus et du stream. Redémarrez Lune pour appliquer un changement.",
     "Antialiasing 4x de los menús y del stream. Reinicia Lune para aplicar un cambio."),
"Touch the rear pad: a dot shows each touch that counts.":
    ("Touchez le pavé arrière" + NB + ": un point montre chaque appui pris en compte.",
     "Toca el panel trasero: un punto muestra cada toque que cuenta."),
"Zones of the front screen that trigger a shortcut during a stream.":
    ("Zones de l'écran avant qui déclenchent un raccourci pendant le stream.",
     "Zonas de la pantalla frontal que activan un atajo durante el stream."),
"Choose": ("Choisir", "Elegir"),
# --- performance options from the dubleyu and juanjsebgarcia forks ---
"Paced": ("Cadencé", "Acompasado"),
"Smooth: one frame per refresh. Paced: frames at the PC's pace, a little later. Low latency: newest at once.":
    ("Fluide" + NB + ": une image par rafraîchissement. Cadencé" + NB + ": au rythme du PC, un peu plus tard. Faible latence" + NB + ": la plus récente aussitôt.",
     "Fluido: una imagen por refresco. Acompasado: al ritmo del PC, algo más tarde. Baja latencia: la más reciente al instante."),
"Fast colour decoding": ("Décodage rapide des couleurs", "Decodificación rápida del color"),
"The graphics chip converts the colours instead of the decoder: about 2 ms less per frame.":
    ("La puce graphique convertit les couleurs à la place du décodeur" + NB + ": environ 2 ms de moins par image.",
     "El chip gráfico convierte los colores en lugar del decodificador: unos 2 ms menos por imagen."),
"Wait for late packets": ("Attendre les paquets en retard", "Esperar paquetes retrasados"),
"Wi-Fi delivers some packets out of order. Waiting for them avoids a frozen picture.":
    ("Le Wi-Fi livre certains paquets dans le désordre. Les attendre évite une image figée.",
     "El Wi-Fi entrega algunos paquetes desordenados. Esperarlos evita una imagen congelada."),
"No wait": ("Pas d'attente", "Sin espera"),
"1024, Moonlight's value, lost the fewest packets on a Vita. Larger: fewer packets per frame.":
    ("1024, la valeur de Moonlight, a perdu le moins de paquets sur une Vita. Plus grand" + NB + ": moins de paquets par image.",
     "1024, el valor de Moonlight, perdió menos paquetes en una Vita. Mayor: menos paquetes por imagen."),
"Spread work over the cores": ("Répartir le travail sur les cœurs", "Repartir el trabajo entre los núcleos"),
"Network, decoding and display each on their own core. Off: the system places them.":
    ("Réseau, décodage et affichage chacun sur son cœur. Désactivé" + NB + ": le système les place.",
     "Red, decodificación y visualización, cada uno en su núcleo. Desactivado: el sistema los coloca."),
# --- second batch: Bluetooth, restart, controls ---
"Bluetooth is on and can disturb the Wi-Fi: if you use no Bluetooth device, turn it off in the Vita's Settings.":
    ("Le Bluetooth est activé et peut perturber le Wi-Fi" + NB + ": si vous n'utilisez aucun appareil Bluetooth, désactivez-le dans les Paramètres de la Vita.",
     "El Bluetooth está activado y puede perturbar el Wi-Fi: si no usas ningún dispositivo Bluetooth, desactívalo en los Ajustes de la Vita."),
"Restarting the stream...": ("Relance du stream...", "Reiniciando el stream..."),
"Some settings you changed only apply to a new stream.\\nRestart the stream now? The game keeps running on the PC.":
    ("Certains réglages modifiés ne s'appliquent qu'à un nouveau stream.\\nRelancer le stream maintenant" + NB + "? Le jeu continue sur le PC.",
     "Algunos ajustes que cambiaste solo se aplican a un nuevo stream.\\n¿Reiniciar el stream ahora? El juego sigue en el PC."),
"Read the controls in step with the Vita": ("Lire les commandes au rythme de la Vita", "Leer los controles al ritmo de la Vita"),
"Reads the buttons once per sample of the Vita instead of every 2 ms. If touch feels slower, turn it off.":
    ("Lit les boutons une fois par relevé de la Vita au lieu de toutes les 2 ms. Si le tactile semble plus lent, désactivez.",
     "Lee los botones una vez por muestreo de la Vita en lugar de cada 2 ms. Si el táctil parece más lento, desactívalo."),
}
