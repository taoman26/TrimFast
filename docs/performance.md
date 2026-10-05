# Performance measurements (Step 7)

Measured with `TRIMFAST_PERF=quit trimfast <file>` (see `src/app/PerfLog.h`); times are
milliseconds since the process started. Each row is the median of 3 runs unless noted.
Audio off. File opening is asynchronous: the window is usable while ffprobe, the first frame
and the keyframe scan are still running.

Machines: **Linux** = WSL2 (offscreen platform, Qt 6.10.3, FFmpeg 7.1.3 backend, SSD);
**Haiku** = R1/beta6 in a VM on the same PC (8 cores, Qt 6.10.3, FFmpeg 6.1.6 backend, software
decoding only). Absolute numbers are indicative; the Haiku VM is slower than real hardware would be.

## Start-up and opening a file

| Case | Linux: window shown | ffprobe done | first frame | keyframes | Haiku: window shown | ffprobe done | first frame | keyframes |
|---|---|---|---|---|---|---|---|---|
| no file | 25 | – | – | – | 97 | – | – | – |
| 6 s, 640x360 H.264 | 25 | 81 | 146 | 171 | 98 | 123 | 428 | 170 |
| 168 s, 2880x2880 H.264, 974 MB (Insta360) | 25 | 247 | 833 | 852 | – ¹ | – | – | – |
| 20 s, 2880x2880 MPEG-4, 93 MB ² | – | – | – | – | 96 | 129 | 1539 | 227 |
| **1 hour**, 90 000 frames, 460 MB / 667 MB ³ | 25 | 100 | 186 | **1 188** | 105 | 159 | 398 | **5 080** |

¹ The 974 MB file was not copied to the Haiku VM. ² Haiku's FFmpeg has no libx264, so the
large-frame clip is MPEG-4 Part 2. ³ Linux: H.264 720p; Haiku: MPEG-4 640x360 (a noisier,
bigger file). The first run on a cold cache can be several times slower (insv keyframe scan:
5.4 s the very first time, 0.85 s afterwards).

Goal from the plan: *empty start-up < 0.5 s* → **met** on both (0.025 s / 0.1 s).
*Operable after opening a one-hour video* → the window is usable after ≈ 0.2–0.4 s (first frame);
only keyframe navigation (Ctrl+←/→) and the keyframe ticks wait for the 1–5 s scan, and IN
snapping is re-applied when it finishes.

## Memory (Linux, resident set)

| Case | after start | file loaded | while playing (1.5 s → 4.5 s) |
|---|---|---|---|
| no file | 60 MiB | – | – |
| 640x360 | 60 | 113 | 115.4 → 115.5 (+0.1) |
| 720p, 1 hour | 60 | 154 | 160.9 → 160.9 (+0.0) |
| 2880x2880 (insv) | 60 | 554 | 663.5 → 668.1 (+4.6) |

Playback memory is flat (`tst_playbackmemory` fails the build if it grows by 100 MiB in 3 s).
The 2880x2880 case is dominated by decoded frames (33 MB each as an RGB image plus decoder
buffers). Haiku does not expose a resident-size figure to PerfLog, so it is not reported there.

## Export (stream copy)

| Case | Result |
|---|---|
| 10 min cut from the 1-hour file, Linux (SSD) | 76.6 MiB in **1.7 s** (44 MiB/s of output) |
| same, Haiku VM (667 MB MPEG-4 source) | 106.1 MiB in **4.0 s** (26 MiB/s) |
| Insta360 pair, 2 × 5.12 s of 2880x2880 | 2 files + trailer + verification in ≈ 1.5 s |

An export is a copy: its time is the time to read and write the bytes, never to encode.

## Quality gates

- Compiled with `-Wall -Wextra -Wpedantic`: **0 warnings** (GCC 11 and GCC 13).
- All 18 tests pass under **AddressSanitizer + UBSan** (Linux; `SANITIZE=1 scripts/run_tests.sh`):
  no memory errors or undefined behaviour reported. Leak detection (`detect_leaks=1`) was run
  separately on the core tests, the timeline widget test and the main-window test (before the
  failure-path and memory tests were added): no leaks. `scripts/run_tests.sh` turns it off by
  default because Qt and FFmpeg report their own start-up allocations.
