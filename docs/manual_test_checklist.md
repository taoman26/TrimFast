# Manual test checklist (real desktop)

Automated tests cannot press keys on Haiku's real desktop or judge how the window looks.
Run this list on **Haiku** and on **Linux with a display** before a release. Note the
platform, Qt version and result of each item. Test files: a short H.264 clip, a clip with
audio, and (if available) a real Insta360 `_00_`/`_10_` pair.

## 1. Start-up and layout
- [ ] Window opens in under a second; nothing flickers; title is "TrimFast".
- [ ] The whole window (including the buttons and the status bar) fits the screen at 1024x768.
- [ ] Resizing keeps the preview large and the other rows fixed. Minimum size is respected.
- [ ] Looks like the mockup (`mockup_main_window.png`): grey panels, no gradients, light only.
- [ ] Haiku: the yellow title tab, the system font and the menu bar look native.

## 2. Opening
- [ ] File > Open, Ctrl+O, drag & drop from Tracker/Files, and `trimfast <file>` all work.
- [ ] Title and status line show path, codec, size, fps, file size, length, keyframe count.
- [ ] A text file / broken video shows a clear message and the window keeps working.
- [ ] File names with spaces and Japanese characters open.
- [ ] File > Open Last File reopens the previous file.

## 3. Playback and navigation (keyboard)
- [ ] Space plays/pauses; the button label changes Play/Pause.
- [ ] Left/Right: one frame back/forward, also while paused, and the picture changes.
- [ ] Shift+Left/Right: 1 second. Ctrl+Left/Right: previous/next keyframe. Home/End.
- [ ] K pauses, L plays. (J reverse play is not implemented: pressing it does nothing.)
- [ ] The time label (below the preview) follows playback; the red playhead moves smoothly.
- [ ] Playback runs at about real time; the end of the clip stops it; Space restarts it.
- [ ] Audio (Linux): sound plays and stops with the picture. (Haiku: off by default.)

## 4. Timeline (mouse)
- [ ] Click and drag on the timeline seeks; the preview follows.
- [ ] Ruler labels are readable and do not overlap; they change when the window is resized.
- [ ] The pointer becomes a left-right arrow over the IN/OUT markers; dragging a marker moves it.
- [ ] Dragging IN past OUT is refused (marker stays) with a message.

## 5. IN / OUT
- [ ] I sets IN (snaps back to the previous keyframe, message says so); O sets OUT.
- [ ] IN/OUT boxes and "Duration" (selection length) update; the blue range matches.
- [ ] Alt+I / Alt+O jump to IN / OUT. Edit > Clear IN/OUT restores the whole file.
- [ ] Close and reopen the app and the file: the range is restored.

## 6. Export
- [ ] Ctrl+E (and the Export button) opens a save dialog with `<name>_trim.<ext>`.
- [ ] Choosing an existing name asks before replacing; cancel does nothing.
- [ ] Progress bar and "Exporting NN%" appear; Esc cancels and no file is left.
- [ ] The result plays in another player (VLC / MediaPlayer) with picture **and** sound, from the IN point.
- [ ] The file is as large as expected (a copy, not a re-encode) and is created in seconds.
- [ ] Open/Set IN/Set OUT/Export are disabled during the export; closing asks for confirmation.
- [ ] Insta360 pair: opening `_00_` says "Pair: ... found"; one export makes both files; both play in sync.
- [ ] Insta360 pair: the exported files are named like the originals with the time 1 s later (no `_trim`),
      and the Insta360 app opens them as **one** 360 video (verified once; repeat after changes to naming).
- [ ] Insta360 pair: in Insta360 Studio, the trimmed clip is stabilised (steady picture, narrower view
      than without stabilisation) and the horizon is level (verified once on a ONE X2 recording; repeat
      after changes to the trailer handling, and try other camera models if you have them).
- [ ] Output on a FAT/exFAT USB stick: a >4 GiB export on FAT is refused with an explanation.

## 7. Robustness
- [ ] Export to a read-only folder shows an error; the window stays usable.
- [ ] Remove the USB stick / network share during an export: error message, no crash, no partial file.
- [ ] Open a 1-hour video: window usable within a second or two; scrolling the timeline stays smooth.
- [ ] Leave the app playing for 5 minutes: memory stays flat (watch the system monitor).

## 8. Platform notes to record
- Haiku: audio on/off result (`TRIMFAST_AUDIO=1`), behaviour of the key shortcuts (Alt vs Ctrl), font sizes.
- Linux: Wayland and X11 both start; HiDPI scaling looks sane.
