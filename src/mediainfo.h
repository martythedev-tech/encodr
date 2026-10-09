#pragma once

#include <QList>
#include <QString>
#include <QVariantMap>
#include <cmath>

struct AudioTrack {
    int index = 0;          // nth audio stream (0:a:N), not the absolute stream index
    QString codec;
    int sampleRate = 0;
    int channels = 0;
    QString layout;
    int bits = 0;
    qint64 bitrate = 0;
    // Filled in later by a level scan; NaN until then.
    double meanDb = NAN;
    double peakDb = NAN;
};

struct MediaInfo {
    bool valid = false;
    QString error;

    QString path;
    QString container;
    qint64 size = 0;
    double duration = 0;
    qint64 bitrate = 0;
    QString timecode;
    QString creationTime;

    bool hasVideo = false;
    QString videoCodec;
    QString videoCodecLong;
    QString videoProfile;
    QString pixelFormat;
    int width = 0;
    int height = 0;
    double fps = 0;
    QString fpsRational;
    QString fieldOrder;
    int bitDepth = 0;
    qint64 videoBitrate = 0;
    QString colorPrimaries;
    QString colorTransfer;
    QString colorSpace;

    QList<AudioTrack> audio;

    bool interlaced() const;
    // Tags the decoder can't convert from: "reserved" stops ffmpeg's scaler cold.
    bool colourTagsBroken() const;
    bool splitMono() const;
    qint64 estimatedFrames() const;
    QString summary() const;
    QVariantMap toVariant() const;
};

MediaInfo parseProbe(const QByteArray &json, const QString &path);

// Plain-language findings about a source, shown before converting.
// level is "fix" (handled automatically), "warn" or "info".
struct Issue {
    QString level;
    QString title;
    QString detail;
};

QList<Issue> inspect(const MediaInfo &info);
QVariantList issuesToVariant(const QList<Issue> &issues);
