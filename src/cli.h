#pragma once

#include <QString>
#include <QStringList>
#include <QVariantMap>

// Encodr from the command line, for people and for agents: list presets,
// read a file, convert files (with the window's own presets, output folder,
// naming and GPU choice), and check the result. `encodr guide` prints the
// full guide for agents. Nothing here opens a window or needs a display,
// so it also works over SSH.
namespace cli {

// True when the arguments ask for a command-line command (the first one is
// presets, probe, convert or guide) rather than the window.
bool wanted(int argc, char *argv[]);

// Runs the command; returns the process's exit code: 0 everything worked,
// 1 a file failed (or a quality check found a problem), 2 the command was
// wrong, 130 stopped with Ctrl-C.
int run(int argc, char *argv[], const QString &version);

// Parsing, used directly by tests.
// "90", "90.5", "1:30", "1:02:03.5" → seconds; -1 when it isn't a time.
double parseTime(const QString &text);
// "IN-OUT", "IN-" (to the end) or "-OUT" (from the start) → {in, out} in
// seconds, out 0 meaning the end; {-1, -1} when it can't be read.
QPair<double, double> parseTrim(const QString &text);
// "key=value" pairs onto a preset's settings (Preset::toVariant keys). The
// value takes the type the setting already has; returns what's wrong, or
// empty when every pair applied.
QString applySettings(QVariantMap &preset, const QStringList &pairs);

}
