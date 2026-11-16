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
| `test_config.c` | every setting survives a save and a load; old keys ignored, out-of-range values reset; automatic bitrate; a file that cannot be written is logged and the app goes on |
| `test_sps.c` | the SPS rewrite sets the low-latency hints, stays within the room the renderer keeps, passes anything it cannot parse through unchanged |
| `test_misc.c` | URL encoding of the pairing name, percentiles of the session summary, the decisions of the automatic reconnection, PC names checked before they are used as folder names |
| `test_quickmenu.c` | the in-game quick menu: what opens it, moves in it, acts and closes it, when the game gets its buttons back |
| `test_log.c` | the log's ring (order, wrap-around, whole lines refused when full), its token buckets (burst, refill, report of the lines left out) and the folding of repeated lines |
| `test_ui.c` | which texts the title font can draw (Latin and its accents yes; Greek, Cyrillic, Japanese and malformed UTF-8 fall back to Inter), the picture of the waiting moon for each step and over the minute of reconnection, the interval between two pictures, every character of the translations among those rendered at start-up (`src/gui/prewarm.h`) |

## Interface preview (`tools/ui-preview`)

```sh
VITASDK=/usr/local/vitasdk tools/ui-preview/run.sh /tmp/screens
```

Compiles the real interface code against a fake vita2d drawing with cairo, plays scripted button
presses and writes every screen as a PNG in English, French and Spanish, a few screens in each colour
theme (`themes/`), then lists any text that does not fit its box. Needs gcc and the cairo and freetype development packages.
`PREVIEW_SANITIZE=1` builds it with AddressSanitizer and UBSan, which then checks the menus for
memory errors and leaks as well.

The fake vita2d draws at once, while the Vita's GPU reads textures only after a frame is submitted.
It therefore checks two rules of the console and stops with a message when one is broken: no wait
for the GPU while a frame is open, and no texture freed before the GPU has finished every frame that
drew it. The theme change in the `settings_change` scenario covers the case that froze beta.5.
It also counts what each frame takes from vita2d's vertex pool (1 MiB), with the sizes of vita2d's
own drawing calls: vita2d does not check that pool. More than half of it is reported, all of it
stops the preview; `PREVIEW_POOL_REPORT=1` prints the peak (9% on 16 November, the settings screen).

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
