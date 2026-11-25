# Encodr on the command line

Encodr converts video and audio with ffmpeg. Its command line does what the
window does, with the same presets, output folder, file naming, GPU choice and
quality check, without opening a window. It needs no display, so it works over
SSH. It is meant for people and for agents (Claude Code, opencode and others).

```
encodr presets                 list the presets
encodr probe FILE...           what a file is, and anything worth knowing first
encodr convert FILE... [opts]  convert, one file after another
encodr guide                   this guide
```

Add `--json` to any of them for machine-readable output. Add `--help` to see a
command's options.

## For agents: the short version

1. `encodr presets --json` lists the preset ids. Pick one.
2. `encodr probe FILE --json` reads the file. Look at `issues` and `suggestion`.
3. `encodr convert FILE --preset ID --dry-run --json` shows the output path and
   the exact ffmpeg command without converting.
4. `encodr convert FILE --preset ID --json --check` converts. The last
   `"event":"done"` line says whether it worked, where the file is and what the
   quality check found.
5. Check the exit code: 0 everything worked, 1 a file failed or its quality
   check found a problem, 2 the command was wrong (the message on stderr says
   why), 130 stopped.

Encodr never overwrites a file. If the output name is taken, it adds a number
(`clip_review-2.mp4`). The source is never changed. A stopped or failed
conversion leaves no half-written file behind.

## presets

```
encodr presets
encodr presets --json
```

Lists the built-in presets and the ones saved in the window ("yours"), with
the window's default marked. `--json` gives every setting of each preset, as
`{"presets": [...], "default": "id"}`.

## probe

```
encodr probe clip.mov
encodr probe *.mov --json
```

Reads each file with ffprobe and measures its audio levels. For each file:

- `info`: container, duration (seconds), size (bytes), video codec, width,
  height, fps, pixel format, interlacing, bit depth, colour tags
  (`colorPrimaries`, `colorTransfer`, `colorSpace`), timecode, audio tracks
  (codec, channels, sample rate, bits, language, `meanDb`/`peakDb`) and
  subtitle tracks (`text: true` for ones that can be burned in).
- `issues`: plain-language findings, each `{level, title, detail}`. `fix`
  means Encodr handles it by itself, `warn` is worth telling the person,
  `info` is good to know.
- `suggestion`: a better preset for this file when there is one
  (`{title, detail, presetId, action}`), otherwise empty.
- `subtitles`: what `convert --subtitles` takes for this file, as
  `[{value, text}]`: its subtitle tracks (`track:0` is the first) and subtitle
  files beside it named after it (`film.srt`, `film.en.srt`). Pass `value`
  as it is.

Exit code 1 when a file can't be read.

## convert

```
encodr convert clip.mov --preset review
encodr convert *.mov --preset hevc --out ~/Videos/out --check
encodr convert interview.mxf --preset prores-proxy --trim 0:10-12:30
encodr convert talk.mp4 --preset review --crop 9:16 --set maxHeight=1920
encodr convert film.mkv --preset review --subtitles track:0 --dry-run
```

Files are converted one after another, each with the same options.

| Option | What it does |
|---|---|
| `-p, --preset ID` | The preset (see `encodr presets`). Default: the window's default. |
| `--set key=value` | Changes one preset setting, for this run only. Repeat it for more. |
| `-o, --out FOLDER` | Where outputs go. Default: the window's setting (normally beside the source; `~/Videos/Encodr` when the source folder is read-only). |
| `--name PATTERN` | The output file name: `{name}` `{suffix}` `{preset}` `{date}` `{time}` `{height}` `{n}`. Default: the window's, normally `{name}{suffix}`. |
| `--crop MODE` | `off`, `bars` (finds black bars), `1:1`, `9:16`, `4:5`, `16:9` (centred), or `L,T,R,B` pixels off each edge. |
| `--pad ASPECT` | Adds bars up to `16:9`, `4:3`, `1:1`, `4:5` or `9:16`. Add `:blur` (`9:16:blur`) to fill them with the picture, blurred. |
| `--trim RANGE` | Only part of the file: `IN-OUT`, `IN-` (to the end) or `-OUT` (from the start). Times are seconds (`90`, `90.5`) or `m:ss` / `h:mm:ss` (`1:30`, `1:02:03.5`). |
| `--subtitles WHICH` | Burns subtitles into the picture: a `value` from `probe`'s `subtitles` (`track:0` is the first track), or any subtitle file. |
| `--engine ENGINE` | The H.264/HEVC encoder: `auto` (the fastest working GPU, else the CPU), `cpu`, `nvenc`, `qsv`, `vaapi`. Default: the window's setting. If a GPU encode fails, Encodr redoes it on the CPU by itself and says so in `notes`. |
| `--check` | After converting, compares the result with the source: VMAF (or SSIM) for the picture, and whether lossless audio is bit-exact. |
| `--dry-run` | Shows the output path, the ffmpeg command and notes, without converting. |
| `--json` | One JSON event per line (below). |

