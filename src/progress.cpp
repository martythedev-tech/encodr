#include "progress.h"

#include <algorithm>

namespace {
double leadingNumber(const QByteArray &value) {
    // "1234.5kbits/s", "1.23x", "N/A"
    QByteArray digits;
    for (char c : value) {
        if ((c >= '0' && c <= '9') || c == '.' || c == '-')
            digits.append(c);
        else
            break;
    }
    return digits.isEmpty() ? 0 : digits.toDouble();
}
}

QList<ProgressSample> ProgressParser::feed(const QByteArray &bytes) {
    QList<ProgressSample> done;
    m_buffer.append(bytes);
    qsizetype newline;
    while ((newline = m_buffer.indexOf('\n')) >= 0) {
        const QByteArray line = m_buffer.left(newline).trimmed();
        m_buffer.remove(0, newline + 1);
        const qsizetype eq = line.indexOf('=');
        if (eq <= 0)
            continue;
        const QByteArray key = line.left(eq);
        const QByteArray value = line.mid(eq + 1).trimmed();

        if (key == "frame")
            m_current.frame = value.toLongLong();
        else if (key == "fps")
            m_current.fps = value.toDouble();
        else if (key == "bitrate")
            m_current.bitrateKbps = leadingNumber(value);
        else if (key == "total_size")
            m_current.totalSize = value.toLongLong();
        else if (key == "out_time_us")
            m_current.outTime = value == "N/A" ? m_current.outTime : std::max(0.0, value.toLongLong() / 1e6);
        else if (key == "speed")
            m_current.speed = leadingNumber(value);
        else if (key == "dup_frames")
            m_current.dupFrames = value.toLongLong();
        else if (key == "drop_frames")
            m_current.dropFrames = value.toLongLong();
        else if (key.startsWith("stream_") && key.endsWith("_q"))
            m_current.quantizer = value.toDouble();
        else if (key == "progress") {
            m_current.end = value == "end";
            done.append(m_current);
        }
    }
    return done;
}

Estimate estimate(double duration, double outTime, double activeSeconds, qint64 totalSize, double recentSpeed) {
    Estimate e;
    if (duration <= 0)
        return e;
    e.fraction = std::clamp(outTime / duration, 0.0, 1.0);
    if (activeSeconds > 0)
        e.averageSpeed = outTime / activeSeconds;

    // Too early, the numbers swing wildly; wait for a little real work.
    if (activeSeconds >= 3 && outTime > 0) {
        double speed = e.averageSpeed;
        if (recentSpeed > 0)
            speed = 0.6 * recentSpeed + 0.4 * e.averageSpeed;
        if (speed > 0)
            e.remaining = std::max(0.0, (duration - outTime) / speed);
    }
    if (e.fraction >= 0.01 && totalSize > 0)
        e.projectedSize = qint64(totalSize / e.fraction);
    return e;
}
