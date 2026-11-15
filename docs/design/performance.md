# Performance: what changed, where it comes from, what is measured

Every change made for speed or smoothness, with its source and the evidence behind it. "Measured"
means measured on a Vita by the source named; **none of these changes has yet been measured in this
project** (see the [checkpoint report](../checkpoints/2026-11-15.md) for the test plan).

Sources:
[juan](https://github.com/juanjsebgarcia/vita-moonlight) = juanjsebgarcia/vita-moonlight
(`docs/DECODE.md`, `docs/FORK-CHANGES.md`, measured on a Vita with an Intel Arc B580 host);
[dubleyu](https://github.com/dubleyu/vita-moonlight-relay) = dubleyu/vita-moonlight-relay
(`PERFORMANCE_OVERHAUL.md`, tested on a Vita, few figures published);
[VitaRPS5](https://github.com/mauricio-gg/vitaki-vitarps5).

## Network and decoding

| Change | Setting (default) | Source | Evidence |
|---|---|---|---|
| Decode video and audio off the network threads | always | dubleyu, juan | dubleyu: the largest visible gain on a home network, same Vita and access point (no figure) |
| Wait for late or reordered packets (reorder window) | *Wait for late packets* (20 ms) | juan | at ~60 % Wi-Fi signal: picture freezes 23-37 per minute → 1-3 (20 ms), 2-8 (10 ms), same latency; ~10 % of packets arrive out of order |
| moonlight-common-c updated (e2722dc), FEC with NEON | always | dubleyu (update), upstream | upstream fix: no speculative loss reports when reference frame invalidation is off |
| 512-packet receive queue | always | dubleyu | juan: the Vita grants 512 KB of socket buffer whatever is asked |
| YUV output converted by the GPU | *Fast colour decoding* (on) | juan | hardware decode 4.6 → 2.4 ms per frame at 540p (2.9 ms at 720p), colours checked by pixel readback |
| Video packets of 1024 bytes | *Video packet size* (1024; 1200, 1392) | juan | 1392 bytes: no receive gain (2.37 vs 2.41 ms), more losses (59 vs 43 per minute) |
| Decoder catch-up | *Decoder catch-up* (on) | VitaRPS5 (issue 413) | VitaRPS5: each picture held back by the decoder is a frame of lasting delay |
| SPS low-latency hints, passed through unchanged on failure | always | Moonlight Embedded; fallback: this project after NowaLone's bounds audit | — |

## Display

| Change | Setting (default) | Source | Evidence |
|---|---|---|---|
| Just-in-time drawing, one frame per refresh | *Display: Smooth* | this project | juan: vita2d's wait for the refresh is a median 6.5 ms and 18 % of drawn frames were never shown |
| Paced display at the PC's capture cadence | *Display: Paced* | dubleyu (12 ms fixed); adaptive anchor: this project | PC simulations (`tools/tests/test_pacing.c`) |
| Newest frame at once | *Display: Lowest latency* | this project | — |
| No full-screen clear when the picture covers the screen | always | dubleyu | one full-screen write less per frame (no figure) |
| 4x multisampling of the interface and stream | *Smooth edges* (on) | this project | cost on the stream not measured: compare `render_avg_us` |

## Audio

| Change | Setting (default) | Source | Evidence |
|---|---|---|---|
| Low-latency output with an adaptive buffer | *Low-latency sound* (on) | this project; output blocks used in rotation as in VitaRPS5 | — |
| Classic mode: two alternating blocks, 60 ms catch-up | with *Low-latency sound* off | dubleyu (catch-up) | — |

## System

| Change | Setting (default) | Source | Evidence |
|---|---|---|---|
| Optimised build (-O2; everything was built at -O0 before) | always | juan (same fix, -O2); dubleyu builds at -O3 | — |
| Streaming threads at priority 64, network on core 1, decoding on core 2, display and input on core 0 | *Spread work over the cores* (on) | dubleyu | no figure |
| Pthreads for input, motion, statistics and cover art | always | dubleyu | crashes in `sem_timedwait` with kernel threads |
| Clocks 444/222/222/166 MHz while connecting and streaming, Wi-Fi in performance mode | *Maximum CPU and GPU speed* (on) | dubleyu, juan (while connecting), VitaRPS5 | juan: battery 426 mA in his balanced preset vs 312 mA in his battery preset (about 5 h vs 7 h) |
| Controls read once per controller sample | *Read the controls in step with the Vita* (on) | juan | no figure |
| Ask the PC to capture at the screen's exact rate (59.94 Hz) | *Match the Vita screen rate* (on) | this project | at 60.00 fps one frame in about 1000 (every ~17 s) cannot be shown; applies if Sunshine accepts the rate |
| Warn when Wi-Fi power saving or Bluetooth is on | home screen | this project, juan | juan: frame loss ~50 per minute → 0 with a strong signal and Bluetooth off |

## Where the time goes (juan's measurements)

- Vita side, per frame: receive about 2.4 ms at 10 Mbps (1.1 ms at 5 Mbps), decode 2.4 ms (YUV),
  draw 0.2 ms, plus the wait for the refresh.
- End to end, pointer to screen: about 73 ms, of which about 60 ms on the PC (capture, encode, send).
- Bitrate costs about 0.4 ms per Mbps near the access point, more with a weaker signal; on the Vita's
  screen 540p and 720p look alike at the same bitrate and quality stops improving around 15-20 Mbps.
- A stream at 50 fps on a 60 Hz desktop costs about 30 ms.

## Measuring on your own setup

Turn on *Settings > General > Statistics while streaming*. Each stream writes
`stats/session-<date>.csv` (one line per second) and `.txt` (settings, Wi-Fi, legend, percentiles over
the session). Compare profiles on the same game and spot with the `.txt` percentiles: `r2p` (first
packet received to frame visible), the interval between shown frames and the share of frames shown
late or repeated. The test plan is in the [checkpoint report](../checkpoints/2026-11-15.md).
