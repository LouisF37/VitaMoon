# Releasing

## Identity and version

The package identity and the version are set at the top of `CMakeLists.txt`:

```cmake
set(TITLE_ID "LUNE00001")          # the app's own: installs next to Moonlight (XYZZ00002)
set(TITLE "Lune")                  # name under the LiveArea bubble
set(LIVEAREA_TITLE "LUNE")
set(LIVEAREA_SUBTITLE "GAME STREAMING\nFROM YOUR PC")
set(VERSION_MAJOR "1")
set(VERSION_MINOR "0")
set(VERSION_PATCH "0")
set(VERSION_LABEL "beta.1")        # empty for a final release
set(VERSION_NAME "Nouvelle Lune")  # milestone name
```

From them the build derives the version shown on the home screen (`v1.0.0-beta.1`, followed by the
branch and commit), the LiveArea text (generated from `sce_sys/livearea/contents/template.xml.in` into
the build folder) and the `APP_VER` of the VPK (`01.00`).

The name used by the code (top bar, logs, statistics) and the data folder (`ux0:data/lune`) are in
`src/app_info.h`. Changing the title ID or the data folder makes a different app for the Vita: the
settings and pairings of the old one are not carried over. The bubble icon and the gate image are
drawn by `tools/art/make_icons.py`.

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
5. Commit and push; wait for CI to build the commit.
6. Tag `v<version>` (for example `v1.0.0-beta.1`) on that commit and push the tag. Create a GitHub
   release for the tag (a pre-release for a beta) with the changelog section as notes, and attach the
   VPK built by CI for the commit as `lune-v<version>.vpk`.
