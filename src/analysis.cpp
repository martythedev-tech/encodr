#include "analysis.h"

#include <QHash>
#include <QRegularExpression>
#include <QVariantMap>
#include <algorithm>
#include <cmath>

QStringList analysisArguments(const MediaInfo &info, const QString &input) {
    QStringList a{"-nostdin", "-hide_banner", "-nostats", "-progress", "pipe:1", "-i", input};
    QStringList graph, maps;
    if (info.hasVideo) {
        // 320 wide is plenty to see black, a still picture or a change of shot.
        graph << "[0:v:0]scale=320:-2:flags=fast_bilinear,blackdetect=d=0.5:pix_th=0.10,"
                 "freezedetect=n=-60dB:d=1,scdet=t=12[v]";
        maps << "-map" << "[v]";
    }
    if (!info.audio.isEmpty()) {
        graph << "[0:a:0]silencedetect=n=-50dB:d=1[a]";
        maps << "-map" << "[a]";
    }
    a << "-filter_complex" << graph.join(';') << maps << "-f" << "null" << "-";
    return a;
}

QStringList barArguments(const MediaInfo &info, const QString &input, int samples) {
    QStringList a{"-nostdin", "-hide_banner", "-nostats"};
    QStringList graph, outputs;
    const double last = std::max(0.0, info.duration - 1);
    for (int i = 0; i < samples; ++i) {
        const double at = std::min(last, info.duration * (i + 0.5) / samples);
        a << "-ss" << QString::number(at, 'f', 2) << "-t" << "0.5" << "-i" << input;
        graph << QString("[%1:v:0]cropdetect=limit=24:round=2:reset=0[c%1]").arg(i);
        outputs << "-map" << QString("[c%1]").arg(i) << "-f" << "null" << "-";
    }
    a << "-filter_complex" << graph.join(';') << outputs;
    return a;
}

QMargins parseBars(const QByteArray &log, const MediaInfo &info) {
    static const QRegularExpression line("Parsed_cropdetect_(\\d+).*x1:(\\d+) x2:(\\d+) y1:(\\d+) y2:(\\d+)");
    // The last line from each sample is its settled view.
    QHash<int, QList<int>> last;
    for (const QByteArray &raw : log.split('\n')) {
        const QRegularExpressionMatch m = line.match(QString::fromUtf8(raw));
        if (m.hasMatch())
            last[m.captured(1).toInt()] = {m.captured(2).toInt(), m.captured(3).toInt(), m.captured(4).toInt(), m.captured(5).toInt()};
    }
    int x1 = info.width, x2 = -1, y1 = info.height, y2 = -1;
    for (const QList<int> &b : std::as_const(last)) {
        if (b[1] < b[0] || b[3] < b[2])
            continue;    // all black (a fade): says nothing about bars
        x1 = std::min(x1, b[0]);
        x2 = std::max(x2, b[1]);
        y1 = std::min(y1, b[2]);
        y2 = std::max(y2, b[3]);
    }
    if (x2 < 0 || info.width <= 0 || info.height <= 0)
        return {};
    const int left = x1 & ~1, top = y1 & ~1;
    const int width = (x2 + 1 - left) & ~1, height = (y2 + 1 - top) & ~1;
    const QMargins bars(left, top, info.width - left - width, info.height - top - height);
    // A few pixels is an encoder's edge, not a bar.
    const bool worth = bars.left() + bars.right() >= 8 || bars.top() + bars.bottom() >= 8;
    return worth ? bars : QMargins();
}

