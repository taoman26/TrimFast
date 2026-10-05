# Releasing

How a new version of TrimFast is made. The version number lives in **one** place,
`CMakeLists.txt`; `scripts/version.py` writes it to the few files that cannot read it and
`scripts/version.py check` (also a test) catches any drift.

## Version numbers

`MAJOR.MINOR.PATCH` ([Semantic Versioning](https://semver.org/)), numbers only.

| Change | Bump |
|---|---|
| Fixes only; nothing a user has to relearn | PATCH (0.1.0 → 0.1.1) |
| New features, or a behaviour change | MINOR (0.1.0 → 0.2.0) |
| Incompatible change of a file format or setting that people rely on | MAJOR (from 1.0.0); before 1.0.0 use MINOR |

A change to the *packaging only* (a missing dependency, a new icon size) keeps the program's version
and raises the **package revision**: `REVISION=2 packaging/haiku/build_hpkg.sh` gives
`TrimFast-0.1.0-2-x86_64.hpkg`. An AppImage has no revision: such a fix is a PATCH release.

## Steps

1. **Write the notes.** Add what changed under `## [Unreleased]` in `CHANGELOG.md` (Added / Changed /
   Fixed / Removed), written for users. Update *Known limitations* and the README if they changed.
2. **Set the version.**
   ```
   scripts/version.py set 0.2.0          # --date YYYY-MM-DD if not today
   ```
   This moves the notes under `## [0.2.0] - <date>`, updates `CMakeLists.txt`, the Haiku
   `trimfast.rdef`, the AppStream release list and the link references at the end of the changelog
   (`[Unreleased]: …/compare/v0.2.0...HEAD`, `[0.2.0]: …/compare/v0.1.2...v0.2.0`), then runs the check. It refuses to go backwards
   or to release without notes, and changes nothing when it refuses.
3. **Test on both systems** (Linux and Haiku), from a clean build:
   ```
   scripts/run_tests.sh                  # all tests; also SANITIZE=1 scripts/run_tests.sh on Linux
   ```
   Go through the Haiku part of [manual_test_checklist.md](manual_test_checklist.md) for anything
   that touched the window.
4. **Build the packages.**
   ```
   # Haiku (on Haiku)
   LICENSE=MIT packaging/haiku/build_hpkg.sh              # -> build-hpkg/TrimFast-<v>-1-x86_64.hpkg
   # Linux
   QT_PREFIX=$HOME/Qt/6.10.3/gcc_64 packaging/linux/build_appimage.sh   # -> build-appimage/out/
   packaging/linux/smoke_test_appimage.sh
   packaging/linux/test_in_distros.sh build-appimage/out/*.AppImage clip.mp4     # needs Docker
   ```
5. **Collect the files** in `dist/`, replacing the old ones, and write the checksums:
   ```
   cd dist && sha256sum TrimFast-* > SHA256SUMS
   ```
   Compare the checksum of a file copied off the Haiku machine with `sha256sum` *there*.
6. **Tag it and publish.** `git tag -a v0.2.0 -m "TrimFast 0.2.0" && git push origin v0.2.0` (the tag name
   `vX.Y.Z` is what the changelog links point to). Then create a release for the tag at
   <https://github.com/taoman26/TrimFast/releases/new>, paste the changelog section of the version
   as its description and attach the files of `dist/` (the `.hpkg`, the AppImage and `SHA256SUMS`;
   `dist/` itself is not committed).

## Checking a build

`trimfast --version` prints the version of the program; the About box shows the same. The
`.hpkg` carries it in its name and `.PackageInfo`; the AppImage in its file name. If any of them
disagree with `CHANGELOG.md`, `scripts/version.py check` was skipped.
