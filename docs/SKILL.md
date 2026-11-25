---
name: encodr
description: >
  Convert, trim, crop, shrink or check video and audio files with Encodr's
  command line (encodr presets / probe / convert). Use when asked to convert a
  video, make a review or share copy, an edit proxy (ProRes, DNxHR), HEVC or
  AV1, extract or re-encode audio, fit a file to a size, cut a clip, crop to
  9:16 or another aspect, burn in subtitles, normalise loudness, or check a
  conversion's quality (VMAF/SSIM, bit-exact audio). Prefer it over writing
  ffmpeg commands by hand.
---

# Encodr

Encodr is a video and audio converter built on ffmpeg. Its command line uses
the same presets, output folder, naming and GPU choice as its window, and
needs no display. It needs Encodr 1.3.0 or newer: check with `encodr --version`.

**Read the full guide first: run `encodr guide`.** It lists every option,
every `--set` setting and the JSON event format.

## The usual steps

```bash
encodr presets --json                                   # pick a preset id
encodr probe FILE --json                                # read issues, suggestion, subtitles
encodr convert FILE --preset ID --dry-run --json        # output path + ffmpeg command
encodr convert FILE --preset ID --json --check          # convert, then check quality
```

- Read the last `{"event":"done", ...}` line of each file: `ok`, `output`,
  `message` (why it failed), `notes`, and with `--check`, `quality`.
- Exit code: 0 all worked, 1 a file failed or its check found a problem,
  2 the command was wrong (stderr says why), 130 stopped.
- If `probe` gives a `suggestion` with a `presetId` (a rewrap instead of a
  re-encode, say), tell the person and prefer it unless they asked otherwise.
- Pass `--out FOLDER` when the person names where files should go; otherwise
  Encodr uses the folder set in its window. Say where the files went.
- Encodr never overwrites anything (it numbers the new name) and never touches
  the source. Long conversions take minutes: run them in the background where
  the agent allows it, and report progress from the `progress` events.
