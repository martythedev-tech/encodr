#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

// An AppImage is just a file: nothing tells the desktop it exists, so it has
// no app-menu entry and the window gets a generic icon (GNOME matches windows
// to .desktop files). This offers to add one, in the user's own data folder,
// and keeps an entry it wrote pointing at the AppImage if that file moves or
// is replaced by a newer one.
class AppMenu : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool offer READ offer NOTIFY changed)

public:
    // `appImage` is the running AppImage ($APPIMAGE; empty when Encodr isn't
    // one), `dataHome` normally ~/.local/share, `dataDirs` the system ones
    // ($XDG_DATA_DIRS). All swappable for tests.
    explicit AppMenu(const QString &appImage = qEnvironmentVariable("APPIMAGE"), const QString &dataHome = {},
                     const QStringList &dataDirs = {}, const QString &configDir = {}, QObject *parent = nullptr);

    bool offer() const { return m_offer; }
    Q_INVOKABLE bool add();
    Q_INVOKABLE void decline();

    // The installed desktop file's text with Exec pointed at `appImage`.
    static QByteArray desktopEntry(const QByteArray &installed, const QString &appImage);

signals:
    void changed();

private:
    QString entryPath() const;
    QString iconPath() const;

    QString m_appImage;
    QString m_dataHome;
    QString m_configDir;
    bool m_offer = false;
};
