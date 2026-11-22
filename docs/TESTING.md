# Testing

Three levels: PC tests of the logic, a rendering of every screen on a PC, and measurements on a Vita.

## PC tests (`tools/tests`)

```sh
VITASDK=/usr/local/vitasdk tools/tests/run.sh
```

Built with AddressSanitizer and UBSan (any memory error or undefined behaviour fails the run), and run
by CI on every push.

| Test | What it checks |
|---|---|
| `test_pacing.c` | the *Paced* display against simulated streams: steady, 0-15 ms jitter, PC clock 200 ppm and 0.1 % off both ways, a matched 59.94 fps stream, 30 fps, a 3 s pause; frames shown in order, drops and repeated frames, bounded wait |
| `test_late.c` | *Skip late frames* (`src/video/late_frames.h`): nothing hidden with 30 ms of jitter, clocks drifting 150 ppm either way or a still scene at 20 fps; after a 0.7 s and a 2.7 s freeze only the frames over 100 ms late are hidden; a delay grown by 300 ms for good is hidden for about a second, then taken as the new reference |
| `test_latency.c` | the press-to-picture test (`src/video/latency_logic.h`) against a simulated PC page, capture, decoder and screen: every press measured, the warm-up presses left out, the parts adding up, the mean equal to the simulated delay; the test stops when the page is absent, never reacts or stays white, and releases A when stopped |
| `test_config.c` | every setting survives a save and a load; old keys ignored, out-of-range values reset; automatic bitrate, Home and Away (the Away one three quarters of the Home one, the stream starting with the one of the connection in use); a file that cannot be written is logged and the app goes on |
| `test_route.c` | the Home and Away connections (`src/route.c`): IPv4 addresses, private ranges, names; the home and Internet addresses of a PC changed one without the other; the box's public address taken as the Internet address only when that one is empty or an IP (never a name typed by hand, never a private address); the state shown for each case at home and away; a PC saved by 1.5 with its public IP in both addresses, back home |
| `test_sps.c` | the SPS rewrite sets the low-latency hints, stays within the room the renderer keeps, passes anything it cannot parse through unchanged |
| `test_misc.c` | URL encoding of the pairing name, percentiles of the session summary, the decisions of the automatic reconnection, PC names checked before they are used as folder names, which statistics files are deleted (the last 30 streams are kept, nothing else in the folder is touched) |
| `test_quickmenu.c` | the in-game quick menu: what opens it, moves in it, acts and closes it, when the game gets its buttons back |
| `test_log.c` | the log's ring (order, wrap-around, whole lines refused when full), its token buckets (burst, refill, report of the lines left out), the folding of repeated lines, and its files (`log_files.h`): one per run named after its start, the oldest deleted beyond 30 runs, nothing else in the folder touched |
| `test_ui.c` | which texts the title font can draw (Latin and its accents yes; Greek, Cyrillic, Japanese and malformed UTF-8 fall back to Inter), the picture of the waiting moon for each step and over the minute of reconnection, the interval between two pictures, every character of the translations among those rendered at start-up (`src/gui/prewarm.h`) |
| `test_splash.c` | the start-up screen's timeline (`src/gui/splash_logic.h`): the moon never goes back nor skips its phases after a long step, the mark comes only once everything is loaded, the move ends exactly on the top bar's mark and name (`theme.h`), and the name never covers the mark on the way, for names narrower and wider than "VitaMoon" |
| `test_library.c` | the library (`src/gui/library_logic.h`): "last played" (today, yesterday, days ago, dates across a new year and a clock set back) and the order (favourites, last played, the PC's order); the twinkle (`twinkle.h`): bounded, smooth from frame to frame, each star at its own moment; the moving sky (`orbit.h`, `stars.h`): four speeds, both directions, no jump on a change or after a stream, never over the moon, as much light as the still picture at every moment, no star popping in or out at the fastest speed; the trackpad's tap (`src/input/tap.h`): click, swipe, the tap time |
| `test_home.c` | the home screen (`src/gui/home_logic.h`): the cursor between the side menu (six entries) and the previews' panels (right enters, left from the first column and O come back, X opens or acts, every panel's moves, the Settings cards with one over two columns, empty and changed panels), the file each PC's library leaves (written and read back, tabs and line breaks in names, Windows line ends, cut or foreign files, more lines than shown, a buffer too small), the PC the dashboard shows, the row under the large card (recent games, favourites, the rest) |
| `test_mdns.c` | the network search's mDNS reader (`third_party/mdnsniff`): a Sunshine answer is understood (name, address, port); a name pointing to itself, two names pointing to each other, every cut of an answer, a record longer than its packet and 20000 random or damaged packets neither hang nor crash it |

## Interface preview (`tools/ui-preview`)

```sh
VITASDK=/usr/local/vitasdk tools/ui-preview/run.sh /tmp/screens
```

Compiles the real interface code against a fake vita2d drawing with cairo, plays scripted button
presses and writes every screen as a PNG in English, French and Spanish, a few screens in each colour
theme (`themes/`), then lists any text that does not fit its box. Needs gcc and the cairo and freetype development packages.
`PREVIEW_SANITIZE=1` builds it with AddressSanitizer and UBSan, which then checks the menus for
memory errors and leaks as well.

The preview cannot download covers, so its games show their coloured tiles. `PREVIEW_COVERS=1` gives
five of them a downloaded cover instead, abstract pictures (two light, two dark, one bright) made in
`preview_main.c` and rounded by the same `cover_round_corners()` as the Vita's loader: `run.sh` renders
the home and library screens so in French (`covers/`), to check the selection frame on light and dark
covers.

