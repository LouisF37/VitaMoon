# Contributing

Bug reports, measurements and pull requests are welcome.

## Reporting a problem

Open an [issue](https://github.com/LouisF37/VitaMoon/issues) with:

- the build shown at the bottom left of the home screen (`v1.0.0-beta.8  branch-commit`);
- the PC side: Sunshine version, GPU and encoder, resolution and frame rate of the stream;
- the `.csv` and `.txt` files of the session from `ux0:data/vitamoon/stats/`;
- for a crash, the dump `ux0:data/psp2core-*.psp2dmp` (or its decoding, `tools/dev/crash-dump.sh`).

Measurements comparing settings on the same game and spot are the most useful contribution: the
`.txt` percentiles make two sessions easy to compare.

## Code

- C (gnu99), two-space indentation, as the surrounding code.
- Code, comments, identifiers and log messages in English. Interface strings in English in the
  source (`N_("...")`, `tr("...")`), with their French and Spanish texts in `tools/i18n/table2.py`
  (`python3 tools/i18n/gen.py`, then `python3 tools/i18n/check.py missing`).
- Comments say why, and name the source of an idea or a measurement (fork, commit, document).
- A performance change states what it was measured against, or that it is not measured yet; add it
  to [docs/design/performance.md](docs/design/performance.md).
- Threads that call moonlight-common-c or the HTTP client are pthreads; streaming threads go
  through `thread_place()` (`src/thread_policy.c`).
- Logic that does not need the Vita belongs in a module the PC tests can build (like
  `src/video/pacing.c`), with a test in `tools/tests`.

Before a pull request: the VPK builds, `tools/tests/run.sh` passes, and for interface changes
`tools/ui-preview/run.sh` reports no text that does not fit, in the three languages.

## Commits

One logical change per commit, with a message that says what changed and why. Changes to
moonlight-common-c go into `third_party/patches/moonlight-common-c/` as a numbered patch, applied to
the copy in `third_party/moonlight-common-c`.
