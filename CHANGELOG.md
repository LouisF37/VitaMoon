# Changelog

All notable changes of this project. Versions follow [Semantic Versioning](https://semver.org);
milestones are named after the phases of the moon ([docs/RELEASING.md](docs/RELEASING.md)).

## Unreleased

## 1.0.0-beta.1 "New Moon" (2026-11-15)

First version of this project, from [xyzz/vita-moonlight](https://github.com/xyzz/vita-moonlight)
0.13.2. Built and tested on a PC; not yet measured on a Vita
([checkpoint report](docs/checkpoints/2026-11-15.md)).

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
- PC tests (pacing simulations, settings file, SPS rewrite, helpers) with AddressSanitizer and UBSan,
  run by CI; interface preview rendering every screen in three languages.
- CI builds every branch, runs the tests and publishes a VPK with a fixed link.

## Before this project

The history of the Vita port up to 0.13.2 is in
[xyzz/vita-moonlight's changelog](https://github.com/xyzz/vita-moonlight/blob/vita/CHANGELOG.md).
