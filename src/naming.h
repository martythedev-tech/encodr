#pragma once

#include <QDateTime>
#include <QRegularExpression>
#include <QString>
#include <QStringList>

// Output file names from one pattern for every conversion. The parts:
//   {name}    the source's name, without its extension
//   {suffix}  the preset's suffix (_review, _proxy…)
//   {preset}  the preset's id (review, prores-422…)
//   {date}    the day the conversion starts, 2026-10-11
//   {time}    the time it starts, 1530
//   {height}  the output picture's height, 1080 (gone for audio, with a
//             "p" right after it and one separator)
//   {n}       001, 002…: the first number not already taken
namespace naming {

const QString kDefault = "{name}{suffix}";

struct Parts {
    QString name;
    QString suffix;
    QString preset;
    QDateTime when;
    int height = 0;     // 0 = no picture
};

// The pattern made safe to name a file with: no folders, no control
// characters, and the default when nothing usable is left.
QString clean(const QString &pattern);
bool numbered(const QString &pattern);
// The name without its extension, with {n} filled in when `n` > 0.
QString expand(const QString &pattern, const Parts &parts, int n = 0);
// The path in `folder` for a new file; with {n}, the first free number.
// Without {n} the name is as it is: the caller adds " (2)" if it's taken.
QString pathFor(const QString &folder, const QString &pattern, const Parts &parts, const QString &extension);
// Matches the file names `pattern` could have given an output of the
// source named `sourceBase` (with any of `suffixes`), with or without a
// " (2)" and with any extension.
QRegularExpression matcher(const QString &pattern, const QString &sourceBase, const QStringList &suffixes);

}
