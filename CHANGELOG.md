# Changelog

All notable changes to TrimFast are recorded here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the version numbers follow
[Semantic Versioning](https://semver.org/) (before 1.0.0 a minor version may change behaviour).
`scripts/version.py` keeps the version in the build files in step with this file;
the release steps are in [docs/releasing.md](docs/releasing.md).

## [Unreleased]

## [0.1.2] - 2026-10-05

### Fixed

- **Insta360 stabilisation now works on trimmed clips.** The camera stores its motion data (gyro and
  accelerometer, 500 samples per second) at the end of the `.insv` file. 0.1.0 and 0.1.1 copied it
  whole, i.e. for the original recording; Insta360 Studio then switched the stabilisation **off** for
  the short clip (the horizon was still level, the picture was wider and unsteady). The motion and
  exposure data are now cut to the exported part and moved to start with it, and the size and
  duration the file records about itself are updated. Checked in Insta360 Studio with a real ONE X2
  recording: the pair opens as one video, stabilisation and horizon levelling work.

### Security

- **Insta360: pictures of the original footage are no longer left in the exported file.** The trailer
  of an `.insv` holds two full-resolution still pictures (one per lens) of the original recording,
  used as thumbnails. They were copied into every export, so a face or place you had cut away could
  still be found in the file. They are now removed (Insta360 Studio shows no difference). Other
  cameras' thumbnails or cover pictures embedded in a file are copied unchanged, as for any lossless
  copy.

### Changed

- The application ID (the `.desktop` file and the AppStream data) is now
  `io.github.taoman26.TrimFast`, and the project's home, <https://github.com/taoman26/TrimFast>, is shown
  in the About box and in `trimfast --help`.
- If the Insta360 trailer has a layout TrimFast does not know (another camera model: only a ONE X2
  has been examined), it is copied unchanged and the export says so: the stabilisation may not work,
  and the camera's pictures of the original recording remain in the file.

## [0.1.1] - 2026-10-05

### Fixed

- **Insta360 recordings are no longer split into two videos.** 0.1.0 named the two exported lens
  files `VID_…_00_082_trim.insv` and `VID_…_10_082_trim.insv`; the Insta360 app does not recognise
  them as one recording, because it pairs the lens files by file names that follow the camera's
  pattern (`VID_<date>_<time>_00_<serial>.insv` and `…_10_…`). Insta360 files are now exported
  without a suffix: the name keeps the pattern and its time of day is moved forward by one second
  (two, three, … if that name is taken), so the copy never collides with the original, for example
  `VID_20260923_105656_00_082.insv` and `…_10_082.insv`. Checked with the Insta360 app: the exported
  pair opens as one video. Files made by 0.1.0 can be renamed to such a pattern by hand.
- A name typed in the save dialog that breaks the pattern is now confirmed first (it may split the
  recording again), and the other lens' name is derived from it by swapping `_00_`/`_10_`.
- The *Out:* name in the status bar ignored the output-suffix setting and did not show the real
  (free) name; it now shows the name the export will use.

## [0.1.0] - 2026-10-05

First release: a lossless video trimmer for Haiku and Linux.

### Added

- **Lossless cutting.** Export copies the chosen part with FFmpeg stream copy, so picture, audio,
  subtitles, chapters and metadata stay exactly as they were. There is no way to ask for
  re-encoding. A 10-minute cut from a one-hour file takes a couple of seconds.
- **Main window** for opening, previewing and marking: video preview, a timeline that you can click
  and drag, keyframe ticks, and IN/OUT markers that can be dragged. Light theme, native look,
  keyboard first (Space, I, O, arrows, Shift/Ctrl+arrows, Home/End, K/L, Alt+I/O, Ctrl+O, Ctrl+E, Esc).
- **Keyframe-aware IN point.** IN snaps back to the previous keyframe (the status line says so),
  because a stream copy can only start cleanly there; Ctrl+←/→ jump between keyframes.
- **Safe export.** A save dialog with a free name (`clip_trim.mp4`, `clip_trim_2.mp4`, …), progress
  and Esc to cancel. Files are written under a temporary name and renamed only when everything
  succeeded, so a failure leaves nothing behind and an existing file is never replaced without
  asking. The result is checked against the input afterwards (streams, chapters, tags, spherical
  metadata), and the destination is checked first (free space, the 4 GiB limit of FAT).
- **Insta360 `.insv`.** Opening one lens file also finds the other (`_00_` ↔ `_10_`) and exports both
  with the same range. The camera's own data is carried over unchanged: the block at the end of the
  file (calibration, gyro; it still describes the original timeline) and the `AMBA` atom of the MP4
  header, which FFmpeg would drop. `.lrv` proxies are accepted.
- **Remembers your work.** The IN/OUT range is stored per file and restored when you open it again;
  *File > Open Last File*.
- **Ways to open a file**: File > Open, drag & drop, the command line (`trimfast clip.mp4`), and the
  file manager's "Open With" (Haiku Tracker).
- **Settings file** for the ffmpeg/ffprobe locations and the output name suffix.
- `trimfast --version` and `--help`.
- **Haiku package** (`TrimFast-0.1.0-1-x86_64.hpkg`): icon, Deskbar entry, registered for video files,
  `trimfast` command; depends on `qt6_base`, `qt6_multimedia`, `ffmpeg8_tools`.
- **Linux AppImage** (`TrimFast-0.1.0-x86_64.AppImage`) with its own Qt 6.10; X11 and Wayland;
  checked on Ubuntu 22.04, Debian 12 and Ubuntu 24.04. `cmake --install` also installs a
  `.desktop` file, AppStream metadata and icons.
- Test suite (22 tests, including real FFmpeg exports, failure paths and odd file names), run under
  AddressSanitizer/UBSan; `scripts/run_tests.sh`.

### Known limitations

- Not done yet: keeping the 360° metadata of Ricoh Theta and GoPro Max recordings (no sample files
  were available), a *Verify Output* menu entry, reverse playback (J), several ranges in one export.
- The end of a cut is accurate to the frame plus a few frames of the codec's reordering, not to
  the exact frame; this is how stream copy works.
- Insta360 gyro/stabilisation data is not trimmed (see above), and whether Insta360's own software
  accepts the exported files has not been checked. The MP4 header is rewritten by FFmpeg, so details
  such as the brand list and the alignment padding of the original are not reproduced.
- Audio: on Haiku it is off by default (it crashed the media server in a virtual machine);
  `TRIMFAST_AUDIO=1` turns it on. Not tried with real sound hardware on either system.
- The Haiku package was checked by unpacking it, resolving its dependencies and starting it from
  Tracker; it has not been installed with the package manager.
- Needs Qt 6.5 or newer (the FFmpeg backend of Qt Multimedia). On Linux use the AppImage if the
  distribution's Qt is older.

[Unreleased]: https://github.com/taoman26/TrimFast/compare/v0.1.2...HEAD
[0.1.2]: https://github.com/taoman26/TrimFast/releases/tag/v0.1.2
