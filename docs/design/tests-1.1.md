# Tests of the 1.1 betas

VitaMoon 1.1.0-beta.1 adds options to measure, not to keep: each is set to 1.0's behaviour by default,
grouped in *Settings > Tests (beta)*. Sessions with and without each one decide what a later version
keeps as a default; the category then goes away. 1.5.0 was released before these sessions: it keeps
the category, every option still off by default. One change at a time, five minutes at the same place in the
same game, and the `stats` files and the log of each session.

| Option | What it changes | Where to look |
|---|---|---|
| Decoder delay mode | Off, Low, Mode 1, Mode 2, High | log `decoder delay mode`, CSV `hold_avg_x100`, `r2p_avg_us` |
| Decoder reference frames | 4 (1.0), 2, 1 | log `reference frames`, CSV `dec_err`, the picture |
| Skip late frames | frames over 100 ms late are decoded, not shown | CSV `late_hidden`, log `late frames` |
| Show frames at once | Low latency display: no wait for the refresh | CSV `r2p_avg_us`, tearing on screen |
| 5 ms sound packets | the PC sends 5 ms of sound per packet instead of 10 | CSV `aud_underrun`, `aud_wait_max_us`, log `sound gap` |

The quick menu's *Measure the delay* (below) gives the delay a player feels, press to picture, which
the statistics cannot: it is the measure that compares these options best.

## Decoder delay mode

The Vita's hardware decoder keeps 3 to 4 pictures before handing one back (`hold_avg_x100` about 360
in the 1.0.0 sessions): about 60 ms at 60 frames a second, and about 170 ms on a still screen, when the
PC sends some 20 frames a second. `sceAvcdecSetLowDelayModeNongameapp`, called before the decoder
library starts, sets a delay mode: the original Moonlight Vita 1.0 calls it with the lowest mode (0)
for every stream, with 4 reference frames (`moonlight/src/video/legacy/modules/vita_init.cpp`), and
its FFmpeg port offers the four values 0, 2, 4 and 6 (`VITA_AVCDEC_DELAY_MODE_LOW`, `_1`, `_2`,
`_HIGH`). VitaMoon offers the same four, and *Off*, which does not call the function.

- Imported from the library the original links it from (`SceVideodecLowDelayUser`, NID `0x6354EEFF`,
  `cmake/vita/SceVideodecLowDelay.yml`) as a **weak** import: VitaMoon starts even if a system lacks
  it, and only this setting calls it. The VitaSDK lists the same function NID in `SceVideodecUser`;
  the library of a released app was preferred.
- The return code is in the log (`decoder delay mode 0 set (0x00000000)`, or `refused`).
- Nothing says how to put the decoder back to its default: once a mode was used, *Off* applies after
  VitaMoon restarts (the setting shows it).
- Expected if it works: `hold_avg_x100` well below 360, and `r2p_avg_us` lower by up to 50 ms.

## Decoder reference frames

The decoder is created for 4 reference frames, while the PC's encoder declares how many it uses in
each stream header (the log says it once per stream: `the PC's encoder declares 1 reference frame`).
Fewer reference frames means less decoder memory and possibly less hold. If the decoder refuses the
number asked, it is made again with 4 (log). With reference frame invalidation on (NVIDIA PCs), 4 are
kept. If the encoder declares more than the decoder was made for, the log warns: the picture can then
break up.

## Skip late frames

After a Wi-Fi freeze, the frames held back arrive together; 1.0 shows them, a fast-forward of the past.
With this option, a frame whose first packet arrived more than 100 ms later than the network's usual
delay is decoded (the next frames are built from it) but not shown: the picture stays still, then jumps
to the present. The reference is the fastest frame of the last 10 to 20 seconds, so that the slow drift
between the PC's clock and the Vita's never counts; if frames stay late for a whole second, the delay
has changed for good and becomes the new reference (`src/video/late_frames.h`, tested on a PC with
simulated freezes, drift and a lasting delay). The decoder's own hold is not counted: a still scene,
which the decoder holds longer, is never hidden.

The original Moonlight Vita had such a check from August to September 2026 (commit `0e9d823`, removed
by `f064cc5`): there, a late frame was dropped undecoded and a key frame requested, and the reference
was the fastest frame of the whole stream.

## Show frames at once

For the *Low latency* display only (*Performance > Display*). The frame just drawn goes on screen at
once (`sceDisplaySetFrameBuf(..., SCE_DISPLAY_SETBUF_IMMEDIATE)`), where the screen is in its refresh,
instead of at the next vertical blank: 0 to 16.7 ms sooner, 8 on average, and a frame can show cut in
two (tearing). The statistics count such a frame as visible when it is flipped. The original's
Borealis interface has the same option; its default went back to the next-frame flip in September
2026 (`da1a0d5`, *prefer next-frame presentation*).

## 5 ms sound packets

moonlight-common-c asks the PC for 10 ms sound packets when the renderer declares a slow Opus decoder
(`CAPABILITY_SLOW_OPUS_DECODER`, which VitaMoon has always declared) and 5 ms ones otherwise. This
option removes the declaration: up to 5 ms less sound delay, twice as many sound packets on the
Wi-Fi. Decoding stays on moonlight-common-c's audio thread either way.

To try after reading the sound gap lines of a few sessions with 10 ms packets: each gap (the sound
ran out) now writes a log line with how long ago the last packet came, how many were waiting and how
many were lost. A gap long after the last packet points to the Wi-Fi or the PC; a gap with a recent
packet points to the Vita's side. The CSV column `aud_wait_max_us` is the longest wait between two
packets in each second.

## Measure the delay (press to picture)

1. Copy [tools/latency/latency.html](../../tools/latency/latency.html) to the PC, for example to
   `C:\VitaMoon\latency.html`.
2. Show it full screen on the PC. Simplest: a Sunshine application *VitaMoon delay test* whose command
   is
   `"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe" --kiosk "file:///C:/VitaMoon/latency.html" --edge-kiosk-type=fullscreen --no-first-run --user-data-dir=C:\VitaMoon\edge-profile`
   (Microsoft Edge's full-screen kiosk mode; the separate profile makes Edge open its own window,
   which Sunshine closes when the game is quit from the Vita).
3. Start that application from VitaMoon, open the quick menu (L + R + SELECT) and choose *Measure the
   delay (test page)*. Do not touch the controls for about 25 seconds.

VitaMoon presses A on its own (2 presses to warm up, then 20 measured), at random moments, and watches
the centre of each decoded picture: the page turns white while A is held. The result shows over the
picture and goes to `stats/latency-<date>.txt`, with each press and its parts:

- press sent → first packet of the white picture: the trip to the PC, the page's reaction (the browser
  draws at the PC screen's next refresh), the capture, the encoding (reported apart) and the trip back;
- first packet → picture out of the decoder: reception, decoding and the decoder's hold;
- decoder → screen: waiting for the display.

Not included: a real press waits 0 to 17 ms (8 on average) for the Vita to read its buttons; "on
screen" is the refresh that shows the picture, whose centre the screen draws about 8 ms after its top.
The page keeps a small bar moving so that the PC sends pictures at its full rate, as in a game.

The test stops by itself if the page is not on screen (no black picture), if it does not react (both
warm-up presses, or 5 presses in all, without a white picture) or if it stays white; from the quick
menu, the same row stops it. Started over a game by mistake, the game receives those first two A
presses.
Its logic is simulated on a PC (`tools/tests/test_latency.c`).
