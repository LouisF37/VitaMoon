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
set(VERSION_LABEL "beta.11")        # empty for a final release
set(VERSION_NAME "Nouvelle Lune")  # milestone name
```

From them the build derives the version shown on the home screen (`v1.0.0-beta.11`, followed by the
branch and commit), the LiveArea text (generated from `sce_sys/livearea/contents/template.xml.in` into
the build folder) and the `APP_VER` of the VPK (`01.00`).

The name used by the code (top bar, logs, statistics) and the data folder (`ux0:data/vitamoon`) are in
`src/app_info.h`. Changing the title ID makes a different app for the Vita (a new bubble); changing
the data folder leaves the settings and pairings behind unless the old folder is moved, as
`src/check_dir.c` does for Lune's (`APP_OLD_DATA_DIR`). The logo and every image made from it (LiveArea
bubble, gate and background, the mark of the top bar, the README logo, the GitHub preview) are drawn
by `tools/art/make_brand.py`.

The app was named *Lune* (title ID `LUNE00001`, folder `ux0:data/lune`) for 1.0.0-beta.1, and
*VitaMoon* from 1.0.0-beta.2.

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
   the PC tests pass, CI publishes the version if it has no release yet: tag `v<version>` on that
   commit, a GitHub release (a pre-release when the version has a label) with the version's
   `CHANGELOG.md` section as notes, and the VPK as `vitamoon-v<version>.vpk`. Later pushes with the same
   version change nothing.

`python3 tools/dev/release_info.py` prints the tag and title CI will use and writes the notes, to
check them before pushing. Every push also refreshes the development build `build-<branch>`.
