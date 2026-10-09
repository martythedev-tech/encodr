#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

// One block of `ffmpeg -progress` output (key=value lines ending in progress=).
struct ProgressSample {
    qint64 frame = 0;
    double fps = 0;
    double bitrateKbps = 0;     // 0 when ffmpeg reports N/A
    qint64 totalSize = 0;
    double outTime = 0;         // seconds of output written
    double speed = 0;           // × real time, 0 when N/A
    qint64 dupFrames = 0;
    qint64 dropFrames = 0;
    double quantizer = -1;
    bool end = false;
};

class ProgressParser {
public:
    // Feed raw bytes as they arrive; returns the blocks completed by them.
    QList<ProgressSample> feed(const QByteArray &bytes);

private:
    QByteArray m_buffer;
    ProgressSample m_current;
};

// Estimates derived from progress, kept apart from the process so they test cleanly.
struct Estimate {
    double fraction = 0;        // 0..1
    double remaining = -1;      // seconds, -1 when unknown
    qint64 projectedSize = 0;   // bytes, 0 when unknown
    double averageSpeed = 0;
};

// recentSpeed is the speed over the last stretch of work; blending it with the
// overall average keeps the time-left figure steady but still responsive when
// the footage gets harder or easier to compress.
Estimate estimate(double duration, double outTime, double activeSeconds, qint64 totalSize, double recentSpeed);
