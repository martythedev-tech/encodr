#pragma once

#include <QList>
#include <QMargins>
#include <QString>
#include <QStringList>
#include <QVariantList>

#include "mediainfo.h"

// A whole-file check for what a quick look misses: black frames, frozen
// pictures, silence and the cuts between shots. One ffmpeg pass, on request
// (it decodes every frame), on a small copy of the picture.
struct Moment {
    QString kind;       // black, freeze, silence, cut
    double start = 0;
    double end = 0;     // a cut has start == end
};

QStringList analysisArguments(const MediaInfo &info, const QString &input);
// What the detectors wrote to ffmpeg's log, as moments in time order.
QList<Moment> parseAnalysis(const QByteArray &log, double duration);
// Findings for the source inspector; black and silence at the very start
// or end (fades, slates) are normal and only mentioned.
QList<Issue> analysisIssues(const QList<Moment> &moments, double duration);
QVariantList momentsToVariant(const QList<Moment> &moments);

// Black bars: cropdetect at `samples` points across the file (fast seeks,
// half a second each), all in one ffmpeg run.
QStringList barArguments(const MediaInfo &info, const QString &input, int samples = 8);
// The bars to take off, from what cropdetect saw: the largest picture any
// sample found (a dark scene mustn't crop real picture), on even pixels.
// Empty margins when there are no bars worth removing.
QMargins parseBars(const QByteArray &log, const MediaInfo &info);

// The waveform under the timeline: the first audio track as mono 16-bit
// PCM at a low rate on stdout, read in pieces as ffmpeg writes it.
constexpr int kWaveformRate = 4000;
QStringList waveformArguments(const QString &input);
class WaveformScan {
public:
    // `samples`: how many the whole file holds (its length × the rate), so
    // each lands in its slice of the timeline as it arrives.
    WaveformScan(qint64 samples, int slices);
    void add(const QByteArray &pcm);
    // The loudest moment of each slice, 0 to 1 against the file's own
    // loudest (a quiet recording still fills the lane), square-rooted so
    // quiet passages still show. All 0 for silence.
    QVariantList peaks() const;

private:
    qint64 m_total;
    qint64 m_seen = 0;
    QByteArray m_odd;    // half a sample left over from the last piece
    QList<int> m_peak;
};

// The shots between cuts as a CMX 3600 edit decision list, on the source's
// own timecode, for Resolve, Premiere or Avid.
QString edlFromCuts(const QList<Moment> &moments, const MediaInfo &info, const QString &title);
