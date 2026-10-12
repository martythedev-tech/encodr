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
  at about the same quality, though H.264 files can come out up to a
  quarter larger. If the GPU refuses a file, it's redone on the CPU. Stats
  show which GPU is doing the work.
- **Presets that adapt to the source.** Review copy (H.264), review copy with
  burned-in timecode, small share copy, HEVC, AV1, ProRes Proxy and 422, DNxHR
  LB and HQ (what Resolve reads on Linux), an FFV1 archive master, lossless
  WAV and FLAC, AAC audio, a lossless cut, and a rewrap to MP4. Tune any
  setting and save your own.
- **AV1 for the smallest files.** 10-bit AV1 through SVT-AV1: about half the
  size of H.264 at the same quality, in about the same time. It needs an
  ffmpeg built with SVT-AV1 (the AppImage's is; Encodr says so if yours
  isn't). It runs on the CPU; GPU AV1 isn't used yet.
- **FFV1 for archives.** Mathematically lossless video in MKV, in the
  source's own pixel format (10-bit 4:2:2 stays 10-bit 4:2:2), every frame a
  keyframe with checksums per slice, and FLAC audio.
- **Play it before you convert it.** The preview is a player: the source, or
  the finished file afterwards. Under it, thumbnails across the whole clip
  with a playhead to scrub. Keys: P plays, arrows jump a second, comma and
  period step a frame.
- **See the output before you convert.** Switch the player to Output and,
  while paused, the frame at the playhead is shown as the conversion will make
  it: LUT, crop, scaling, bars, logo, burned-in subtitles and timecode,
  watermark.
  It updates as you step, scrub or change a setting.
- **Trim to just the part you need.** Drag the in and out points under the
  thumbnails, or press [ and ] at the playhead. Any preset converts only that
  part, and a trimmed copy keeps its place on the source's timecode.
- **Loudness to a standard.** EBU R128 (−23 LUFS), ATSC A/85 (−24), podcasts
  and web (−16) or streaming (−14): measured first, then set evenly, with the
  true peak held under the ceiling.
- **Cut or rewrap without re-encoding.** Copy the video and audio exactly as
  they are, in seconds: a lossless cut in the same file type, or MKV and MOV
  into an MP4 that plays anywhere. Encodr says up front when a stream won't
  fit the new file type (ProRes or uncompressed audio into MP4, say).
- **Join clips into one file.** Queue the clips in the order they should
  play and press Join. Clips that match (same codec, size, frame rate and
  audio, like one camera's) are put back to back as they are, and can be
  joined without re-encoding in seconds. Clips that differ are converted, each
  fitted to the first one's picture and frame rate with its first audio track;
  a clip without sound gets silence. Split puts them back.
- **Nothing left behind.** Every audio track is kept, with its language, and
  subtitles come along (as MP4 text, or exactly as they are in MKV).
- **Crop.** Remove black bars (found automatically), crop to 16:9, 1:1, 4:5 or
  9:16 for social media, or set the edges by hand; the player shows what's kept.
- **Bars to fit a shape.** The opposite of a crop: pad to 16:9, 4:3, 1:1, 4:5
  or 9:16 with black bars or a blurred copy of the picture, so a widescreen
  clip fits a vertical feed whole. The frame's short side stays the picture's
  (1080p becomes 1080×1920), within the size limit; the logo, subtitles and
  timecode go on the finished frame.
- **Burn in subtitles** from a track in the file, text or picture (Blu-ray
  PGS, DVD), a subtitle file beside it (`film.srt`, `film.en.ass`) or one you
  choose. Trimmed copies stay in sync, and burned-in timecode moves to the top
  to make room.
- **A LUT for log footage.** Apply a `.cube` LUT (S-Log, V-Log, C-Log…) so
  review copies look right; the copy is labelled Rec. 709.
- **A logo.** Put an image (PNG, JPEG, WebP) in a corner or the centre, sized
  to the picture and as see-through as you like, next to the text watermark.
- **A source inspector that explains problems and fixes them.** Blank colour
  labels (common on Ki Pro and other recorders), interlaced video, audio split
  across mono tracks, clipping and silent tracks, read-only source drives.
  When an MKV or MOV already holds H.264 or HEVC with AAC, it says the file
  only needs a rewrap, with one click to switch to it.
- **Check the whole file.** One pass finds black frames, frozen pictures,
  silence and the cuts between shots, marks them on the timeline (click one
  to jump there), and saves the cuts as an EDL for Resolve, Premiere or Avid.
- **A live preview while it converts.** The frame being worked on right now,
  as the output will look (with burned-in timecode, watermark, scaling) or
  as the source, or both side by side.
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
- **Name outputs your way.** One pattern for every conversion, under
  Settings → Output → File name: the original's name, the preset's suffix or
  id, the date and time, the picture height, and a running number
  (`{date}_{name}{suffix}` gives `2026-10-11_CLIP0001_review.mp4`). The
  browser still ticks originals that have an output.
- **The exact ffmpeg command**, ready to copy.

## Known limitations

- **The output is a still, not playback.** Before converting, the Output view
  shows the processed frame while paused; playing shows the source. To watch
  the processed picture move, use the live preview during the conversion or
  play the finished file.
- **Picture subtitles** (Blu-ray PGS, DVD) are drawn for the whole frame, so
  they go on before a crop: any placed in the cropped-off bars are cut. A copy
  trimmed to start in the middle of one shows it a few frames late.
- **Joined clips** play and check as their first clip: the player, the
  Output still and the whole-file check see only that one. Trim and subtitle
  burn-in are for single files.
- **GPU encoding** can make H.264 files up to a quarter larger than the
  CPU's at the same quality, and is still marked experimental. Quick Sync needs an Intel GPU
  from 2020 or later (older ones use VA-API instead).

## Shortcuts

| Key | Action |
| --- | --- |
| Space | Start the queue, or pause / resume |
| O | Add files (or drag them onto the window) |
| Double-click | In the browser: open a folder, or queue a file |
| V | Preview: output, source, or side by side (before converting: source or output still) |
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
