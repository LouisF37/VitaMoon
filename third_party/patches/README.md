# Patches applied to vendored libraries

## moonlight-common-c

`third_party/moonlight-common-c` is a copy of
https://github.com/moonlight-stream/moonlight-common-c at commit e2722dc
(12 Oct 2026), with its `nanors` submodule (https://github.com/sleepybishop/nanors,
commit b1e3c22; only the files the build uses) copied in place, and the patches
below applied in order.
It was a submodule before; it is copied so that the build does not depend on a
patch step.

| Patch | Origin | What it does |
|---|---|---|
| 0001 | juanjsebgarcia/moonlight-common-c, branch reorder-window | Holds the next frame's packets briefly while the current frame waits for late packets, instead of dropping the current frame at once. |
| 0002 | juanjsebgarcia/moonlight-common-c, branch reorder-window | Adds `LiSetVideoReorderWaitUs()` to set or turn off that wait. |
| 0003 | this fork | Vita hooks: `LiVitaThreadStarted()` called by each new thread (priority and core, see `src/thread_policy.c`), a 512-packet video receive queue with the granted socket buffer logged, and counters for out-of-order packets and frames repaired or lost by FEC. |

To move to a newer upstream version: copy the new sources over, copy nanors
again, and apply the three patches with `git apply`.

The ENet submodule (`third_party/enet`) stays at dea6fb5: the next commit
(aca8784) does not build for the Vita (`in6addr_any` is not declared).
