# Encodr

Convert video and audio with ffmpeg, and see everything about the job while it runs.

Encodr is an Omarchy app: one window, keyboard first, following your theme.
Drop in files, pick a preset, press Space.

## What it does

- **A queue.** Add as many files (or folders) as you like; they convert one
  after another. Reorder, remove, undo, retry.
- **Presets that adapt to the source.** Review copy (H.264), review copy with
  burned-in timecode, small share copy, HEVC, ProRes Proxy and 422, lossless
  WAV and FLAC, AAC audio. Tune any setting and save your own.
- **A source inspector that explains problems and fixes them.** Blank colour
  labels (common on Ki Pro and other recorders), interlaced video, audio split
  across mono tracks, clipping and silent tracks, read-only source drives.
- **A live preview while it converts.** The frame being worked on right now,
  as the output will look (with burned-in timecode, watermark, scaling) or
  as the source, or both side by side. Before converting, a poster frame.
- **Every number while it converts.** Percent, elapsed, time left and the clock
  time it will finish, speed (now and average), frames and fps, bitrate,
  size so far and the projected final size against free space, quantizer,
  dropped frames, encoder CPU, energy used, and live graphs of speed, fps,
  bitrate, CPU and power.
- **The machine doing the work.** Load on every core, clock speeds against
  their maximum (it warns when heat slows the CPU), temperatures and power
  draw from whatever sensors the kernel exposes.
- **Pause and resume** a conversion exactly where it is. Sleep and lid-close
  suspend are blocked while converting, and a notification says when it's done.
- **Safe with your files.** Originals are never touched. Output is written to a
  hidden partial file and renamed only when it's complete, so a failed or
  stopped conversion leaves nothing behind. Existing files are never overwritten.
- **The exact ffmpeg command**, ready to copy.

## Shortcuts

| Key | Action |
| --- | --- |
| Space | Start the queue, or pause / resume |
| O | Add files (or drag them onto the window) |
| V | Preview: output, source, or side by side |
| C | Stop the current file |
| R | Convert the selected file again |
| Enter | Play the finished file |
| F | Show the finished file in its folder |
| ↑ ↓ or K J | Select a file |
| Alt + ↑ ↓ | Move a file in the queue |
| Delete | Remove the selected file |
| X | Clear finished files |
| Ctrl + Z / Ctrl + Shift + Z | Undo / redo removing |
| 1 2 3 | Progress, Source, Settings |
| ? | Shortcuts |
| Q | Quit (asks first while converting) |

## Build

```sh
bin/build        # build/encodr
bin/test         # unit and end-to-end tests, offscreen
bin/install      # package and install, so it shows in the launcher
```

Needs Qt 6 (base, declarative, wayland), ffmpeg and libnotify. `mediainfo` is
optional and adds a full technical report to the Source tab.

## License

MIT. Theme watching, the portal file picker and the project layout come from
[Omacut](https://github.com/omacom-io/omacut) by David Heinemeier Hansson.
