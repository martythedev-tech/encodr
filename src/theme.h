#pragma once

#include <QFileSystemWatcher>
#include <QHash>
#include <QObject>
#include <QTimer>

// Follows the Omarchy theme live: the accent plus the handful of palette
// colours Encodr paints with, each with a dark fallback for when Omarchy
// isn't there.
class Theme : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString accent READ accent NOTIFY changed)
    Q_PROPERTY(QString accentForeground READ accentForeground NOTIFY changed)
    Q_PROPERTY(QString background READ background NOTIFY changed)
    Q_PROPERTY(QString surface READ surface NOTIFY changed)
    Q_PROPERTY(QString raised READ raised NOTIFY changed)
    Q_PROPERTY(QString foreground READ foreground NOTIFY changed)
    Q_PROPERTY(QString dim READ dim NOTIFY changed)
    Q_PROPERTY(QString good READ good NOTIFY changed)
    Q_PROPERTY(QString warn READ warn NOTIFY changed)
    Q_PROPERTY(QString bad READ bad NOTIFY changed)
    Q_PROPERTY(QString border READ border NOTIFY changed)
    Q_PROPERTY(QString hover READ hover NOTIFY changed)
    Q_PROPERTY(QString mono READ mono CONSTANT)

public:
    explicit Theme(const QString &currentDirectory = {}, QObject *parent = nullptr);

    QString accent() const { return m_accent; }
    QString accentForeground() const;
    QString background() const { return m_colors.value("background", "#0e0e10"); }
    QString surface() const { return m_colors.value("dark_background", "#16161a"); }
    QString raised() const { return m_colors.value("lighter_background", "#222228"); }
    QString foreground() const { return m_colors.value("foreground", "#d8d8dc"); }
    QString dim() const { return m_colors.value("dark_foreground", "#8a8a94"); }
    QString good() const { return m_colors.value("green", "#7bd88f"); }
    QString warn() const { return m_colors.value("yellow", "#e8c46a"); }
    QString bad() const { return m_colors.value("red", "#f07178"); }
    // Hairlines and hover fills, mixed from the palette so they suit any theme.
    QString border() const { return mix(foreground(), surface(), 0.12); }
    QString hover() const { return mix(foreground(), surface(), 0.07); }
    // Numbers read best in a monospaced face; fontconfig resolves it to the
    // Omarchy terminal font.
    QString mono() const { return "monospace"; }

    static QString mix(const QString &top, const QString &bottom, double amount);

    static QString readAccent(const QString &path);
    static QHash<QString, QString> readColors(const QString &path);

signals:
    void changed();

private:
    void reload();

    QString m_directory;
    QString m_accent = "#FFD60A";
    QHash<QString, QString> m_colors;
    QFileSystemWatcher m_watcher;
    QTimer m_debounce;
};