QList<Moment> parseAnalysis(const QByteArray &log, double duration) {
    static const QRegularExpression black("black_start:\\s*([0-9.]+)\\s+black_end:\\s*([0-9.]+)");
    static const QRegularExpression freezeStart("freezedetect\\.freeze_start:\\s*([0-9.]+)");
    static const QRegularExpression freezeEnd("freezedetect\\.freeze_end:\\s*([0-9.]+)");
    static const QRegularExpression silenceStart("silence_start:\\s*(-?[0-9.]+)");
    static const QRegularExpression silenceEnd("silence_end:\\s*([0-9.]+)");
    static const QRegularExpression cut("lavfi\\.scd\\.time:\\s*([0-9.]+)");

    QList<Moment> moments, freezes;
    double openFreeze = -1, openSilence = -1;
    for (const QByteArray &raw : log.split('\n')) {
        const QString line = QString::fromUtf8(raw);
        QRegularExpressionMatch m;
        if ((m = black.match(line)).hasMatch()) {
            moments.append({"black", m.captured(1).toDouble(), m.captured(2).toDouble()});
        } else if ((m = freezeStart.match(line)).hasMatch()) {
            openFreeze = m.captured(1).toDouble();
        } else if ((m = freezeEnd.match(line)).hasMatch() && openFreeze >= 0) {
            freezes.append({"freeze", openFreeze, m.captured(1).toDouble()});
            openFreeze = -1;
        } else if ((m = silenceStart.match(line)).hasMatch()) {
            openSilence = std::max(0.0, m.captured(1).toDouble());
        } else if ((m = silenceEnd.match(line)).hasMatch() && openSilence >= 0) {
            moments.append({"silence", openSilence, m.captured(1).toDouble()});
            openSilence = -1;
        } else if ((m = cut.match(line)).hasMatch()) {
            const double t = m.captured(1).toDouble();
            if (t > 0)
                moments.append({"cut", t, t});
        }
    }
    // Still going when the file ended.
    if (openFreeze >= 0)
        freezes.append({"freeze", openFreeze, duration});
    if (openSilence >= 0)
        moments.append({"silence", openSilence, duration});

    // Black is also a still picture: a freeze that is mostly black isn't news.
    for (const Moment &f : freezes) {
        double covered = 0;
        for (const Moment &b : moments)
            if (b.kind == "black")
                covered += std::max(0.0, std::min(f.end, b.end) - std::max(f.start, b.start));
        if (covered < 0.8 * (f.end - f.start))
            moments.append(f);
    }
    std::sort(moments.begin(), moments.end(), [](const Moment &a, const Moment &b) { return a.start < b.start; });
    return moments;
}

namespace {
QString clock(double seconds) {
    const int s = int(std::floor(seconds));
    const int tenths = int(std::floor((seconds - s) * 10));
    return s >= 3600 ? QString("%1:%2:%3.%4").arg(s / 3600).arg(s / 60 % 60, 2, 10, QChar('0')).arg(s % 60, 2, 10, QChar('0')).arg(tenths)
                     : QString("%1:%2.%3").arg(s / 60).arg(s % 60, 2, 10, QChar('0')).arg(tenths);
}

QString span(const Moment &m) {
    return QString("%1 s at %2").arg(m.end - m.start, 0, 'f', 1).arg(clock(m.start));
}
}

QList<Issue> analysisIssues(const QList<Moment> &moments, double duration) {
    QList<Issue> issues;
    const double edge = 1.0;    // within a second of either end counts as the start or end
    auto atEdge = [&](const Moment &m) { return m.start <= edge || m.end >= duration - edge; };

    QStringList blackMid, blackEdge, freezes, silenceMid;
    int cuts = 0;
    for (const Moment &m : moments) {
        if (m.kind == "black")
            (atEdge(m) ? blackEdge : blackMid) << span(m);
        else if (m.kind == "freeze")
            freezes << span(m);
        else if (m.kind == "silence" && !atEdge(m))
            silenceMid << span(m);
        else if (m.kind == "cut")
            ++cuts;
    }
    if (!blackMid.isEmpty())
        issues.append({"warn", QString("Black picture %1 time%2 in the middle").arg(blackMid.size()).arg(blackMid.size() == 1 ? "" : "s"),
                       "Could be a dropout or a gap between clips: " + blackMid.mid(0, 4).join(", ")
                           + (blackMid.size() > 4 ? QString(" and %1 more").arg(blackMid.size() - 4) : QString()) + "."});
    if (!freezes.isEmpty())
        issues.append({"warn", QString("The picture freezes %1 time%2").arg(freezes.size()).arg(freezes.size() == 1 ? "" : "s"),
                       "A stuck or repeated frame, or a \"media offline\" still: " + freezes.mid(0, 4).join(", ")
                           + (freezes.size() > 4 ? QString(" and %1 more").arg(freezes.size() - 4) : QString()) + "."});
    if (!silenceMid.isEmpty())
        issues.append({"warn", QString("Silence %1 time%2 in the middle").arg(silenceMid.size()).arg(silenceMid.size() == 1 ? "" : "s"),
                       "Audio track 1 goes quiet: " + silenceMid.mid(0, 4).join(", ")
                           + (silenceMid.size() > 4 ? QString(" and %1 more").arg(silenceMid.size() - 4) : QString()) + "."});
    if (!blackEdge.isEmpty())
        issues.append({"info", "Black at the start or end", "Usually a fade or a slate: " + blackEdge.join(", ") + "."});
    if (cuts > 0)
        issues.append({"info", QString("%1 cut%2 between shots").arg(cuts).arg(cuts == 1 ? "" : "s"),
                       "Save them as an EDL to rebuild the edit in Resolve, Premiere or Avid."});
    if (issues.isEmpty())
        issues.append({"info", "Checked: nothing found", "No black frames, freezes, silence or cuts."});
    return issues;
}

