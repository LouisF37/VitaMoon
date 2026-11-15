# Releasing

## Version

The identity and version are set in one place, at the top of `CMakeLists.txt`:

```cmake
set(TITLE "Moonlight")             # name under the LiveArea bubble
set(VERSION_MAJOR "1")
set(VERSION_MINOR "0")
set(VERSION_PATCH "0")
set(VERSION_LABEL "beta.1")        # empty for a final release
set(VERSION_NAME "New Moon")       # milestone name
```

From them the build derives the version shown on the home screen (`v1.0.0-beta.1`, followed by the
branch and commit), the LiveArea text (generated from `sce_sys/livearea/contents/template.xml.in` into
the build folder) and the `APP_VER` of the VPK (`01.00`).

Numbering follows [Semantic Versioning](https://semver.org): the patch number for fixes, the minor
for features, the major for changes that break settings or pairing. Milestones are named after the
phases of the moon: *New Moon* (1.0), then *Waxing Crescent*, *First Quarter*, *Waxing Gibbous*,
*Full Moon*.

## Steps

1. Update `CHANGELOG.md`: move the *Unreleased* items under the new version and date.
2. Set the version in `CMakeLists.txt`.
3. Run the PC tests and the interface preview ([TESTING.md](TESTING.md)).
4. For a release that changes the stream, write a checkpoint report with Vita measurements in
   `docs/checkpoints/`.
5. Commit, tag `v<version>` (for example `v1.0.0-beta.1`) and push the tag. CI builds the VPK; attach
   it to a GitHub release for the tag, with the changelog section as notes.
