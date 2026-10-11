#include "naming.h"

#include <QDir>
#include <QFileInfo>

namespace naming {
namespace {
const QString kSeparators = "_-. ";

// Takes an empty part out with one separator beside it, so "{date}_{name}"
// without a date doesn't start with "_".
QString dropPart(QString pattern, const QString &token, bool withP = false) {
    const QString t = QRegularExpression::escape(token) + (withP ? "p?" : "");
    const QString sep = "[" + QRegularExpression::escape(kSeparators) + "]";
    pattern.replace(QRegularExpression("^" + t + sep + "?"), "");
    pattern.replace(QRegularExpression(sep + "?" + t), "");
    return pattern;
}

// The pattern with the parts that come out empty for these parts removed.
QString withoutEmpty(QString pattern, const Parts &parts) {
    if (parts.height <= 0)
        pattern = dropPart(pattern, "{height}", true);
    if (parts.suffix.isEmpty())
        pattern = dropPart(pattern, "{suffix}");
    if (parts.preset.isEmpty())
        pattern = dropPart(pattern, "{preset}");
    return pattern;
}
}

QString clean(const QString &pattern) {
    QString p = pattern;
    p.remove(QRegularExpression("[/\\\\\\x00-\\x1f]"));
    p = p.trimmed();
    // Something has to tell the outputs of different sources apart.
    if (!p.contains("{name}"))
        p = p.isEmpty() ? kDefault : "{name}" + (p.startsWith('_') || p.startsWith('-') ? p : "_" + p);
    return p;
}

bool numbered(const QString &pattern) {
    return pattern.contains("{n}");
}

QString expand(const QString &pattern, const Parts &parts, int n) {
    QString out = withoutEmpty(clean(pattern), parts);
    const QDateTime when = parts.when.isValid() ? parts.when : QDateTime::currentDateTime();
    out.replace("{suffix}", parts.suffix);
    out.replace("{preset}", parts.preset);
    out.replace("{date}", when.toString("yyyy-MM-dd"));
    out.replace("{time}", when.toString("HHmm"));
    out.replace("{height}", QString::number(parts.height));
    if (n > 0)
        out.replace("{n}", QString("%1").arg(n, 3, 10, QChar('0')));
    out.replace("{name}", parts.name);    // last: the source's own name may hold braces
    return out;
}

QString pathFor(const QString &folder, const QString &pattern, const Parts &parts, const QString &extension) {
    const QDir dir(folder);
    if (!numbered(clean(pattern)))
        return dir.filePath(expand(pattern, parts) + "." + extension);
    for (int n = 1; n < 100000; ++n) {
        const QString path = dir.filePath(expand(pattern, parts, n) + "." + extension);
        if (!QFileInfo::exists(path))
            return path;
    }
    return dir.filePath(expand(pattern, parts, 1) + "." + extension);
}

QRegularExpression matcher(const QString &pattern, const QString &sourceBase, const QStringList &suffixes) {
    QStringList escaped;
    for (const QString &s : suffixes)
        if (!s.isEmpty())
            escaped << QRegularExpression::escape(s);
    const QString suffix = "(?:" + escaped.join('|') + ")";
    auto body = [&](const QString &p) {
        static const QRegularExpression parts("\\{(name|suffix|preset|date|time|height|n)\\}");
        QString re;
        qsizetype at = 0;
        for (auto it = parts.globalMatch(p); it.hasNext();) {
            const auto m = it.next();
            re += QRegularExpression::escape(p.mid(at, m.capturedStart() - at));
            const QString part = m.captured(1);
            re += part == "name" ? QRegularExpression::escape(sourceBase)
                  : part == "suffix" ? (escaped.isEmpty() ? QString() : suffix)
                  : part == "preset" ? QString("[A-Za-z0-9_-]+")
                  : part == "date" ? QString("\\d{4}-\\d{2}-\\d{2}")
                  : part == "time" ? QString("\\d{4}")
                  : part == "height" ? QString("\\d+")
                  : QString("\\d{3,}");
            at = m.capturedEnd();
        }
        return re + QRegularExpression::escape(p.mid(at));
    };
    // Audio outputs have no {height}: their names leave it out.
    const QString p = clean(pattern);
    Parts audio;
    audio.suffix = audio.preset = "x";
    QStringList alternatives{body(p)};
    const QString noHeight = withoutEmpty(p, audio);
    if (noHeight != p)
        alternatives << body(noHeight);
    return QRegularExpression("^(?:" + alternatives.join('|') + ")(?: \\(\\d+\\))?\\.[^.]+$");
}

}
