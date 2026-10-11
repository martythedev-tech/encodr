#pragma once

#include <QList>
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

// The shots between cuts as a CMX 3600 edit decision list, on the source's
// own timecode, for Resolve, Premiere or Avid.
QString edlFromCuts(const QList<Moment> &moments, const MediaInfo &info, const QString &title);
