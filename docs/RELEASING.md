# Releasing

## Identity and version

The package identity and the version are set at the top of `CMakeLists.txt`:

```cmake
set(TITLE_ID "VMOON0001")          # the app's own: installs next to Moonlight (XYZZ00002)
set(TITLE "VitaMoon")              # name under the LiveArea bubble
set(LIVEAREA_TITLE "VITAMOON")
set(LIVEAREA_SUBTITLE "GAME STREAMING\nFROM YOUR PC")
set(VERSION_MAJOR "1")
set(VERSION_MINOR "0")
set(VERSION_PATCH "0")
set(VERSION_LABEL "")               # empty for a final release, e.g. "beta.1" before one
set(VERSION_NAME "Nouvelle Lune")  # milestone name
```

From them the build derives the version shown on the home screen (`v1.0.0`, followed by the
branch and commit), the LiveArea text (generated from `sce_sys/livearea/contents/template.xml.in` into
the build folder) and the `APP_VER` of the VPK (`01.00`).

The name used by the code (top bar, logs, statistics) and the data folder (`ux0:data/vitamoon`) are in
`src/app_info.h`. Changing the title ID makes a different app for the Vita (a new bubble); changing
the data folder leaves the settings and pairings behind unless the old folder is moved, as
`src/check_dir.c` does for Lune's (`APP_OLD_DATA_DIR`). The logo and every image made from it (LiveArea
bubble, gate and background, the mark of the top bar, the README logo, the GitHub preview) are drawn
by `tools/art/make_brand.py`.

The app was named *Lune* (title ID `LUNE00001`, folder `ux0:data/lune`) for 1.0.0-beta.1, and
*VitaMoon* from 1.0.0-beta.2. The changelog of the eleven pre-releases of 1.0.0 (beta.1 to beta.11)
is in [history/betas.md](history/betas.md), that of 1.1.0-beta.1 to beta.4 (which led to 1.5.0) in
[CHANGELOG.md](../CHANGELOG.md).

Numbering follows [Semantic Versioning](https://semver.org): the patch number for fixes, the minor
for features, the major for changes that break settings or pairing. Milestones are named after the
phases of the moon, in French: *Nouvelle Lune* (1.0), then *Premier Croissant*, *Premier Quartier*,
*Gibbeuse Croissante*, *Pleine Lune*.

## Steps

1. Update `CHANGELOG.md`: move the *Unreleased* items under the new version and date.
2. Set the version in `CMakeLists.txt`.
3. Run the PC tests and the interface preview ([TESTING.md](TESTING.md)).
4. For a release that changes the stream, write a checkpoint report with Vita measurements in
   `docs/checkpoints/`.
5. Commit and push to `main` (the `RELEASE_BRANCH` of the CI workflow). Once the VPK is built and
   the PC tests pass, CI publishes the version if it is not published yet:
   - a final version (no label): tag `v<version>` on that commit, a GitHub release with the version's
     `CHANGELOG.md` section as notes and the VPK as `vitamoon-v<version>.vpk`; then the VPKs of
     pre-release versions waiting on the development build go into `vitamoon-v<version>-betas.zip` on
     that release (below);
   - a pre-release version (a label such as `beta.1`): only the tag `v<version>`, and the VPK parked
     on the development build as `vitamoon-v<version>.vpk`. No release page.

   Later pushes with the same version change nothing.

`python3 tools/dev/release_info.py` prints the tag and title CI will use and writes the notes, to
check them before pushing. Every push to `main` also refreshes the development build `build-main`
(fixed link `releases/download/build-main/vitamoon-main.vpk`), updated in place so that the parked
VPKs stay on it.

## Releases page

It lists only the final versions and the development build. Nothing of the pre-releases is lost: each
final release carries `vitamoon-v<version>-betas.zip`, with, for each pre-release of that version, the
VPK as it was published, its notes, the data of its former GitHub release (`release.json`: dates,
download counts) and an index with each file's SHA-256; the code of each one stays under its git tag
(`v1.0.0-beta.1`...). `tools/dev/archive_betas.py` builds the zip, uploads it, reads it back and checks
it file by file before deleting anything; `tools/dev/test_archive_betas.py` checks it against an
imitation of GitHub. The workflow *Archive pre-releases* (`.github/workflows/archive-betas.yml`) runs
it by hand, as a dry run unless *apply* is ticked.

Until 22 November 2026 every pre-release had its own release page. That day they were archived:
1.0.0-beta.1 to beta.11 into `vitamoon-v1.0.0-betas.zip` on the 1.0.0 release, with the last rolling
builds of the `dev` and `phase2` branches under `builds/`; 1.1.0-beta.1 to beta.4 into
`vitamoon-v1.5.0-betas.zip` on the 1.5.0 release. Branches removed that day, each already part of
`main` or kept under a tag: `dev` (last at `d9ca626`), `phase2` (`39f8447`), `vita` (`984603b`, the
same commit as `legacy/vita-moonlight-0.13.2`, which stays), and `sdk-export` (two commits outside
`main`, kept under the tag `archive/sdk-export`: a temporary workflow that copied the Vita SDK for
compile checks outside CI; its 136 MB copy of that public SDK was not kept).
