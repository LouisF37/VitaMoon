# Changelog

All notable changes of this project. Versions follow [Semantic Versioning](https://semver.org);
milestones are named after the phases of the moon ([docs/RELEASING.md](docs/RELEASING.md)).

## 1.0.0-beta.10 "Nouvelle Lune" (2026-11-16)

From the first sessions on a Vita: the start of the app and the first opening of Settings.

### Changed

- **Start-up screen.** VitaMoon opened on a black text screen showing its version for about two
  seconds, then the menus. It now shows the theme's sky from the first frame vita2d can draw, with
  the moon waxing as VitaMoon loads; once everything is loaded the moon becomes the VitaMoon mark,
  which then moves with the name into the top bar of the home screen, where the menus fade in.
  The idea of a start-up animation comes from VitaRPS5 (its logo assembles from particles while it
  loads); the moon, its phases and the move to the top bar are VitaMoon's own.
- **Faster loading.** What loaded one thing after another now overlaps: the network, the controls
  and the PCs are set up on a thread of their own while the menu thread renders the fonts, a slice
  per frame so that the moon keeps moving; the start runs at the clock speeds used while connecting.
  The log records how long each part took and when the home screen appeared
  (`start-up: first frame ... ms after launch; ...`).

### Fixed

- **Settings took a moment to open the first time.** It started and stopped the Vita's video decoder
  for each of the 9 stream sizes, to list those it accepts. This now runs in the background from the
  start of the app; Settings, and the start of a stream (which uses the decoder too), wait for it to
  end. The log records how long it took.

### Tests

- `test_splash`: when the moon changes, that the mark only comes once everything is loaded, that a
  long wait does not make the moon skip its phases, that the mark ends exactly over the top bar's and
  the name where the top bar draws it, and that the name never covers the mark on the way (it did in
  a first version: the test fails on it). 280 PC checks.
- The preview renders the start-up screen from its first frame to the home screen (scenario
  `splash`; `PREVIEW_FRAMES=<folder>` writes every frame, for a video). Every other screen renders
  exactly as in beta.9 (pixel comparison of the 60 French pictures).

## 1.0.0-beta.9 "Nouvelle Lune" (2026-11-16)

A full review after the first tests on a Vita: every journey of a user replayed on a PC through
the real menus, the real connection code and the real libgamestream against an imitation of
Sunshine 2026.914; every module read; static analysis; memory checked.

### Fixed

- **A game the PC refused stayed "running" for the Vita** (inherited from upstream). libgamestream
  recorded the game as running as soon as the PC answered, before reading the answer; Sunshine
  refuses inside a normal answer ("An app is already running", "No running app to resume"). Every
  Play then asked to resume a game that did not run, was refused, and so on, with no way out. The
  game is now recorded only once the PC has accepted it.
- **The library follows the PC.** What runs on the PC is read again each time the library opens: a
  game closed on the PC no longer shows *Resume*, a game started there does.
- **Reconnection gives up when the game was closed on the PC** meanwhile, with a message, instead of
  trying for a minute; it never starts the game again by itself.
- **A paused stream of another game** is ended before a new stream starts (moonlight-common-c runs
  one at a time).
- **The network search could freeze.** Its mDNS reader followed DNS name pointers without limit: a
  packet whose name points to itself, from any device on the network, looped for ever. Names now
  follow 16 pointers at most, inside the packet, and a record longer than its packet ends the
  reading. mdnsniff is now a copy in the repository with this change (it was a submodule, which the
  change could not reach).
- **The PC detection thread of the home screen did not really stop.** It was given 100 ms, then
  deleted while still running, which fails: it went on, next to the following one, sharing the mDNS
  reader and reading the list of PCs while the menus changed it (forget, rename). It now stops
  within a step, is waited for and deleted; the list of PCs is guarded by a lock. The network search
  thread is also deleted every time (one was kept per search) and can be stopped right after it
  starts.
- **The PS button could stay locked in the menus after a stream**: it was unlocked from the menus
  while the input thread could lock it again. Only the input thread locks and unlocks it now.
- **Memory**: the pairing certificate was loaded again, without freeing the previous one, each time a
  PC was opened (a few KB); the list of games at each visit of the library; the display modes, the
  status strings and the stream address at each request; a copy of each refusal message. The
  certificate's text is read within its buffer. Sorting the games no longer allocates, and an empty
  title or mode no longer reads uninitialised memory.
- **A failed start-up** (network, controls) went on without them and crashed later; it now stops
  with a message (the PS button closes the app).
- **Statistics files**: every stream wrote two files that were never deleted (about 1 MB an hour).
  The last 30 streams are kept.

### Tests

- The preview runs **journeys**: the real menus, `src/connection.c` and libgamestream (`client.c`,
  `xml.c`) against `fake_http.c`, which answers as Sunshine 2026.914 does (from its `nvhttp.cpp`), and
  a fake moonlight-common-c that refuses a stream nobody launched. 29 journeys, from opening the PC
  to the end of a game: play, pause, resume, quit, switch game, settings and restart, stream lost
  and reconnected or given up, PC refusing, pairing. Every request to the PC must be expected;
  a wait without drawing for 15 minutes is a hang. They fail on the code before the fixes they
  cover (Play in the library on beta.7, the refused launch). With AddressSanitizer they also check
  for leaks.
- `test_mdns`: a Sunshine answer is understood; pointer loops, every cut of an answer and 20000
  random or damaged packets neither hang nor crash the reader (the original reader hangs).
- `test_misc`: which statistics files are deleted. 240 PC checks.

## 1.0.0-beta.8 "Nouvelle Lune" (2026-11-16)

### Fixed

- **Play in the library did nothing** (seen on the first test on a Vita). Opening a PC leaves the
  session *ready*; it is marked *paired*, the state a stream starts from, only when the classic menu
  (triangle) is built. Since the library became the first screen (Lune 1.0.0-beta.1), Play from it
  started nothing and the library came back. The launch now marks a ready session of a paired PC
  itself, and a press that cannot start a stream is logged and explained instead of ignored.

### Tests

- New preview scenario `play`: opens a paired PC the way the home screen does, then presses X on a
  cover; it fails if the PC is not asked to start the game (it fails on beta.7). The preview's
  `connection_reset()` now changes the session state as the real one does.

## 1.0.0-beta.7 "Nouvelle Lune" (2026-11-16)

Fixes from a review of what the PC preview cannot see: the limits of the Vita and of the network.

### Fixed

- **Pairing could wait without end.** The Vita waited for the code to be typed on the PC with no time
  limit and no way out (inherited from upstream): a PC that went to sleep or a lost Wi-Fi left the
  code on screen for good. The pairing screen now has *Cancel* (O, or X with the buttons swapped),
  and the Vita gives up after 5 min 30 s. Sunshine 2026.914 itself drops a request after 5 minutes
  and says so; VitaMoon now explains that case, and the one where a request from this Vita is still
  waiting on the PC (after a cancel: remove it on Sunshine's PIN page, or wait 5 minutes).
- **The reason of a failed pairing was lost.** After a failure the Vita asks the PC to forget it;
  Sunshine has no such request and answers 404, whose curl message ("HTTP response code said error")
  replaced the PC's own reason on screen.
- **Stack of the stream's display thread**: 64 KB, now 256 KB as moonlight-common-c's threads. That
  thread draws the quick menu and the statistics; rendering a glyph not drawn before took FreeType up
  to 69 KB on a PC (Poppins, through its auto-hinter).
- **Glyphs rendered during play.** The no-break spaces of the French quick menu and the "oe" ligature
  were not among the characters rendered at start-up; the network and sprint icons were rendered the
  first time they showed; a long game name was cut to the quick menu's width by the display thread.
  All of it now happens before the game.

### Tests

- `test_ui`: every character of the translations is among those rendered at start-up (it found the
  no-break spaces). 228 PC checks.
- The PC preview also counts what each frame takes from vita2d's vertex pool, which vita2d does not
  check: peak 9% of it, on the settings screen.
- Pairing cancel and time limit tried on a PC against a server that never answers: cancelled at
  once, limit reached on time, the next request on the same connection runs without the check.

## 1.0.0-beta.6 "Nouvelle Lune" (2026-11-16)

### Fixed

- Changing the theme in *Settings > General* froze the Vita (seen on the first test on a Vita). The
  setting is applied in the middle of a frame that has already drawn the old background: the old
  picture was freed, after a wait for the GPU, while that frame was still open and about to use it.
  It is now freed at the end of the frame, once the GPU has finished with it (`theme_end_frame()`).
  Present since themes came in, 1.0.0-beta.2; the PC preview could not show it, its drawing being
  immediate.
- A settings file that could not be written closed the app at once (inherited from upstream). The
  error is now logged and the app goes on with the settings in memory.

### Tests

- The PC preview's fake vita2d now checks two rules of the Vita's GPU: no wait for the GPU while a
  frame is open, no texture freed before the GPU has finished the frames that drew it. The settings
  scenario changes the theme; beta.5's code fails there. 227 PC checks.

## 1.0.0-beta.5 "Nouvelle Lune" (2026-11-16)

### Fixed

- A PC's name is also the name of the folder holding its pairing keys and settings: it is now checked
  before that folder is renamed, used or emptied. A PC removed from the list could empty another
  folder if its name was empty, `.`, `..` or held a path separator; names announced on the network
  (mDNS) are made plain folder names (forbidden characters become `-`). Inherited from 0.13.2; no
  such name is known to have occurred.
- The list of PCs found on the network is bounded (64): more answers wrote past its end.

### Project

- The repository's branch is `main`, from which CI publishes each version; the development build is
  `vitamoon-main.vpk` ([docs/RELEASING.md](docs/RELEASING.md)).
- Documentation tidied: a current checkpoint with the full test plan for the Vita
  ([docs/checkpoints/2026-11-16.md](docs/checkpoints/2026-11-16.md)), the 15 November report kept
  as it was, editor settings inherited from upstream removed.

## 1.0.0-beta.4 "Nouvelle Lune" (2026-11-16)

### Changed

- Nothing of the new look is prepared or kept during a game. The names the quick menu draws over the
  game (the game's, the PC's) are prepared on the menu thread before the stream, with the title
  font's fallback when the game's name has a character Poppins lacks, so the display thread
  rasterises nothing when the menu first opens. The moon picture (0.6 MB of graphics memory) is freed
  once the full moon is drawn, before the video starts, and before each reconnection try.

## 1.0.0-beta.3 "Nouvelle Lune" (2026-11-16)

### Changed

- A look of VitaMoon's own for the menus, which looked too much like VitaRPS5's:
  - **Lunar sky** behind every menu, in place of the waves: faint stars (none behind the top bar's
    text, fewer towards the horizon), a large faint crescent low on the right, a soft glow on the
    horizon, and one touch per theme (aurora curtains, a warm dawn horizon, a dark disc with its
    corona for Eclipse). Still one picture per theme, drawn once per frame as before
    (`tools/make_background.py`).
  - **Type**: Poppins, the logo's typeface, for the titles, and Inter for the text, in place of Roboto.
    Both are cut down to Latin (with Vietnamese), Greek and Cyrillic by `tools/art/make_fonts.py`: 350 KB
    for the three files against 342 KB for the two Roboto ones. A title with a character Poppins lacks
    (Greek, Cyrillic, a few accented letters) is drawn in Inter. Glyphs are still prepared at start-up
    for every size.
  - **The waiting moon**, in place of the "Starting stream..." box that stayed on screen until the game
    appeared: contacting a PC and starting a stream show a moon that fills at each stage of the start,
    the stage written under it, and is full, the VitaMoon mark, as the video starts. The reconnection
    screen shows it filling over the minute of tries. Pictures are drawn on the menu thread, at most
    one per 50 ms and never once the video thread may draw; the log says how many and how long
    (`waiting moon: N pictures drawn in M ms`), to compare start times with beta.2.

## 1.0.0-beta.2 "Nouvelle Lune" (2026-11-16)

### Identity

- Named **VitaMoon**, with a new logo (a disc with a V cut out of its top, in the phases of the moon)
  drawn by `tools/art/make_brand.py`: LiveArea bubble, gate and background, the mark in the menus' top
  bar, the README logo and the GitHub preview image. The LiveArea background no longer uses the
  photograph inherited from vita-moonlight.
- Own title ID `VMOON0001` and data folder `ux0:data/vitamoon`. On its first start VitaMoon moves
  Lune's folder (`ux0:data/lune`) to its own, with the paired PCs, settings, statistics and logs; the
  old Lune bubble can be deleted. VPKs are named `vitamoon-<version>.vpk`.

### Added

- Automatic reconnection: a stream lost to a Wi-Fi drop or to a sleep of the Vita starts again on its
  own, on the same game, which kept running on the PC. VitaMoon waits for the Wi-Fi, tries every 3 s for
  a minute and can be cancelled; it stops after three streams in a row lost within 20 s. Option
  *Settings > General > Reconnect automatically*, on by default (`src/reconnect.h`).
- In-game quick menu, L + R + SELECT during a stream: back to the game, statistics on screen, keyboard,
  pause (VitaMoon's menus), quit the game (asked twice), with the Wi-Fi, battery and clock of the Vita.
  It is drawn over the live picture; the game keeps running and receives no button until the menu is
  closed and every button released (`src/gui/quickmenu.c`, state machine tested on a PC).
- Five colour themes for the menus, *Settings > General > Theme*: Night (the former look), Dawn,
  Aurora, Twilight, Eclipse; each with its own background. The idea of a few presets comes from
  VitaRPS5; the palettes are VitaMoon's own (`src/gui/theme.c`, `tools/make_background.py`).
- A log on every run, `ux0:data/vitamoon/vitamoon.log` (the four previous runs kept as
  `vitamoon.1.log` to `vitamoon.4.log`): start-up and device state, each PC and its Sunshine version, each stream (settings,
  timed start stages, video, audio and controller set-up, how and when it ended), reconnections, quick
  menu, errors, and one health line per minute of play. Lines are written by a low-priority thread
  from a memory ring, so no stream thread waits for the memory card; repeated lines are folded and
  floods bounded (`src/log.c`, pure part tested on a PC). VitaRPS5 also writes its log from a queue.
- Starting a game while another runs on the PC now offers to quit that one and start the new one,
  instead of asking to quit it first (NowaLone/vita-moonlight).
- Keyboard layout *Text, any PC layout*, the default for new settings: what is typed on the Vita's
  keyboard is sent as characters (UTF-8), so it arrives right whatever the PC's keyboard layout (a US
  key position on a French AZERTY PC gave another letter). The PC layouts stay for games that read
  keys (NowaLone/vita-moonlight sends text too).

### Changed

- *Save a debug log* becomes *Detailed log*: it adds the detail lines to the log, which is always
  written, and no longer slows the stream (the old one wrote each line to the card from the thread
  that logged it).
- *Let the PC adjust game settings* becomes *Let the PC adapt its screen*: with Sunshine, this option
  ("Optimize game settings" in Moonlight) is what lets the PC switch its screen to the stream's
  resolution; the old help said it only mattered for GeForce Experience.
- HTTP requests are logged without their query, which holds the client's identifiers and the stream key.

## 1.0.0-beta.1 "Nouvelle Lune" (2026-11-15)

First version of Lune, from [xyzz/vita-moonlight](https://github.com/xyzz/vita-moonlight) 0.13.2
(commit `984603b`, kept as the branch `legacy/vita-moonlight-0.13.2`).
Built and tested on a PC; not yet measured on a Vita
([checkpoint report](docs/checkpoints/2026-11-15.md)).

### Identity

- Named Lune, with its own title ID (`LUNE00001`), data folder (`ux0:data/lune`) and LiveArea icon:
  it installs next to Moonlight instead of replacing it. PCs must be paired again.

### Streaming

- Video and audio decoded on moonlight-common-c's decoder threads: the network threads no longer wait
  for the decoder, the screen or the audio output (dubleyu/vita-moonlight-relay).
- moonlight-common-c updated to upstream e2722dc and copied into the repository with three patches:
  wait for late or reordered video packets (juanjsebgarcia, *Wait for late packets*, 20 ms), its
  setting, and Vita hooks (thread priority and core, 512-packet receive queue, FEC and out-of-order
  counters). NEON error correction (nanors).
- Display modes: *Smooth* (one frame per refresh, drawn just in time), *Paced* (at the PC's capture
  cadence with an adaptive anchor), *Lowest latency* (newest frame at once), *Direct* (original);
  profiles *Smooth*, *Paced*, *Lowest latency*, *Original*.
- *Fast colour decoding*: YUV output converted by the GPU (juanjsebgarcia: 4.6 → 2.4 ms per frame).
- Decoder catch-up: takes back the pictures the hardware decoder keeps, each a frame of delay
  (method from VitaRPS5).
- Low-latency sound with an adaptive buffer; classic sound with alternating blocks and a 60 ms
  catch-up after stalls.
- Streaming threads at the highest priority and spread over the three cores (*Spread work over the
  cores*, dubleyu/vita-moonlight-relay); input, motion, statistics and cover downloads as pthreads.
- Controls read once per controller sample (*Read the controls in step with the Vita*, juanjsebgarcia).
- Clocks raised while connecting and streaming, with the Wi-Fi in performance mode; the PC asked to
  capture at the screen's exact rate (59.94 Hz).
- Video packets of 1024 bytes by default (juanjsebgarcia's measurement), 1200 and 1392 available; the
  setting is now read back (it never was).
- 8 MiB network memory pool (1 MiB before).
- Release build optimised (-O2): everything, the streaming library included, was built at -O0.

### Interface

- New theme (Roboto, ocean background), home screen with the state of each PC and its details,
  game library with cover art and the last session's figures, two-pane settings with an explanation
  for every option, pairing screen with the code and the address of Sunshine's page.
- English, French and Spanish (*Settings > General > Language*).
- Warnings on the home screen when Wi-Fi power saving or Bluetooth is on.
- Resuming after a change to a setting that needs a new stream offers to restart it; the game keeps
  running on the PC.
- The Vita pairs under its own nickname instead of "roth".

### Statistics

- Overlay during the stream; per session a CSV (one line per second, about 60 columns) and a summary
  with the settings, the Wi-Fi state and percentiles over the session.

### Fixes

- The menus and the decoder thread could draw at the same time at a pause in direct display mode.
- The end of a stream could be handled twice at once (PC-side end and menus).
- Quitting the game from the pause dropped back to the home screen.
- A 5th or 6th finger on the front touch screen wrote past an array.
- A failed SPS rewrite corrupted the decoder's input; the SPS now passes through unchanged.
- A pause or the end of a stream now releases buttons, sticks, mouse buttons and touches held on the
  PC (pointer fix from dubleyu/vita-moonlight-relay); clocks are restored when a paused stream ends.
- Pairing errors reported as 1 instead of their code; the audio port leaked once per stream; a
  double `fclose` at start-up when the data folder falls back; unbounded `sscanf` in mapping files;
  HTTP requests without a time limit; settings files with out-of-range values (now reset).

### Project

- Code, comments and logs in English; documentation in `docs/`, French user guide.
- Removed code that nothing called: the customisable hotkeys module of 0.13.2 (its menu was never
  reachable and its shortcuts were never checked during a stream), the old LiveArea template.
- PC tests (pacing simulations, settings file, SPS rewrite, helpers) with AddressSanitizer and UBSan,
  run by CI; interface preview rendering every screen in three languages.
- CI builds every branch, runs the tests and publishes a VPK with a fixed link.

## Before this project

The history of the Vita port up to 0.13.2 is in
[xyzz/vita-moonlight's changelog](https://github.com/xyzz/vita-moonlight/blob/vita/CHANGELOG.md).
