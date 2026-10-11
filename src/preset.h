#pragma once

#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include "mediainfo.h"

// A conversion recipe. Presets are settings, not raw ffmpeg arguments, so the
// same preset adapts to each source (split mono tracks, interlacing, broken
// colour tags) when the command is built.
struct Preset {
    QString id;
    QString name;
    QString description;
    QString suffix;              // added to the output file name, e.g. "_review"
    bool builtin = true;

    // mp4, mov, mkv, webm, wav, flac, m4a, or "source": the source's own
    // container (for cutting or copying without re-encoding).
    QString container = "mp4";
    // h264, hevc, prores_proxy, prores_lt, prores_422, prores_hq, dnxhr_lb,
    // dnxhr_sq, dnxhr_hq, dnxhr_hqx, vp9, none,
    // or "copy": the source's video as it is, not re-encoded.
    QString video = "h264";
    int crf = 18;
    int maxrateKbps = 16000;       // 0 = no cap
    // x264/x265 preset. "fast" measured 2.4x quicker than "slow" on a Ki Pro
    // clip with the same VMAF within 0.2 points, so slower presets cost hours
    // for no visible gain.
    QString speed = "fast";
    int maxHeight = 0;             // 0 = keep the source size
    QString audio = "aac";         // aac, pcm24, pcm16, flac, alac, opus, mp3, none, copy
    int audioKbps = 320;
    // off, ebu (-23 LUFS), atsc (-24), web (-16) or stream (-14): measured
    // in a first pass, then set evenly in the conversion.
    QString loudness = "off";
    bool deinterlace = true;       // only applied when the source is interlaced
    bool burnTimecode = false;
    QString watermark;
    // A .cube LUT for log footage, applied first; the copy is labelled Rec. 709.
    QString lut;
    // An image (PNG, JPEG, WebP) over the picture: tl, tr, bl, br or c, its
    // height as a percentage of the picture's, and how opaque, in percent.
    QString logo;
    QString logoPosition = "tr";
    int logoSize = 10;
    int logoOpacity = 80;
    // Where H.264/HEVC are encoded: "auto" (a working GPU encoder when GPU
    // encoding is switched on, otherwise the CPU), "cpu", "nvenc", "qsv" or
    // "vaapi". The job resolves "auto" before building the command.
    QString hardware = "auto";
    QString hardwareDevice;        // the VA-API render node; set per run, not saved
    // Pixels cropped off each edge of the source; set per run from the job
    // (crop belongs to a file, not a preset), not saved.
    int cropLeft = 0, cropTop = 0, cropRight = 0, cropBottom = 0;
    // Subtitles to burn into the picture, set per run from the job: a path
    // ffmpeg's filter can take as it is (a link with a plain name), the
    // subtitle track in it (-1 for a subtitle file), and what to call it.
    QString burnSubtitles;
    int burnSubtitleTrack = -1;
    QString burnSubtitleName;
    // Plain-named links to the LUT and logo for ffmpeg's filters; set per run.
    QString lutFile;
    QString logoFile;

    bool usesVideoEncoder() const { return video == "h264" || video == "hevc"; }
    bool audioOnly() const { return video == "none"; }
    bool copiesVideo() const { return video == "copy"; }
    bool copiesAudio() const { return audio == "copy"; }
    bool crops() const { return cropLeft || cropTop || cropRight || cropBottom; }
    bool burnsSubtitles() const { return !burnSubtitles.isEmpty(); }
    QVariantMap toVariant() const;
    static Preset fromVariant(const QVariantMap &map);
};

QList<Preset> builtinPresets();

// Container/codec pairs that can't work, in plain words; empty when fine.
QString presetProblem(const Preset &preset);
// The same for one source: streams copied as they are must fit the target
// container (ProRes can't go into MP4, say).
QString sourceProblem(const Preset &preset, const MediaInfo &info);
// The container written: the preset's, or the source's own for "source".
QString containerFor(const Preset &preset, const MediaInfo &info);

struct Command {
    QStringList arguments;      // for ffmpeg, without the program name
    QStringList notes;          // what was adapted to this source
};

// start/length < 0 mean the whole file. With preview paths, ffmpeg also keeps
// a small JPEG of the frame it's working on up to date, once per second of
// media: the source as decoded, and the picture after Encodr's processing.
// One audio stream's loudness, from ffmpeg's loudnorm in its measuring
// pass. NaN where it couldn't say (a silent track measures -inf).
struct Loudness {
    double input = NAN;       // integrated, LUFS
    double truePeak = NAN;    // dBTP
    double range = NAN;       // LU
    double threshold = NAN;
    double offset = 0;
    bool valid() const {
        return std::isfinite(input) && std::isfinite(truePeak) && std::isfinite(range) && std::isfinite(threshold);
    }
};

struct LoudnessTarget {
    double lufs = 0;          // 0 = off
    double truePeak = -1;
    QString name;             // "EBU R128"…
};
LoudnessTarget loudnessTarget(const QString &id);
// The measurement loudnorm prints as JSON at the end of its run.
Loudness parseLoudnorm(const QByteArray &output);
// How many audio streams the output gets, and the measuring pass for one of
// them (the same tracks, joins and trim as the conversion).
int audioStreamCount(const Preset &preset, const MediaInfo &info);
QStringList loudnessArguments(const Preset &preset, const MediaInfo &info, const QString &input, int stream,
                              double start = -1, double length = -1);

// `loudness` holds the measuring pass's result for each audio stream; without
// it, a loudness target is shown as a one-pass filter.
// A strip of `count` thumbnails across the source, side by side in one JPEG,
// from fast seeks (nothing decoded beyond the frames themselves).
QStringList stripArguments(const MediaInfo &info, const QString &input, int count, int height, const QString &jpeg);

Command buildCommand(const Preset &preset, const MediaInfo &info, const QString &input,
                     const QString &output, double start = -1, double length = -1,
                     const QString &previewOut = {}, const QString &previewSource = {},
                     const QList<Loudness> &loudness = {});

// One frame at `at` seconds through the conversion's own picture processing
// (LUT, crop, scale, logo, subtitles, timecode, watermark), at most `height`
// tall, as a JPEG. Empty when the video isn't processed (copied, audio only).
QStringList outputStillArguments(const Preset &preset, const MediaInfo &info, const QString &input, double at,
                                 const QString &jpeg, int height = 1080);

// When the source's video and audio already fit an MP4 that plays anywhere
// (H.264 or HEVC with AAC or MP3, in MKV, MOV or the like), the rewrap
// preset makes one in seconds: a plain-language reason why, else empty.
QString rewrapReason(const MediaInfo &info, const QString &path);

// The output picture's height after crop and size limit; 0 without a picture.
int outputHeight(const Preset &preset, const MediaInfo &info);

// Input options that relabel blank colour tags in the bitstream, if needed.
QStringList colourFixArguments(const MediaInfo &info);

// Arguments that write one small preview frame at `at` seconds to `jpeg`.
QStringList posterArguments(const MediaInfo &info, const QString &input, double at, const QString &jpeg, int height = 360);

// A shell-pasteable rendering of the command, for display and copying.
QString shellQuote(const QStringList &arguments);
