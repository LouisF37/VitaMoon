# Streaming

From the PC's packets to the Vita's screen and speakers. The source files are named in each section.

## Network: moonlight-common-c and its patches

`third_party/moonlight-common-c` is a copy of the upstream library (commit e2722dc, October 2026) with
three patches, listed in [third_party/patches/README.md](../../third_party/patches/README.md):

1. **Reorder window** (juanjsebgarcia): on Wi-Fi about 10 % of video packets arrive out of order on a
   Vita. Upstream gives a frame up as soon as a packet of the next frame arrives; the patch holds the
   next frame's packets for a short time (setting *Wait for late packets*, default 20 ms) so that the
   late ones can complete the current frame. Without it, each abandoned frame freezes the picture
   until the next key frame: about 13 frames on juanjsebgarcia's host, whose encoder (Sunshine with
   Intel Arc QSV, H.264) offers no reference frame invalidation to shorten that wait.
2. `LiSetVideoReorderWaitUs()` to set or turn off that wait.
3. **Vita hooks**: a callback on every thread start (priority and core), a 512-packet receive queue
   (the Vita grants 512 KB of socket buffer whatever is asked), and counters for out-of-order packets
   and frames repaired or lost by FEC, shown in the statistics.

The FEC is [nanors](https://github.com/sleepybishop/nanors), whose ARM NEON path the Vita toolchain
selects. The upstream update also stops reporting losses speculatively when reference frame
invalidation is off, as it is by default in this app.

## Video: decoding

`src/video/vita.c`, `vita_submit_decode_unit()`, on moonlight-common-c's `VideoDec` thread (no
`CAPABILITY_DIRECT_SUBMIT`): the network threads only read and repair, they never wait for the decoder
or the screen. dubleyu/vita-moonlight-relay found this separation to be the change that mattered most
on a home network.

1. The frame's buffers are concatenated; the SPS goes through `gs_sps_fix()` (`libgamestream/sps.c`),
   which sets the low-latency hints (one reference frame, no reordering, a single buffered frame).
   An SPS that cannot be parsed or rewritten is passed through unchanged.
2. `sceAvcdecDecode()` writes the picture straight into a texture: RGBA, or with *Fast colour decoding*
   NV21 into a `YVU420P2_CSC0` texture that the GPU converts while drawing (BT.601 limited range, which
   the app asks the PC for).
3. **Decoder catch-up** (method from VitaRPS5): the hardware decoder returns at most one picture per
   call, so a call that returns none leaves a picture inside for good, one frame of delay each time.
   When pictures are held back, an extra call with an empty access unit takes one out.
4. The picture is handed to the display thread through a pool of textures ("slots": 3, 4 in *Paced*)
   so that the decoder never writes into the texture being drawn.

## Video: display modes

Set by *Settings > Performance > Profile* or *Display*. Buffer flips always wait for the vertical
blank (vita2d's `NEXTFRAME`), so no mode tears.

| Mode | Who draws | Which frame | When |
|---|---|---|---|
| Smooth (default) | `vid_display` | the oldest waiting, one per refresh; a lasting backlog is trimmed | just in time: the thread sleeps after each blank until only the drawing budget is left |
| Paced | `vid_display` | the newest frame that is due | at the PC's capture cadence, plus a margin (below) |
| Lowest latency | `vid_display` | the newest, at once (older ones dropped) | as soon as it is decoded, at most two draws per refresh |
| Direct (*Original*) | `VideoDec` | each one, drawn after decoding | immediately; the original frame pacer may drop some |

**Just-in-time drawing** (*Smooth*, *Paced*): vita2d waits for the next refresh to show a flip, a
median 6.5 ms measured by juanjsebgarcia, during which a newer frame may arrive and the one drawn is
replaced (18 % of frames in his measurement). Picking the frame late in the refresh, with the
drawing budget measured on the fly (3.5 to 12 ms), shows the newest frame at the earliest flip.

## Video: the Paced display

`src/video/pacing.c`, tested by `tools/tests/test_pacing.c`. Idea from dubleyu/vita-moonlight-relay
(fixed 12 ms buffer); the adaptive part is this project's.

- A frame is due at an anchor on the Vita clock plus the time between its capture and the anchor
  frame's capture on the PC (moonlight-common-c's presentation time). The display thread shows, at each
  refresh, the newest frame due by the next flip.
- A frame must be ready its drawing budget before it is due, otherwise it counts as late.
- Every 2 s, the slowest frame of the window decides: late, the anchor moves later (back to the
  margin); waiting more than the margin plus 2 ms, it moves earlier; in between it stays, so that due
  times do not slide back and forth across a refresh. At most 4 ms per window, which also follows a PC
  clock drifting from the Vita's.
- The margin starts at 12 ms, grows by 4 ms after a window with two or more late frames and shrinks by
  2 ms after five without, between 4 and 33 ms.
- A large jump of the PC's clock (pause, stall, new stream) re-anchors on the frame at hand; when a
  waiting frame had to be overwritten, the newest is shown at once and becomes the anchor.

What the simulations show (60 s to 120 s streams, 59.94 Hz screen): an even cadence with up to 15 ms
of jitter (no repeated frame, about one frame in a thousand dropped, the PC sending 60.00 fps to a
59.94 Hz screen), a bounded wait with a PC clock 200 ppm or 0.1 % off either way, the first frame
after a 3 s pause shown within 3 refreshes. The cost: the margin adds a few milliseconds over
*Smooth*, and with a 59.94 fps stream it can add a whole refresh depending on the phase.

## Audio

`src/audio/vita.c`, on moonlight-common-c's `AudioDec` thread (`CAPABILITY_SLOW_OPUS_DECODER`).

- **Classic** (original): 20 ms blocks to a blocking port, two blocks in turn so that a block is never
  rewritten while it plays. After a stall, audio queued beyond 60 ms is decoded but not played
  (dubleyu/vita-moonlight-relay), so the sound does not stay behind the picture.
- **Low latency** (default): decoded audio goes into a ring buffer, and a dedicated thread feeds a
  5.33 ms port. The buffer is adaptive: playback starts once a cushion is there (10 ms to begin with),
  each gap raises the cushion by 5 ms (up to 50 ms), 30 s without a gap lowers it by 1 ms, and delay that
  builds up is trimmed with a short cross-fade.
- A lost packet is replaced by Opus concealment in both modes. Only stereo is supported.

## Input

`src/input/vita.c`: the input thread reads the controller once per sample (*Read the controls in step
with the Vita*, juanjsebgarcia) instead of every 2 ms, then touch, and sends what changed. A pause or
the end of a stream releases on the PC every button, stick, mouse button and touch still held
(pointer fix from dubleyu/vita-moonlight-relay).

## Statistics

`src/video/stream_stats.c`. Hooks on every stage update lock-free counters; a thread writes one CSV
line per second and the overlay's one line (frames shown per second, receive to screen, bitrate). The `.txt` of each session holds the settings, the Wi-Fi state,
the column legend and, at the end, percentiles over the whole session (receive to screen, interval
between shown frames, hardware decode) and the share of frames shown late or repeated.