### Settings for `--set`

| Key | Values |
|---|---|
| `container` | `source`, `mp4`, `mov`, `mkv`, `webm`, `wav`, `flac`, `m4a` |
| `video` | `copy`, `h264`, `hevc`, `av1`, `vp9`, `prores_proxy`, `prores_lt`, `prores_422`, `prores_hq`, `dnxhr_lb`, `dnxhr_sq`, `dnxhr_hq`, `dnxhr_hqx`, `ffv1`, `none` (audio only) |
| `crf` | Quality for H.264/HEVC/AV1/VP9: lower is better and bigger (18 is visually lossless for H.264). |
| `maxrateKbps` | Bitrate cap in kbps; 0 = none. |
| `targetSizeMB` | Fit the whole file into this many MB instead of a quality (H.264/HEVC take two passes on the CPU). 0 = off. |
| `speed` | x264/x265 speed: `ultrafast` … `fast` (default) … `veryslow`. |
| `maxHeight` | Scale down to this height in pixels; 0 = keep the source size. |
| `audio` | `copy`, `aac`, `opus`, `mp3`, `alac`, `flac`, `pcm24`, `pcm16`, `none` |
| `audioKbps` | Bitrate for AAC, Opus and MP3. |
| `loudness` | `off`, `ebu` (−23 LUFS), `atsc` (−24), `web` (−16), `stream` (−14): measured first, then set evenly. |
| `deinterlace` | `true`/`false`; only applied when the source is interlaced. |
| `burnTimecode` | `true`/`false` |
| `watermark` | Text drawn over the picture. |
| `lut` | A `.cube` LUT file for log footage. |
| `logo`, `logoPosition`, `logoSize`, `logoOpacity` | An image over the picture; position `tl` `tr` `bl` `br` `c`; size in percent of the picture's height; opacity in percent. |
| `suffix` | Added to the output name, e.g. `_review`. |

Settings changed with `--set` are for that run only. The command line never
changes the window's settings or saved presets.

### JSON events (`convert --json`)

One object per line, in this order for each file:

```
{"event":"start","file":"/abs/clip.mov","output":"/abs/clip_review.mp4","preset":"review","presetName":"Review copy","engine":"cpu","duration":10,"command":"ffmpeg …","notes":[]}
{"event":"progress","file":"/abs/clip.mov","percent":42.5,"speed":3.1,"fps":77,"eta":12,"size":5123456,"projectedSize":12000000,"state":"running"}
{"event":"done","file":"/abs/clip.mov","ok":true,"state":"done","output":"/abs/clip_review.mp4","size":11873210,"seconds":21,"engine":"cpu","notes":[],"message":"Saved as clip_review.mp4"}
```

Then, after the last file: `{"event":"summary","done":1,"failed":0,"interrupted":false}`.

- `progress` comes every 2 seconds. `eta` is in seconds, -1 while not yet known.
- `done` with `"ok":false` has the reason in `message`. A file that couldn't
  be read, or a setting it can't take, goes straight to `done`.
- With `--check`, `done` also has `quality`: `metric` (`VMAF` or `SSIM`),
  `mean`, `min`, `low` (worst 1%), `verdict`, `worst` (`[{time, score}]`) and
  `audio` (`[{track, state, text}]`, `state` being `exact` or `differs`), plus
  `qualityReport`, a text report saved beside the output. A lossless audio
  track that isn't bit-exact makes `ok` false.
- `--dry-run --json` gives one `{"event":"plan", …}` per file (the same fields
  as `start`) and converts nothing.

Without `--json`, progress goes to the terminal as one updating line, or as a
line every 10 seconds when the output isn't a terminal.

## Good to know

- Ctrl-C (or SIGTERM) stops the running conversion and deletes its
  half-written file; the exit code is 130.
- The AppImage uses the ffmpeg it carries; the package uses the system's.
- `--engine auto` tests the GPU encoders first (a few seconds).