QVariantList momentsToVariant(const QList<Moment> &moments) {
    QVariantList list;
    for (const Moment &m : moments)
        list.append(QVariantMap{{"kind", m.kind}, {"start", m.start}, {"end", m.end}});
    return list;
}

namespace {
qint64 framesOf(const QString &tc, int base) {
    const QStringList p = tc.split(QRegularExpression("[:;.]"));
    if (p.size() != 4)
        return 0;
    return ((p[0].toLongLong() * 60 + p[1].toLongLong()) * 60 + p[2].toLongLong()) * base + p[3].toLongLong();
}

QString timecode(qint64 frames, int base) {
    const qint64 ff = frames % base, s = frames / base;
    return QString("%1:%2:%3:%4").arg(s / 3600 % 24, 2, 10, QChar('0')).arg(s / 60 % 60, 2, 10, QChar('0'))
        .arg(s % 60, 2, 10, QChar('0')).arg(ff, 2, 10, QChar('0'));
}
}

QString edlFromCuts(const QList<Moment> &moments, const MediaInfo &info, const QString &title) {
    const int base = std::max(1, int(std::lround(info.fps)));
    const qint64 sourceStart = info.timecode.isEmpty() ? 0 : framesOf(info.timecode, base);
    const qint64 recordStart = qint64(base) * 3600;    // edits conventionally start at 01:00:00:00
    QList<qint64> bounds{0};
    for (const Moment &m : moments)
        if (m.kind == "cut")
            bounds << qint64(std::llround(m.start * info.fps));
    bounds << qint64(std::llround(info.duration * info.fps));

    QString clipName = info.path.section('/', -1);
    QString out = QString("TITLE: %1\nFCM: NON-DROP FRAME\n\n").arg(title);
    qint64 record = recordStart;
    int event = 0;
    for (int i = 0; i + 1 < bounds.size(); ++i) {
        const qint64 length = bounds[i + 1] - bounds[i];
        if (length <= 0)
            continue;
        out += QString("%1  AX       V     C        %2 %3 %4 %5\n")
                   .arg(++event, 3, 10, QChar('0'))
                   .arg(timecode(sourceStart + bounds[i], base), timecode(sourceStart + bounds[i + 1], base),
                        timecode(record, base), timecode(record + length, base));
        out += "* FROM CLIP NAME: " + clipName + "\n\n";
        record += length;
    }
    return out;
}

QStringList waveformArguments(const QString &input) {
    return {"-nostdin", "-hide_banner", "-loglevel", "error", "-i", input, "-map", "0:a:0", "-vn", "-sn", "-dn",
            "-ac", "1", "-ar", QString::number(kWaveformRate), "-f", "s16le", "-"};
}

WaveformScan::WaveformScan(qint64 samples, int slices) : m_total(std::max<qint64>(1, samples)), m_peak(slices, 0) {}

void WaveformScan::add(const QByteArray &pcm) {
    const QByteArray data = m_odd + pcm;
    const qsizetype whole = data.size() / 2 * 2;
    m_odd = data.mid(whole);
    const int slices = m_peak.size();
    for (qsizetype i = 0; i < whole; i += 2, ++m_seen) {
        const qint16 v = qint16(quint16(quint8(data[i])) | quint16(quint8(data[i + 1])) << 8);
        const int level = v == -32768 ? 32767 : std::abs(int(v));
        // A file a little longer than its stated length piles into the last slice.
        const int slice = int(std::min<qint64>(slices - 1, m_seen * slices / m_total));
        m_peak[slice] = std::max(m_peak[slice], level);
    }
}

QVariantList WaveformScan::peaks() const {
    const int loudest = m_peak.isEmpty() ? 0 : *std::max_element(m_peak.begin(), m_peak.end());
    QVariantList out;
    out.reserve(m_peak.size());
    for (int p : m_peak)
        out << (loudest > 0 ? std::sqrt(double(p) / loudest) : 0.0);
    return out;
}
