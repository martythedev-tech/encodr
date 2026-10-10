# Encodr

Convert video and audio with ffmpeg, and see everything about the job while it runs.

Encodr is an Omarchy app: one window, keyboard first, following your theme.
Find files in the built-in browser, pick a preset, press Space.

Everything sits in one workspace, in three columns of panels you can resize:
the browser and the queue; the picture being converted with its settings
below; and the source details beside live stats. The layout is remembered.

## What it does

- **A file browser built in.** Places and mounted drives (USB, camera cards, a
  Mac drive through apfs-fuse) across the top; every video shows a thumbnail
  and its length, picture size, codec and frame rate. Files that already have
  a converted copy beside them get a tick. Double-click or drag to queue.
- **A queue that uses the whole machine.** Add as many files (or folders) as
  you like. Big computers convert several side by side (automatically: one
  per eight cores, up to four, or choose). Reorder, remove, undo, retry.
- **GPU encoding (experimental, off by default).** Encodr finds the hardware
  encoders that really work on this machine (NVIDIA NVENC, Intel Quick Sync,
  VA-API for Intel and AMD) and can use them for H.264 and HEVC: much faster
  at the same quality, but files come out larger (often about twice the
  size). If the GPU refuses a file, it's redone on the CPU. Stats show which
  GPU is doing the work.
- **Presets that adapt to the source.** Review copy (H.264), review copy with
  burned-in timecode, small share copy, HEVC, ProRes Proxy and 422, lossless
  WAV and FLAC, AAC audio, a lossless cut, and a rewrap to MP4. Tune any
  setting and save your own.
- **Trim to just the part you need.** Drag the in and out points under the
  preview; it shows the exact frame as you go. Any preset converts only that
  part, and a trimmed copy keeps its place on the source's timecode.
- **Cut or rewrap without re-encoding.** Copy the video and audio exactly as
  they are, in seconds: a lossless cut in the same file type, or MKV and MOV
  into an MP4 that plays anywhere. Encodr says up front when a stream won't
  fit the new file type (ProRes or uncompressed audio into MP4, say).
- **Nothing left behind.** Every audio track is kept, with its language, and
  subtitles come along (as MP4 text, or exactly as they are in MKV).
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
- **The machine doing the work.** Load on every core, the clock speed the
  encoder really gets (measured from its CPU cycles, because the kernel only
  reports the speed it asked for), a warning when heat slows the CPU and how
  long it did, temperatures and power draw from whatever sensors the kernel
  exposes, and the graphics card's load, encoder load and clocks (NVIDIA,
  AMD, Intel).
- **Pause and resume** a conversion exactly where it is. Sleep and lid-close
  suspend are blocked while converting (not while paused, so a paused job can
  wait through sleep), and a notification says when it's done.
- **Safe with your files.** Originals are never touched. Output is written to a
  hidden partial file and renamed only when it's complete, so a failed or
  stopped conversion leaves nothing behind. Existing files are never overwritten.
- **The exact ffmpeg command**, ready to copy.

## Shortcuts

| Key | Action |
| --- | --- |
| Space | Start the queue, or pause / resume |
| O | Add files (or drag them onto the window) |
| Double-click | In the browser: open a folder, or queue a file |
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
| ? | Shortcuts |
| Q | Quit (asks first while converting) |

## Install

On Omarchy (or any Arch-based system), download the package for your machine
from the [latest release](https://github.com/martythedev-tech/encodr/releases/latest):
`x86_64` for most computers, `aarch64` for ARM ones such as Apple Silicon
Macs running Asahi. Then:

```sh
sudo pacman -U ./encodr-*.pkg.tar.*
```

Encodr appears in the app launcher (Super + Space). Pacman pulls in what it
needs (Qt 6, ffmpeg); `sudo pacman -S mediainfo` adds the full technical
report.

### Other Linux distributions

Download the AppImage for your machine from the same release, make it
executable, and run it:

```sh
chmod +x Encodr-*.AppImage
./Encodr-*.AppImage
```

It carries everything it needs, ffmpeg included, so nothing else has to be
installed. Every release is started on stock Ubuntu, Debian and Fedora and
converts a clip before it's published. The x86_64 AppImage runs on Ubuntu
22.04, Debian 12, Fedora 36 or newer; the aarch64 one on Ubuntu 24.04,
Debian 13, Fedora 39 or newer. The first time it runs, Encodr offers to add
itself to your app menu (with its icon); if you move the AppImage or switch
to a newer one, the menu entry follows it.

Outside Omarchy, Encodr uses its own dark theme.

## Build

```sh
git clone https://github.com/martythedev-tech/encodr && cd encodr
bin/build        # build/encodr
bin/test         # unit and end-to-end tests, offscreen
bin/install      # package and install, so it shows in the launcher
```

Needs Qt 6 (base, declarative, wayland), ffmpeg and libnotify. `mediainfo` is
optional and adds a full technical report to the Source tab.

## License

MIT. Theme watching, the portal file picker and the project layout come from
[Omacut](https://github.com/omacom-io/omacut) by David Heinemeier Hansson.
