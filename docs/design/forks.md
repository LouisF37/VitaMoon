# Other Vita Moonlight forks: what was taken

Surveyed in November 2026 from their public repositories. GitHub's list of forks could not be read
from the tools used, so forks were found by search: there may be others.

## juanjsebgarcia/vita-moonlight

Measured performance work on a Vita (Intel Arc B580 host): a benchmark mode, per-change
measurements, a modified moonlight-common-c with a reorder window.

| Item | Taken | Notes |
|---|---|---|
| Reorder window (moonlight-common-c branch `reorder-window`) | yes | patches 0001-0002, default 20 ms |
| YUV decoding converted by the GPU | yes | *Fast colour decoding* |
| Packet size measurements (1024 better than 1392) | yes | default 1024 |
| Clocks raised while connecting | yes | `vitapower_busy_begin()` |
| Clocks put back when a paused stream ends | yes | `connection.c` |
| Controls read in step with the controller | yes | option, on |
| Bluetooth warning | yes | home screen |
| Pairing under the Vita's nickname | yes | instead of "roth" |
| Restart offer when a setting needs a new stream | yes | from the pause |
| OpenSSL 1.0.2 build | no | a workaround for his SDK's libcurl, not an optimisation |
| Benchmark mode and PC scripts (`tools/bench`) | not yet | heavy; our statistics files cover part of it |
| Recommended settings with a 10 s automatic test | not yet | to decide once our own measurements exist |
| Battery presets | not yet | his figures: 426 mA balanced, 312 mA battery preset |

## dubleyu/vita-moonlight-relay

Architecture rework tested on a Vita, plus a relay mode for play over the internet.

| Item | Taken | Notes |
|---|---|---|
| Receive, decode, present and audio decoupled | yes | decoding on moonlight-common-c's threads |
| Timestamp-paced presentation (12 ms buffer) | yes, reworked | *Paced* display with an adaptive anchor (`src/video/pacing.c`) |
| 4-frame decoded queue | yes | in *Paced* mode |
| Slow Opus decoder capability, 60 ms audio catch-up | yes | classic audio mode |
| moonlight-common-c update and NEON FEC | yes | newer upstream commit (e2722dc) |
| CPU cores per role, pthreads for the workers | yes | `src/thread_policy.c` |
| Clock and wireless boost | yes | |
| 512-packet receive buffer | yes | patch 0003 |
| No clear when the picture covers the screen | yes | |
| End-of-session percentiles | yes | `.txt` summary |
| Pointer released on pause, stop and touch zones | yes | commit 3f9fbb8 |
| -O3 build | no | -O2 kept (juanjsebgarcia also uses -O2); no measurement either way |
| Defaults 30 fps, 4 Mbps, 1200-byte packets | no | not measured; 30 fps and lower bitrates stay available |
| Relay mode (TLS over TCP through a PC companion) | no | for play outside the home network |
| Relative trackpad, four rear-touch zones, START + Right display switching | not yet | desktop use |
| Fixed 64 KiB log buffer | yes, reworked | 64 KiB ring written by a low-priority thread, always on (`src/log.c`) |

## alex4580/vita-moonlight

An all-in-one Windows host package (Sunshine set-up, a 960x544 virtual display, display recovery) and
first-run defaults.

| Item | Taken | Notes |
|---|---|---|
| Settings sanitising after loading | yes | `config_sanitize()` |
| Defaults 960x544, 60 fps, 1024-byte packets | already the same | his bitrate 10 Mbps, ours 8 Mbps at 960x544 |
| Windows host package and virtual display | no | host side; a 960x544 virtual display is worth considering on the PC |

## NowaLone/vita-moonlight ("Vita Moonlight PAF")

A new interface built with Sony's PAF framework (the system applications' look), on the same
streaming code.

| Item | Taken | Notes |
|---|---|---|
| SPS and decode buffer bounds audit | yes, adapted | SPS passed through unchanged when it cannot be rewritten |
| Settings validated against their choices | yes | with alex4580's sanitising |
| Unsafe PC folder names refused | partly | renaming to "." or ".." refused; other separators already were |
| PAF interface | no | a different interface technology |
| Offer to quit the running game and launch another | yes | library: confirm, then quit and start |
| Text typed with the system keyboard sent as UTF-8 | yes | keyboard layout *Text, any PC layout*, the default for new settings |
| Motion units corrected (rad/s to deg/s, g to m/s²) | not yet | our scaling and axes are the original ones: to check on a Vita before changing |

## Others

- **GrapheneCt/vita-moonlight**: its 2023-2024 release notes list decoding stopped while the app is in
  the background (battery) and a Bluetooth keyboard; that source is not in the public repository.
  Not taken.
- **MetalfaceScout / AorsiniYT vita-moonlight-motion**: motion controls, already in xyzz 0.13.2.
- **VitaRPS5** (PS5 Remote Play, not Moonlight): see below.

## VitaRPS5 (mauricio-gg/vitaki-vitarps5)

A PS5 Remote Play client for the Vita (Chiaki's protocol, not Moonlight's), compared on 16 November
2026 at commit e0a2fe8. Its protocol, PSN sign-in and microphone have no Moonlight counterpart; the
rest was read for what a Vita client needs whatever the protocol.

| Item | In VitaMoon | Notes |
|---|---|---|
| Decoder catch-up, input thread priority, output buffer rotation | yes | taken earlier |
| 8 MiB network memory pool (4 MiB ran out under load) | yes | `src/main.c` |
| Reconnection after a lost stream, with an overlay | yes | `src/reconnect.h`, `ui_connect.c` |
| Wi-Fi power saving and Bluetooth warnings | yes | home screen |
| Colour themes | yes, own palettes | five themes |
| Log written from a queue by its own thread, per-run files, drop counters | yes, reworked | a byte ring with no allocation per line, levels, token buckets, repeat folding, the four previous runs kept |
| Time per stream stage, settings and device state in the log | yes | `stream` and `system` lines |
| Delay probes: decoder hold, receive backlog, audio gaps, presentation order | mostly | VitaMoon's per-second CSV has decoder hold, receive and queue times, audio buffer and gaps, late and repeated frames |
| Button-press-to-picture meter (`PIPE/PRESS`: matches a press to the next big frame) | not yet | worth adding: the only end-to-end measure of the delay a player feels; needs care (quiet screen, ambiguous matches), as its own documentation shows |
| Microphone, PSN, console wake-up over the Internet | no | not in the Moonlight protocol, or done by Sunshine |
| Latency presets that change several settings at once | yes | *Profile* (Smooth, Paced, Lowest latency, Original) |
