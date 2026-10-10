#include "appmenu.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

namespace {
const char *kMarker = "X-Encodr-AppImage=true";

QByteArray readAll(const QString &path) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

bool writeAll(const QString &path, const QByteArray &bytes) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    return f.open(QIODevice::WriteOnly | QIODevice::Truncate) && f.write(bytes) == bytes.size();
}
}

AppMenu::AppMenu(const QString &appImage, const QString &dataHome, const QStringList &dataDirs,
                 const QString &configDir, QObject *parent)
    : QObject(parent), m_appImage(appImage),
      m_dataHome(dataHome.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) : dataHome),
      m_configDir(configDir.isEmpty()
                      ? QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/encodr"
                      : configDir) {
    if (m_appImage.isEmpty())
        return;

    // Our own entry: follow the AppImage if it moved or a newer one runs.
    const QByteArray mine = readAll(entryPath());
    if (mine.contains(kMarker)) {
        const QByteArray wanted = desktopEntry(readAll(":/encodr.desktop"), m_appImage);
        if (mine != wanted)
            writeAll(entryPath(), wanted);
        return;
    }
    if (!mine.isEmpty())
        return;    // someone else's encodr.desktop: leave it be

    // Installed properly (the Arch package, say): nothing to offer.
    QStringList dirs = dataDirs;
    if (dirs.isEmpty())
        dirs = qEnvironmentVariable("XDG_DATA_DIRS", "/usr/local/share:/usr/share").split(':', Qt::SkipEmptyParts);
    for (const QString &dir : dirs)
        if (QFileInfo::exists(dir + "/applications/encodr.desktop"))
            return;

    m_offer = !QFileInfo::exists(m_configDir + "/app-menu-declined");
}

QString AppMenu::entryPath() const {
    return m_dataHome + "/applications/encodr.desktop";
}

QString AppMenu::iconPath() const {
    return m_dataHome + "/icons/hicolor/scalable/apps/encodr.svg";
}

QByteArray AppMenu::desktopEntry(const QByteArray &installed, const QString &appImage) {
    QByteArray out;
    for (const QByteArray &line : installed.split('\n')) {
        if (line.startsWith("Exec="))
            out += "Exec=\"" + appImage.toUtf8() + "\" %F\n";
        else if (!line.isEmpty())
            out += line + "\n";
    }
    return out + kMarker + "\n";
}

bool AppMenu::add() {
    const bool ok = writeAll(iconPath(), readAll(":/encodr.svg"))
                    && writeAll(entryPath(), desktopEntry(readAll(":/encodr.desktop"), m_appImage));
    if (ok) {
        m_offer = false;
        emit changed();
    }
    return ok;
}

void AppMenu::decline() {
    writeAll(m_configDir + "/app-menu-declined", {});
    m_offer = false;
    emit changed();
}
