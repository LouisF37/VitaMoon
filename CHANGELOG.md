# Changelog

All notable changes of this project. Versions follow [Semantic Versioning](https://semver.org);
milestones are named after the phases of the moon ([docs/RELEASING.md](docs/RELEASING.md)).

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
