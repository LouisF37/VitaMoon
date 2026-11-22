# Changelog

All notable changes of this project. Versions follow [Semantic Versioning](https://semver.org);
milestones are named after the phases of the moon ([docs/RELEASING.md](docs/RELEASING.md)).

## 1.5.0 "Premier Croissant" (2026-11-22)

The first final version since 1.0.0: everything of the four 1.1.0 betas (in short at the end of this
section), and rounded covers with a selection frame that shows on any cover. It follows 1.1.0-beta.4 directly: there is no final 1.1 to
1.4. Settings and pairings are kept; the stream behaves as in 1.0.0 unless an option of *Tests (beta)*
is turned on.

### New since 1.1.0-beta.4

- Covers have rounded corners, like the coloured tiles shown until they are downloaded: a twelfth of
  their width (10 px for a 120 px cover), so the selected cover, drawn larger, keeps the same shape.
  The corners are made transparent once, when a cover is loaded (`src/gui/cover_shape.c`): drawing
  costs nothing more, it looks right over the moving sky, and covers already downloaded are rounded
  too. Tiles and shadows take the same radius.
- The selected cover, on the home screen as in the library, still grows and now has a 3 px white
  frame, set off from it by a 2 px dark line, so that it shows on a light cover as on a dark one (it
  was a thin gold line on the home and a thin white line in the library, lost on light covers). The
  home keeps its halo in the theme's colour.
- Tests: `tools/tests/test_cover.c` (12 checks: transparent corners, a smooth edge, the four corners
  alike, nothing else touched); `PREVIEW_COVERS=1` gives games of the preview abstract covers (two
  light, two dark, one bright), rendered by `tools/ui-preview/run.sh` in `covers/`.

### Since 1.0.0, in short

- Home screen: a side menu (Home, Library, Computers, Settings, Help, Quit) with a preview of each
  entry; a large card that says what X does (continue the last game, resume a paused stream, wake the
  PC...) and a row of covers (games played last, favourites, *See all*); one element selected at a
  time, clearly shown; the right arrow enters a preview.
- Help page: shortcuts during a stream, touch, adding games in Sunshine, Wi-Fi.
- Bigger covers: 120 x 160 on the home, 140 x 187 in the library (168 x 224 selected).
- Look: Eclipse and the moving sky at a first start (settings already saved are kept).
- Statistics overlay on one line: frames shown per second, receive to screen, bitrate.
- Quick menu: the stream's duration, the battery time left, and a press-to-picture test.
- *Settings > Touch*: the rear touch pad can be turned off.
- *Settings > Tests (beta)*: five options to measure (decoder delay mode, decoder reference frames,
  skip late frames, show frames at once, 5 ms sound packets), all off by default; they stay until
  sessions with them decide what to keep ([docs/design/tests-1.1.md](docs/design/tests-1.1.md)).
- The decoder's memory is freed whatever step of its creation failed.

## 1.1.0-beta.4 "Premier Croissant" (2026-11-19)

Bigger covers, after a test on a Vita: at 96 x 128 they could not be read well. Nothing else changes.

- Home: the row under the large card shows four games and *See all* at 120 x 160 (was five at
  96 x 128): up to two other games played last, then the favourites (so that they stay in sight),
  then the other recent games and the library. The large card is 160 px high (was 182), its cover
  96 x 128 (was 108 x 144).
- Home, Library preview: two rows of five covers at 120 x 160 (was six at 100 x 133), the first
  ten games.
- Library page: covers of 140 x 187 (was 120 x 160), 168 x 224 for the selected one (was 150 x 200);
  about five and a half in sight; the "1 / 8" counter moves up with them.
- Tests: the row's order checked again (`test_home.c`, 60 checks); every screen rendered in three
  languages, the 33 journeys, and the home and library screens clean under AddressSanitizer and UBSan;
  a frame of the home uses at most 7.7% of vita2d's vertex pool, the library 5.8%.

## 1.1.0-beta.3 "Premier Croissant" (2026-11-18)

The home screen reworked after the first tests on a Vita, a Help page, and Eclipse as the starting
look. The *Tests (beta)* options of beta.1 are unchanged.

### Home screen

- One element at a time is selected, and it shows: lifted a little, lit, with a thick border and a
  halo in the theme's colour. A card's button takes the theme's colour only when the card is selected
  (the large card's button was always in that colour, so one could not tell it was selected).
- The same moves everywhere: the right arrow enters the preview of any entry that has something to
  pick (Home, Library, Computers, Settings, Help), the arrows move in it, X acts on what is selected,
  left from the first column or O goes back to the menu. X on a menu entry still opens its page.
- Home: a large card that says what X does now (*Continue* the game played last, *Resume* a paused
  stream, *Wake the PC* when the scan finds it off and its network address is known, *PC options*
  when it does not answer or answers from a new address, *Open the library* before the library has
  been opened once), and under it a row of covers: up to three other games played last, then the
  favourites, then the library's next ones, and *See all*. X on a cover starts the game at once. The
  PC and last-session tiles are gone (the PC's state is on the large card when it matters; the last
  session's figures stay in the library and in the statistics files).
- Library: X on one of the twelve covers of the preview starts that game at once.
- Computers: the PCs (one in full, several in a list) and *Search the network* / *Enter an address*;
  X connects to an online PC or opens the menu of one that is not, square opens the selected PC's menu.
- Settings: one card per category (General, Video, Performance, Controls, Touch, Advanced, Tests),
  each with its main values; X or a tap opens the Settings page on that category, on its first option
  (before, it opened on the category last visited, General at first). *Advanced* says *Default* while
  none of its options that can be changed differs from VitaMoon's starting values. The caption "what
  is set now" is gone.
- Help, a new entry before Quit: four cards and a page (L / R or up / down to change topic). *During a
  stream*: L + R + SELECT (quick menu), START + L + R (pause), START + left (keyboard), the PS button.
  *Touch*: the front screen's mode, the rear pad (L2 / R2 at the top, L3 / R3 below), the corner
  shortcuts. *Adding games*: Sunshine offers only Desktop and Steam Big Picture at first; how to add a
  game in its web page (Applications, Add New, Detached Commands `steam://rungameid/<number>`, Find
  Cover). *Wi-Fi*: power saving and Bluetooth as the Vita reports them, the 2.4 GHz band, a cable for
  the PC.
- Dates under the covers are short ("Yesterday", "5 d ago", "2 wk ago").
- Kept light: the summaries of the settings and the help are read when the home opens or comes back,
  not every frame; up to 20 covers (large card, row, library preview) load one per frame and are freed
  before any other screen; a frame of the home uses at most 7.7% of vita2d's vertex pool (preview count).

### Look

- Eclipse is the theme VitaMoon starts with, and the stars of the menus turn around the moon (Moving
  sky: Medium, one turn in 8 minutes, clockwise). Only a first start (or a settings file without these
  lines) is concerned: settings already saved are kept.

### Tests

- `tools/tests/test_home.c` rewritten for the panels (59 checks: the menu and its six entries, every
  panel's moves, the Settings cards' two rows with a card over two columns, panels changed under the
  cursor, the row's recent games, favourites and the rest). `test_config.c` checks the starting look.
  The preview renders every panel, the Settings page opened from a card, the four Help topics, the
  large card of a PC that is off, and the Wi-Fi warnings, in three languages; the home journeys go
  through the six-entry menu. These screens and the home journeys run clean under AddressSanitizer and
  UBSan.

## 1.1.0-beta.2 "Premier Croissant" (2026-11-17)

A new home screen and a lighter statistics overlay. The *Tests (beta)* options of beta.1 are unchanged.

### Home screen

- A side menu (Home, Library, Computers, Settings, Quit) and, on its right, a preview of the entry
  under the cursor. On Home, a dashboard: the game played last on the PC used last (X starts it at
  once, without the library, or goes back into a paused stream), the PC and its state, the last
  session's figures (with *Statistics in the library*, otherwise the stream's settings), and a strip
  of games (the others played last, then the library's next ones) ending with *See all*.
- The previews: the library's first twelve games, the paired PCs with their state, the main settings
  (video, profile, controller, touch, the *Tests (beta)* options turned on), Quit. X opens the page full
  screen, as before; O comes back to the home, and in the menu O brings the cursor back to Home.
- *Computers* is the former home screen (the PCs, search, address, the advice about Wi-Fi power saving
  and Bluetooth), now a page with a title; Settings and Quit moved to the side menu.
- The home shows a PC's games without asking the PC: the library leaves a small file when it is left,
  `<data folder>/<PC name>/home.txt` (the start of the shelf, the games played last, the covers'
  folder), written only when something changed. Until a PC's library has been opened once with this
  version, the home offers to open it.
- A PC the scan has not seen yet shows *Checking...* for the few seconds a first pass takes, instead
  of *Offline*.
- Starting a game from the home while another one runs on the PC asks first, as the library does.
- Kept light: the files are read when the home opens or comes back, never while it is shown; covers
  load one per frame, 16 at most, and are freed before any other screen opens (a stream needs the
  memory); a frame of the home uses at most 6.4% of vita2d's vertex pool (preview count).

### Statistics overlay

- One line: frames shown per second, receive to screen (ms) and bitrate. Every other figure stays in
  each session's CSV and `.txt`, unchanged.

### Tests

- PC tests of the home's logic (`tools/tests/test_home.c`, 48 checks: cursor, the PC's file, the PC
  shown, the strip). Four journeys start from the home: continue the last game, resume a paused
  stream, a game of the strip, a switch from the game running on the PC. Every state of the home is
  rendered in three languages (the dashboard also in the five themes); these screens and all the
  journeys run clean under AddressSanitizer and UBSan.

## 1.1.0-beta.1 "Premier Croissant" (2026-11-17)

A test version: options to measure before 1.1.0 keeps the best ones, all in a new category,
*Settings > Tests (beta)*, each set to 1.0's behaviour by default. What each one changes and where to
read its effect: [docs/design/tests-1.1.md](docs/design/tests-1.1.md); in French, with the test
protocol: [docs/GUIDE-FR.md](docs/GUIDE-FR.md#essais-de-la-11-bêta).

### Tests (beta)

- *Decoder delay mode* (Off, Low, Mode 1, Mode 2, High): the hardware decoder's delay mode, which the
  original Moonlight Vita 1.0 sets to Low on every stream. Weak import, so VitaMoon starts even if a
  system lacks it; the log gives the decoder's answer.
- *Decoder reference frames* (4, 2, 1): 1.0 creates the decoder for 4. Made again with 4 if the
  decoder refuses; the log gives how many the PC's encoder declares.
- *Skip late frames*: after a Wi-Fi freeze, frames over 100 ms late are decoded but not shown, against
  a reference that follows the drift between the PC's clock and the Vita's (tested on a PC).
- *Show frames at once*: with the *Low latency* display, a frame goes on screen without waiting for
  the refresh (8 ms sooner on average; tearing possible).
- *5 ms sound packets*: the PC sends 5 ms of sound per packet instead of 10.

### Measures

- Press-to-picture test: quick menu > *Measure the delay (test page)*, with
  [tools/latency/latency.html](tools/latency/latency.html) full screen on the PC. VitaMoon presses A
  itself 20 times and times the white picture's arrival on its screen, in parts (to the PC and back,
  decoding, display); result on screen and in `stats/latency-<date>.txt`. Its logic is simulated on a
  PC (`tools/tests/test_latency.c`).
- Each sound gap is written in the log with how long ago the last packet came, how many were waiting
  and how many were lost; new statistics columns `late_hidden` and `aud_wait_max_us`, and the tests'
  settings in each session's `.txt`.

### Other changes

- *Settings > Touch > Rear touch pad*: the rear pad can be turned off (on by default, as before). Its
  dead zone stays at 0 by default: the original Moonlight Vita set one of 80 points, then went back to
  0.
- The quick menu shows how long the stream has been running and the battery time the Vita estimates,
  next to the PC's name (the percentage and the clock were already in its top bar).
- The decoder's memory is freed whatever step of its creation failed (a failure between two
  allocations left it behind).
- Documentation: the original repository's 1.0 in [docs/design/forks.md](docs/design/forks.md), and
  advice on Sunshine's `minimum_fps_target` and the PC-side tests in the French guide.

## 1.0.0 "Nouvelle Lune" (2026-11-17)

First version of VitaMoon, a Moonlight client for the PS Vita, from
[xyzz/vita-moonlight](https://github.com/xyzz/vita-moonlight) 0.13.2 (commit `984603b`, kept as the
branch `legacy/vita-moonlight-0.13.2`). It installs next to Moonlight (title ID `VMOON0001`, data in
`ux0:data/vitamoon`). It was prepared through eleven pre-releases on 15 and 16 November, the first
named *Lune*; their detail is in [docs/history/betas.md](docs/history/betas.md).

### Measured on a Vita

Eight sessions on 16 November with Sunshine 2026.914 (1280x720, 60 fps, 10 Mbps, *Smooth* display,
Final Fantasy IX), the longest 34 minutes with the video path of 1.0.0:

- 96 % of the frames are shown while the PC sends 60 a second (57.2 of 59.8 a second);
- 97 ms median from a frame's first packet to its picture on screen, about 60 ms of it the Vita's
  hardware decoder holding 3 to 4 pictures; when the PC sends about 20 frames a second (a still
  screen), about 170 ms;
- no crash and no decoder error; a dozen freezes, from 0.4 s to about 2.7 s, when the Wi-Fi lost
  whole frames, each over with the next key frame.

Known: short gaps in the sound, about 6 a minute in these sessions (cause not found yet); the
press-to-picture delay is not measured. Details and the test plan:
[docs/checkpoints/2026-11-17.md](docs/checkpoints/2026-11-17.md).

### Streaming

- Video and audio decoded off the network threads (dubleyu/vita-moonlight-relay); moonlight-common-c
  updated (upstream `e2722dc`) with a short wait for late or reordered video packets
  (juanjsebgarcia, *Wait for late packets*) and NEON error correction (nanors).
- Display modes through profiles: *Smooth* (one frame per refresh, drawn just in time), *Paced* (at
  the PC's capture cadence), *Lowest latency* (newest frame at once), *Original*.
- *Fast colour decoding*: the GPU converts the decoder's YUV output (juanjsebgarcia: 4.6 → 2.4 ms a
  frame). Decoder catch-up takes back pictures the hardware decoder keeps (method from VitaRPS5).
- Low-latency sound with an adaptive buffer.
- Streaming threads at the highest priority, spread over the three cores; controls read in step with
  the controller; clocks raised and Wi-Fi in performance mode while connecting and streaming; the PC
  asked to capture at the screen's rate (59.94 Hz); 1024-byte video packets; an optimised build.
- **Automatic reconnection** after a Wi-Fi drop or a sleep of the Vita, to the same game, which kept
  running on the PC; it gives up after a minute, or when the game was closed on the PC.
- Starting a game while another runs on the PC offers to quit that one first (NowaLone/vita-moonlight).

### Interface

- A lunar sky behind the menus in five colour themes (Night, Dawn, Aurora, Twilight, Eclipse), whose
  stars can twinkle (on) and turn around the moon (*Moving sky*, off: one turn in 32, 16, 8 or 4
  minutes, either way). Poppins for titles, Inter for text. English, French and Spanish.
- A start-up screen from the first frame: the moon waxes while VitaMoon loads, then becomes its mark
  in the home screen's top bar. Settings open at once.
- Home screen with each PC's state and details, and warnings when Wi-Fi power saving or Bluetooth is
  on. Pairing shows the code and the address of Sunshine's page, can be cancelled, and gives the PC's
  reason when it fails; the Vita pairs under its own nickname.
- Library with cover art and when each game was last played; favourites (SELECT) first, then the
  games last played. The last session's figures are an option (*Statistics in the library*).
- Two-pane settings with an explanation for every option. Resuming after a change that needs a new
  stream offers to restart it.
- A moon that fills at each step while a stream starts or reconnects.
- **In-game quick menu** (L + R + SELECT): back to the game, statistics, keyboard, pause, quit the
  game; the game gets no button while it is open.

### Controls

- Keyboard layout *Text, any PC layout* (the default): typed text arrives as typed, whatever the PC's
  keyboard layout.
- Front touch screen as a trackpad: a tap clicks when lifted in time without moving more than 20
  pixels, as in Moonlight for Android; *Tap to click* sets the time, 100 to 250 ms.
- A pause or the end of a stream releases on the PC every button, stick, click and touch held.

### Logs and statistics

- A log of every run in `ux0:data/vitamoon/logs/vitamoon-<date>.log`, the last 30 runs kept: start-up,
  each PC and stream with its timed steps and how it ended, reconnections, errors, and a health line
  a minute. It is written by a low-priority thread, so a stream never waits for the memory card. The
  logs of the pre-releases (`vitamoon.log`, `vitamoon.1.log` .. `vitamoon.4.log`) are moved there on
  the first start.
- Statistics on screen during play; for each stream a CSV (one line a second) and a summary with
  percentiles, in `ux0:data/vitamoon/stats`, the last 30 streams kept.

### Fixed

Inherited from vita-moonlight 0.13.2 or found on the way, among others: a game the PC refused stayed
"running" for the Vita; pairing could wait without end; a looping network packet could freeze the
search for PCs; a PC's name was used as a folder name without being checked; a settings file that
could not be written closed the app; the menus and the decoder could draw at the same time; buttons
could stay held on the PC after a pause; memory leaks at each PC opened and each library visit. In
1.0.0: the screen refresh count of the statistics files went back by 65536 every 18 minutes.

### Project

- Code, comments and logs in English; documentation in `docs/`, with a French user guide.
- Checked on a PC with AddressSanitizer and UBSan: 349 checks of the logic, every screen rendered in
  three languages and five themes, and 29 journeys through the real menus and connection code
  against an imitation of Sunshine. CI builds, tests and publishes each version.

## Before this project

The history of the Vita port up to 0.13.2 is in
[xyzz/vita-moonlight's changelog](https://github.com/xyzz/vita-moonlight/blob/vita/CHANGELOG.md).
