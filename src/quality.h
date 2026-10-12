#pragma once

#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include "mediainfo.h"
#include "preset.h"

// The check after converting: how close the picture came to what was asked
// for, and whether audio that should be bit-exact is.
//
// The picture is measured against the source run through the conversion's
// own processing (LUT, crop, scale, bars, logo, subtitles…), so only what
// the encoder lost counts. VMAF when there's a way to run it: ffmpeg's
// libvmaf filter, or the standalone vmaf tool fed through two pipes;
// otherwise SSIM, which every ffmpeg has.
namespace quality {

enum class Tool { LibVmaf, VmafCli, Ssim };

// "VMAF" or "SSIM", and how it was run, in words.
QString metricName(Tool tool);
QString toolName(Tool tool);

// The model file for this picture height, from the usual places, or empty
// (then a VMAF build with models inside it uses its own).
QString modelPath(int height);
// Every nth frame scored, so a long film stays under about 20,000.
int subsampleFor(qint64 frames);

// The output, normalised for comparing: from the start, at the output's
// frame rate, in a layout both metrics take.
QString comparisonFormat(const MediaInfo &output);

// One ffmpeg that measures with libvmaf or ssim, writing per-frame scores
// to `log` and its progress to stdout.
QStringList measureArguments(Tool tool, const Preset &preset, const MediaInfo &source, const QString &input,
                             double start, double length, const QString &output, const MediaInfo &outInfo,
                             const QString &log, int threads, int subsample);
// For the vmaf tool: the reference and the output as 4:2:0 y4m into two
// pipes, and the tool reading them.
QStringList referenceY4mArguments(const Preset &preset, const MediaInfo &source, const QString &input, double start,
                                  double length, const MediaInfo &outInfo, const QString &pipe);
QStringList outputY4mArguments(const QString &output, const MediaInfo &outInfo, const QString &pipe);
QStringList vmafToolArguments(const QString &referencePipe, const QString &outputPipe, const QString &json,
                              int height, int threads, int subsample);

struct Moment {
    double time = 0;     // seconds into the output
    double score = 0;
};

struct Result {
    Tool tool = Tool::Ssim;
    QList<double> times;     // seconds into the output, one per scored frame
    QList<double> scores;    // VMAF 0-100, or SSIM as a percentage
    int subsample = 1;
    double mean = 0, min = 0, low = 0;    // low: the worst 1% of frames starts here
    QList<Moment> worst;     // the weakest seconds, worst first, a few
    bool valid() const { return !scores.isEmpty(); }
};

// libvmaf's JSON log (the filter and the tool write the same one).
Result parseVmafLog(const QByteArray &json, double fps);
// The ssim filter's stats file: "n:1 Y:… All:0.987 (18.9)" per frame.
Result parseSsimLog(const QByteArray &log, double fps);
// Fills in the mean, minimum, 1% low and weakest moments.
void summarise(Result &result);
// What the numbers mean, in a sentence.
QString verdict(const Result &result);

// Audio: which output tracks should match the source bit for bit (copied,
// or lossless from lossless at the same depth and rate, nothing changed),
// and why not for the rest.
struct AudioPlan {
    int track = 0;
    bool compare = false;
    QString reason;          // when not compared
};
QList<AudioPlan> planAudio(const Preset &preset, const MediaInfo &source, bool trimmed);
// Hashes of each listed audio track's decoded samples, as `streamhash` lines.
QStringList audioHashArguments(const QString &file, const QList<int> &tracks);
// "0,a,MD5=…" lines into the hashes, in order.
QStringList parseStreamHashes(const QByteArray &output);

// The whole check as a plain-text report to keep beside the output.
QString report(const QVariantMap &check, const QStringList &header);

}
