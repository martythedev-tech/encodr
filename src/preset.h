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

    QString container = "mp4";     // mp4, mov, mkv, webm, wav, flac, m4a
    QString video = "h264";        // h264, hevc, prores_proxy, prores_lt, prores_422, prores_hq, vp9, none
    int crf = 18;
    int maxrateKbps = 16000;       // 0 = no cap
    // x264/x265 preset. "fast" measured 2.4x quicker than "slow" on a Ki Pro
    // clip with the same VMAF within 0.2 points, so slower presets cost hours
    // for no visible gain.
    QString speed = "fast";
    int maxHeight = 0;             // 0 = keep the source size
    QString audio = "aac";         // aac, pcm24, pcm16, flac, alac, opus, mp3, none
    int audioKbps = 320;
    bool deinterlace = true;       // only applied when the source is interlaced
    bool burnTimecode = false;
    QString watermark;

    bool audioOnly() const { return video == "none"; }
    QVariantMap toVariant() const;
    static Preset fromVariant(const QVariantMap &map);
};

QList<Preset> builtinPresets();

// Container/codec pairs that can't work, in plain words; empty when fine.
QString presetProblem(const Preset &preset);

struct Command {
    QStringList arguments;      // for ffmpeg, without the program name
    QStringList notes;          // what was adapted to this source
};

// start/length < 0 mean the whole file. With preview paths, ffmpeg also keeps
// a small JPEG of the frame it's working on up to date, once per second of
// media: the source as decoded, and the picture after Encodr's processing.
Command buildCommand(const Preset &preset, const MediaInfo &info, const QString &input,
                     const QString &output, double start = -1, double length = -1,
                     const QString &previewOut = {}, const QString &previewSource = {});

// Input options that relabel blank colour tags in the bitstream, if needed.
QStringList colourFixArguments(const MediaInfo &info);

// Arguments that write one small preview frame at `at` seconds to `jpeg`.
QStringList posterArguments(const MediaInfo &info, const QString &input, double at, const QString &jpeg, int height = 360);

// A shell-pasteable rendering of the command, for display and copying.
QString shellQuote(const QStringList &arguments);