The fake vita2d draws at once, while the Vita's GPU reads textures only after a frame is submitted.
It therefore checks two rules of the console and stops with a message when one is broken: no wait
for the GPU while a frame is open, and no texture freed before the GPU has finished every frame that
drew it. The theme change in the `settings_change` scenario covers the case that froze beta.5.
It also counts what each frame takes from vita2d's vertex pool (1 MiB), with the sizes of vita2d's
own drawing calls: vita2d does not check that pool. More than half of it is reported, all of it
stops the preview; `PREVIEW_POOL_REPORT=1` prints the peak (9% on 16 November, the settings screen).

The `splash` scenario plays the start-up screen (`src/gui/splash.c`) up to the home screen. Threads
run to their end as soon as they start here, so the steps of `main.c` take no time; each character
the start-up renders costs 0.5 ms of the virtual clock instead, about a Vita's pace. With
`PREVIEW_FRAMES=<folder>` every frame is written, numbered, to make a video of it:

```sh
PREVIEW_FRAMES=/tmp/frames /tmp/screens/preview splash fr /tmp/screens/fr
```

The `sky` scenario shows 12 s of the moving sky on the home screen (Slow, clockwise;
`PREVIEW_SKY_SPEED=1..4`, `PREVIEW_SKY_ANTICLOCKWISE=1`). `PREVIEW_TIME_SCALE=<n>` makes time run
n times faster, to see the stars' path in a short video (twinkling is then turned off).

### Journeys

The screens alone do not show whether a press does what it should: Play in the library started
nothing until beta.8, and every screen looked right. `run.sh` therefore also replays **journeys**
(`journeys.c`), from opening a PC (or, for the `j_home_*` ones, from the home screen) to the end of
a game, through the real menus, the real
`src/connection.c` and the real libgamestream (`client.c`, `xml.c`; only its pairing exchange is
replaced). Under them:

- `fake_http.c` answers libgamestream's requests as Sunshine 2026.914 does (its `src/nvhttp.cpp`):
  `serverinfo` over HTTP, and over HTTPS only for a paired client; `applist`; `launch` refused while
  a game runs and `resume` refused when none does, both inside a normal answer as Sunshine sends
  them; `cancel`; and no `/unpair` (404);
- `fake_pc.c` stands in for moonlight-common-c: a stream starts only after an accepted launch or
  resume, through the stages and callbacks of `LiStartConnection`, on the calling thread;
- `preview_sys.c` keeps a virtual clock (frames and sleeps both move it) and stops a menu that
  sleeps 15 minutes without drawing anything: a hang.

A journey gives the answers to the screens as they come (library, classic menu, settings,
dialogs), what other threads do meanwhile (quick menu, stream lost, stream ended by the PC, game
closed on the PC) and what the Vita must ask the PC, in order. Any other request, screen or dialog
fails it, as does a stream left open. 33 journeys: play; pause, resume, quit; quick menu quit;
stream lost and resumed, cancelled, given up, lost three times quickly, lost while the game was
closed; switch game, accepted or declined; resume a running game; quit it from the library;
classic menu launch, resume and disconnect; settings during a pause with and without the restart;
stream start failing, PC refusing, then working; back and reopen; PC ending the stream, with and
without the game; PC ending it or the Wi-Fi dropping during a pause; game closed on the PC while in
the library; pairing again, cancelled, or refused because a request is still pending; PC not
answering at home, the Away connection offered and declined or taken (no Internet address yet);
Away: opened at the Internet address only and played with the Away bitrate, a DynDNS name looked up
first, no answer over the Internet. The PC is opened at an address the log shows (`open
192.168.1.24:47989`) and a stream's bitrate is logged (`bitrate 8000`), checked where a journey
expects it. Failures are written in `report.txt`; `PREVIEW_JOURNEY_LOG=1` prints every log:

```sh
tools/ui-preview/build.sh /tmp/p && /tmp/p/preview journeys en /tmp/p   # the list
PREVIEW_JOURNEY_LOG=1 /tmp/p/preview j_pause_resume_quit en /tmp/p
```

With `PREVIEW_SANITIZE=1` the journeys are also checked for leaks: the libgamestream leaks fixed in
beta.9 were found this way.

## Static analysis

Used for the November 2026 audit, not run by CI:

```sh
cppcheck --enable=warning,style,performance,portability --std=c11 -D__vita__ \
  -I src -I libgamestream -I third_party/moonlight-common-c/src src libgamestream
cmake -S . -B /tmp/analyze -DVITA_OPT_LEVEL="-O1 -fanalyzer" && cmake --build /tmp/analyze
```

## On a Vita

The streaming behaviour can only be measured on the console. Each stream writes
`ux0:data/vitamoon/stats/session-<date>.csv` (one line per second) and `.txt` (settings, Wi-Fi,
column legend, percentiles over the session). The checkpoint reports in [checkpoints/](checkpoints/)
hold the test plan and the results.

The press-to-picture test (quick menu, *Measure the delay*, with `tools/latency/latency.html` full
screen on the PC) writes `stats/latency-<date>.txt`: the delay of 20 presses from the moment VitaMoon
sends them to the moment the answer is on screen, and its parts. The options measured during the 1.1
betas, and what to read for each, are in [design/tests-1.1.md](design/tests-1.1.md).
