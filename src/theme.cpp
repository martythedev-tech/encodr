// Theme watching follows Omacut (MIT, David Heinemeier Hansson).
#include "theme.h"

#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <cmath>

Theme::Theme(const QString &directory, QObject *parent)
    : QObject(parent),
      m_directory(directory.isEmpty() ? QDir::homePath() + "/.local/state/omarchy/current" : directory) {
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(80);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] { m_debounce.start(); });
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, [this] { m_debounce.start(); });
    connect(&m_debounce, &QTimer::timeout, this, &Theme::reload);
    reload();
}

QHash<QString, QString> Theme::readColors(const QString &path) {
    QHash<QString, QString> colors;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return colors;
    const QRegularExpression expression(R"re(^\s*([a-z_0-9]+)\s*=\s*["'](#[0-9a-fA-F]{6})["'])re",
                                        QRegularExpression::MultilineOption);
    auto it = expression.globalMatch(QString::fromUtf8(file.readAll()));
    while (it.hasNext()) {
        const auto match = it.next();
        colors.insert(match.captured(1), match.captured(2));
    }
    return colors;
}

QString Theme::readAccent(const QString &path) {
    return readColors(path).value("accent", "#FFD60A");
}

// Black or white, whichever reads on the accent (WCAG relative luminance).
QString Theme::accentForeground() const {
    const QColor c(m_accent);
    auto linear = [](double v) { return v <= .04045 ? v / 12.92 : std::pow((v + .055) / 1.055, 2.4); };
    const double luminance = .2126 * linear(c.redF()) + .7152 * linear(c.greenF()) + .0722 * linear(c.blueF());
    return luminance > .179 ? "black" : "white";
}

QString Theme::mix(const QString &top, const QString &bottom, double amount) {
    const QColor a(top), b(bottom);
    auto blend = [amount](double x, double y) { return x * amount + y * (1 - amount); };
    return QColor::fromRgbF(blend(a.redF(), b.redF()), blend(a.greenF(), b.greenF()), blend(a.blueF(), b.blueF())).name();
}

void Theme::reload() {
    const auto paths = m_watcher.files() + m_watcher.directories();
    if (!paths.isEmpty())
        m_watcher.removePaths(paths);

    // A theme switch replaces symlinks and files, so watch the parents too and
    // re-arm after every change. Watching the nearest existing ancestor means
    // installing Omarchy later works without restarting the app.
    QString ancestor = m_directory;
    while (!QFileInfo::exists(ancestor) && ancestor != "/")
        ancestor = QFileInfo(ancestor).absolutePath();
    QStringList candidates{ancestor, QFileInfo(ancestor).absolutePath(), m_directory,
                           m_directory + "/theme", m_directory + "/theme/colors.toml"};
    candidates.removeDuplicates();
    for (const auto &path : candidates)
        if (QFileInfo::exists(path))
            m_watcher.addPath(path);

    const auto colors = readColors(m_directory + "/theme/colors.toml");
    // Light themes would put dark text on a dark-styled window; keep the
    // dark fallbacks unless the theme says it's dark.
    QHash<QString, QString> usable = colors;
    if (!colors.isEmpty()) {
        QFile file(m_directory + "/theme/colors.toml");
        if (file.open(QIODevice::ReadOnly) && QString::fromUtf8(file.readAll()).contains(QRegularExpression(R"(mode\s*=\s*["']light)")))
            usable = {{"accent", colors.value("accent")}, {"green", colors.value("green")},
                      {"yellow", colors.value("yellow")}, {"red", colors.value("red")}};
    }
    usable.remove("");
    for (auto it = usable.begin(); it != usable.end();)
        it = it.value().isEmpty() ? usable.erase(it) : std::next(it);

    const QString accent = usable.value("accent", "#FFD60A");
    if (accent != m_accent || usable != m_colors) {
        m_accent = accent;
        m_colors = usable;
        emit changed();
    }
}
