#pragma once

#include <QAbstractListModel>
#include <QFileSystemWatcher>
#include <QHash>
#include <QQueue>
#include <QSet>
#include <QTimer>
#include <QVariantList>

// The built-in file browser: one folder at a time, folders first. Media
// files get a thumbnail and a line of details (length, picture, codec), read
// in the background with ffprobe/ffmpeg and cached by path, size and date, so
// a folder of camera files is quick the second time. Files that already have
// an Encodr output beside them are marked, so nothing gets converted twice.
class Browser : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(QString path READ path NOTIFY pathChanged)
    Q_PROPERTY(QString folderName READ folderName NOTIFY pathChanged)
    Q_PROPERTY(bool canGoUp READ canGoUp NOTIFY pathChanged)
    Q_PROPERTY(QVariantList places READ places NOTIFY placesChanged)
    Q_PROPERTY(int mediaCount READ mediaCount NOTIFY countsChanged)
    Q_PROPERTY(int pending READ pending NOTIFY countsChanged)

public:
    enum Role {
        NameRole = Qt::UserRole + 1, PathRole, UrlRole, IsDirRole, IsMediaRole, IsAudioRole, SizeRole,
        ModifiedRole, ThumbRole, DurationRole, DetailsRole, ConvertedRole, ConvertedNameRole, ReadRole
    };

    // `cacheDir` holds thumbnails and details; `mountsFile` lists mounted
    // drives (normally /proc/mounts). Both are swappable for tests.
    explicit Browser(const QString &cacheDir = {}, const QString &mountsFile = {}, QObject *parent = nullptr);
    ~Browser() override;

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString path() const { return m_path; }
    QString folderName() const;
    bool canGoUp() const;
    QVariantList places() const { return m_places; }
    int mediaCount() const;
    int pending() const { return m_queue.size() + m_running; }

    Q_INVOKABLE void open(const QString &path);
    Q_INVOKABLE void up();
    Q_INVOKABLE void refresh();
    Q_INVOKABLE QStringList mediaIn(const QVariantList &paths) const;

    // Output-name suffixes ("_review", "_proxy"…) that mark a file as converted,
    // and folders other than the source's that outputs may land in.
    void setOutputSuffixes(const QStringList &suffixes);
    void setOutputFolders(const QStringList &folders);

    static QString cacheKey(const QString &path, qint64 size, qint64 modifiedMs);

signals:
    void pathChanged();
    void placesChanged();
    void countsChanged();

private:
    struct Entry {
        QString name;
        QString path;
        bool isDir = false;
        bool isMedia = false;
        bool isAudio = false;
        qint64 size = 0;
        qint64 modified = 0;
        QString key;
        QString thumb;
        double duration = 0;
        QString details;
        QString converted;
        bool read = false;
    };

    void list();
    void loadCached(Entry &e) const;
    void markConverted();
    void pump();
    void probe(const QString &key, const QString &path, bool audio);
    void finish(const QString &key);
    void updatePlaces();
    int rowFor(const QString &key) const;

    QString m_cache;
    QString m_mounts;
    QString m_path;
    QVector<Entry> m_entries;
    QVariantList m_places;
    QStringList m_suffixes;
    QStringList m_outputFolders;
    QFileSystemWatcher m_watcher;
    QTimer m_relist;
    QTimer m_placesTimer;
    QQueue<QPair<QString, QString>> m_queue;   // key, path
    QSet<QString> m_queued;
    int m_running = 0;
};
