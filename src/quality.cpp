#include "quality.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QRegularExpression>
#include <algorithm>
#include <cmath>

namespace quality {

namespace {
QString rate(const MediaInfo &info) {
    return info.fpsRational.isEmpty() || info.fpsRational == "0/0" ? QString::number(info.fps > 0 ? info.fps : 25)
                                                                    : info.fpsRational;
}

// Both pictures from zero, at the output's rate (a variable-rate source
// lines up with its constant-rate copy), the reference sized to the output.
QString referenceNormal(const MediaInfo &out) {
    return QString("setpts=PTS-STARTPTS,fps=%1,scale=%2:%3:flags=bicubic,format=%4")
        .arg(rate(out)).arg(out.width).arg(out.height).arg(comparisonFormat(out));
}

QString outputNormal(const MediaInfo &out) {
    return QString("setpts=PTS-STARTPTS,fps=%1,format=%2").arg(rate(out), comparisonFormat(out));
}

const QStringList kModelDirs{"/usr/share/model", "/usr/share/vmaf/model", "/usr/local/share/model",
                             "/usr/local/share/vmaf/model"};
}

QString metricName(Tool tool) {
    return tool == Tool::Ssim ? "SSIM" : "VMAF";
}

QString toolName(Tool tool) {
    return tool == Tool::LibVmaf ? "ffmpeg's libvmaf" : tool == Tool::VmafCli ? "the vmaf tool" : "ffmpeg's ssim";
}

QString modelPath(int height) {
    // The 4K model for pictures meant to be watched at 4K.
    const QString name = height > 1440 ? "vmaf_4k_v0.6.1.json" : "vmaf_v0.6.1.json";
    for (const QString &dir : kModelDirs)
        if (QFileInfo::exists(dir + "/" + name))
            return dir + "/" + name;
    return {};
}

int subsampleFor(qint64 frames) {
    return std::max<qint64>(1, (frames + 19999) / 20000);
}

QString comparisonFormat(const MediaInfo &output) {
    // ffprobe doesn't give every codec's depth; the layout's name does.
    static const QRegularExpression deep("p(10|12|14|16)");
    return output.bitDepth > 8 || deep.match(output.pixelFormat).hasMatch() ? "yuv420p10le" : "yuv420p";
}

QStringList measureArguments(Tool tool, const Preset &preset, const MediaInfo &source, const QString &input,
                             double start, double length, const QString &output, const MediaInfo &outInfo,
                             const QString &log, int threads, int subsample) {
    const Reference ref = referencePicture(preset, source, input, start, length);
    QString metric;
    if (tool == Tool::Ssim) {
        metric = "ssim=stats_file=" + log;
    } else {
        metric = QString("libvmaf=log_fmt=json:log_path=%1:n_threads=%2:n_subsample=%3").arg(log).arg(threads).arg(subsample);
        // A model file on disk when there is one (distribution builds carry
        // none inside); otherwise the build's own.
        const QString model = modelPath(outInfo.height);
        if (!model.isEmpty())
            metric += ":model='path=" + model + "'";
        else if (outInfo.height > 1440)
            metric += ":model='version=vmaf_4k_v0.6.1'";
    }
    QStringList a{"-nostdin", "-hide_banner", "-loglevel", "error", "-nostats", "-progress", "pipe:1"};
    a << ref.inputs << "-i" << output;
    // libvmaf and ssim take the output first, then the reference.
    a << "-filter_complex"
      << ref.graph + ";[ref]" + referenceNormal(outInfo) + "[r];"
             + QString("[%1:v:0]").arg(ref.inputCount) + outputNormal(outInfo) + "[d];[d][r]" + metric + "[q]"
      << "-map" << "[q]" << "-f" << "null" << "-";
    return a;
}

QStringList referenceY4mArguments(const Preset &preset, const MediaInfo &source, const QString &input, double start,
                                  double length, const MediaInfo &outInfo, const QString &pipe) {
    const Reference ref = referencePicture(preset, source, input, start, length);
    QStringList a{"-nostdin", "-hide_banner", "-loglevel", "error", "-nostats", "-progress", "pipe:1"};
    a << ref.inputs << "-filter_complex" << ref.graph + ";[ref]" + referenceNormal(outInfo) + "[r]"
      << "-map" << "[r]" << "-f" << "yuv4mpegpipe" << "-strict" << "-1" << "-y" << pipe;
    return a;
}

QStringList outputY4mArguments(const QString &output, const MediaInfo &outInfo, const QString &pipe) {
    return {"-nostdin", "-hide_banner", "-loglevel", "error", "-i", output,
            "-filter_complex", "[0:v:0]" + outputNormal(outInfo) + "[d]", "-map", "[d]",
            "-f", "yuv4mpegpipe", "-strict", "-1", "-y", pipe};
}

QStringList vmafToolArguments(const QString &referencePipe, const QString &outputPipe, const QString &json,
                              int height, int threads, int subsample) {
    QStringList a{"--reference", referencePipe, "--distorted", outputPipe, "--threads", QString::number(threads),
                  "--subsample", QString::number(subsample), "--json", "--output", json, "--quiet"};
    const QString model = modelPath(height);
    a << "--model" << (model.isEmpty() ? QString(height > 1440 ? "version=vmaf_4k_v0.6.1" : "version=vmaf_v0.6.1")
                                       : "path=" + model);
    return a;
}

Result parseVmafLog(const QByteArray &json, double fps) {
    Result r;
    const QJsonArray frames = QJsonDocument::fromJson(json).object().value("frames").toArray();
    const double step = fps > 0 ? 1 / fps : 0.04;
    int previous = -1;
    for (const QJsonValue &f : frames) {
        const QJsonObject o = f.toObject();
        const QJsonValue score = o.value("metrics").toObject().value("vmaf");
        if (!score.isDouble())
            continue;
        const int n = o.value("frameNum").toInt();
        if (previous >= 0 && r.subsample == 1 && n - previous > 1)
            r.subsample = n - previous;
        previous = n;
        r.times << n * step;
        r.scores << score.toDouble();
    }
    summarise(r);
    return r;
}

Result parseSsimLog(const QByteArray &log, double fps) {
    Result r;
    static const QRegularExpression line("n:(\\d+).*All:([0-9.]+)");
    const double step = fps > 0 ? 1 / fps : 0.04;
    for (const QByteArray &l : log.split('\n')) {
        const auto m = line.match(QString::fromUtf8(l));
        if (!m.hasMatch())
            continue;
        r.times << (m.captured(1).toInt() - 1) * step;
        r.scores << m.captured(2).toDouble() * 100;
    }
    summarise(r);
    return r;
}

void summarise(Result &r) {
    if (r.scores.isEmpty())
        return;
    double sum = 0;
    for (double s : r.scores)
        sum += s;
    r.mean = sum / r.scores.size();
    QList<double> sorted = r.scores;
    std::sort(sorted.begin(), sorted.end());
    r.min = sorted.first();
    r.low = sorted[qsizetype(std::floor(0.01 * (sorted.size() - 1)))];

    // The weakest seconds: each second's average, lowest first, at least
    // three seconds apart so one bad shot isn't listed five times.
    QMap<int, QPair<double, int>> seconds;
    for (int i = 0; i < r.scores.size(); ++i) {
        auto &s = seconds[int(std::floor(r.times[i]))];
        s.first += r.scores[i];
        ++s.second;
    }
    QList<Moment> all;
    for (auto it = seconds.cbegin(); it != seconds.cend(); ++it)
        all << Moment{double(it.key()), it.value().first / it.value().second};
    std::sort(all.begin(), all.end(), [](const Moment &a, const Moment &b) { return a.score < b.score; });
    r.worst.clear();
    for (const Moment &m : all) {
        if (r.worst.size() >= 5)
            break;
        const bool near = std::any_of(r.worst.cbegin(), r.worst.cend(),
                                      [&](const Moment &w) { return std::abs(w.time - m.time) < 3; });
        if (!near)
            r.worst << m;
    }
}

QString verdict(const Result &r) {
    if (!r.valid())
        return {};
    // VMAF: about 6 points is a difference most viewers notice; 93 and up
    // is generally taken as indistinguishable. SSIM is coarser.
    const bool vmaf = r.tool != Tool::Ssim;
    const double m = r.mean;
    QString text;
    if (vmaf ? m >= 93 : m >= 99)
        text = "Excellent: practically indistinguishable from the source.";
    else if (vmaf ? m >= 87 : m >= 97.5)
        text = "Very good: close to the source; small differences if you look for them.";
    else if (vmaf ? m >= 80 : m >= 95)
        text = "Good: fine for watching and sharing, with some visible softening.";
    else
        text = "Visible loss: raise the quality if this copy matters.";
    if (r.low < m - (vmaf ? 10 : 3))
        text += " Some moments dip well below that; see the weakest seconds.";
    return text;
}

QList<AudioPlan> planAudio(const Preset &p, const MediaInfo &source, bool trimmed) {
    QList<AudioPlan> plans;
    const int count = audioStreamCount(p, source);
    static const QHash<QString, int> depth{{"pcm16", 16}, {"pcm24", 24}, {"flac", 32}, {"alac", 32}};
    const bool lossless = depth.contains(p.audio);
    const bool normalised = loudnessTarget(p.loudness).lufs < 0 && !p.copiesAudio();
    for (int k = 0; k < count; ++k) {
        AudioPlan plan;
        plan.track = k;
        const AudioTrack t = k < source.audio.size() ? source.audio[k] : AudioTrack();
        const bool sourceLossless = t.codec.startsWith("pcm_") || t.codec == "flac" || t.codec == "alac"
                                    || t.codec == "truehd";
        if (p.joins())
            plan.reason = "Joined clips: not compared.";
        else if (source.splitMono() && !p.copiesAudio())
            plan.reason = "Two mono tracks joined into stereo on purpose.";
        else if (normalised)
            plan.reason = "Loudness changed on purpose.";
        else if (trimmed)
            plan.reason = "Trimmed: not compared.";
        else if (p.copiesAudio())
            plan.compare = true;
        else if (!lossless)
            plan.reason = QString("Re-encoded as %1: lossy by design.").arg(p.audio.toUpper());
        else if (!sourceLossless)
            plan.reason = QString("The source's %1 is lossy; a lossless copy of it can't be bit-exact to the "
                                  "original recording.").arg(codecName(t.codec));
        else if (t.bits > depth.value(p.audio))
            plan.reason = QString("Stored at %1-bit from a %2-bit source.").arg(depth.value(p.audio)).arg(t.bits);
        else
            plan.compare = true;
        plans << plan;
    }
    return plans;
}

QStringList audioHashArguments(const QString &file, const QList<int> &tracks) {
    QStringList a{"-nostdin", "-hide_banner", "-loglevel", "error", "-i", file};
    for (int t : tracks)
        a << "-map" << QString("0:a:%1").arg(t);
    // Decoded samples in one layout for both sides: 16- and 24-bit alike
    // come out of their decoders as these same values.
    a << "-c:a" << "pcm_s32le" << "-f" << "streamhash" << "-hash" << "md5" << "-";
    return a;
}

QStringList parseStreamHashes(const QByteArray &output) {
    QStringList hashes;
    for (const QByteArray &line : output.split('\n')) {
        const int eq = line.indexOf('=');
        if (line.contains(",a,") && eq > 0)
            hashes << QString::fromUtf8(line.mid(eq + 1)).trimmed();
    }
    return hashes;
}

QString report(const QVariantMap &check, const QStringList &header) {
    QStringList out;
    out << "Encodr quality check" << QString(20, '=') << "";
    out << header << "";
    const QString metric = check.value("metric").toString();
    if (check.value("measured").toBool()) {
        out << QString("Picture (%1, measured with %2%3)")
                   .arg(metric, check.value("tool").toString(),
                        check.value("subsample").toInt() > 1
                            ? QString(", every %1th frame").arg(check.value("subsample").toInt()) : QString());
        out << QString("  Average   %1").arg(check.value("mean").toDouble(), 0, 'f', 2);
        out << QString("  Worst 1%  %1 and below").arg(check.value("low").toDouble(), 0, 'f', 2);
        out << QString("  Lowest    %1").arg(check.value("min").toDouble(), 0, 'f', 2);
        out << "  " + check.value("verdict").toString();
        const QVariantList worst = check.value("worst").toList();
        if (!worst.isEmpty()) {
            out << "  Weakest seconds:";
            for (const QVariant &w : worst) {
                const QVariantMap m = w.toMap();
                const int t = int(m.value("time").toDouble());
                out << QString("    %1:%2:%3  %4").arg(t / 3600).arg(t / 60 % 60, 2, 10, QChar('0'))
                           .arg(t % 60, 2, 10, QChar('0')).arg(m.value("score").toDouble(), 0, 'f', 2);
            }
        }
        out << "  Measured against the source through the same processing (crop, scaling, LUT, overlays),"
            << "  so only what the encoder lost counts.";
    } else if (!check.value("videoNote").toString().isEmpty()) {
        out << "Picture" << "  " + check.value("videoNote").toString();
    }
    const QVariantList audio = check.value("audio").toList();
    if (!audio.isEmpty()) {
        out << "" << "Audio";
        for (const QVariant &v : audio) {
            const QVariantMap a = v.toMap();
            out << QString("  Track %1: %2").arg(a.value("track").toInt() + 1).arg(a.value("text").toString());
        }
    }
    return out.join('\n') + '\n';
}

}
