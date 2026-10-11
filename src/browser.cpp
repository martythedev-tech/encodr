#include "browser.h"
#include "naming.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUrl>

#include "mediainfo.h"
#include "preset.h"

namespace {
constexpr int kParallel = 2;
constexpr int kThumbHeight = 108;

QString clock(double seconds) {
    const qint64 s = qRound64(seconds);
    if (s >= 3600)
        return QString("%1:%2:%3").arg(s / 3600).arg(s % 3600 / 60, 2, 10, QChar('0')).arg(s % 60, 2, 10, QChar('0'));
    return QString("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QChar('0'));
}

QString codecName(const QString &codec, const QString &profile) {
    if (codec == "prores")
        return profile.isEmpty() ? "ProRes" : "ProRes " + profile;
    if (codec == "h264")
        return "H.264";
    if (codec == "hevc")
        return "HEVC";
    if (codec == "dnxhd")
        return "DNxHD";
    if (codec.startsWith("pcm_"))
        return "PCM";
    return codec.toUpper();
}

// One line that tells files apart at a glance: length, picture, codec, rate.
QString describe(const MediaInfo &info) {
    QStringList parts;
    if (info.duration > 0)
        parts << clock(info.duration);
    if (info.hasVideo) {
        parts << QString("%1×%2").arg(info.width).arg(info.height);
        parts << codecName(info.videoCodec, info.videoProfile);
        if (info.fps > 0)
            parts << QString::number(info.fps, 'f', std::abs(info.fps - qRound(info.fps)) < 0.01 ? 0 : 2) + (info.interlaced() ? "i" : "p");
    } else if (!info.audio.isEmpty()) {
        const auto &a = info.audio.first();
        parts << codecName(a.codec, {}) + (a.bits ? QString(" %1-bit").arg(a.bits) : QString());
        parts << (a.channels == 1 ? "mono" : a.channels == 2 ? "stereo" : QString("%1 ch").arg(a.channels));
    }
    return parts.join(" · ");
}

// Natural order ("Card 2" before "Card 10") that doesn't depend on the
// system locale: QCollator's numeric mode quietly does nothing under the C
// locale. Digit runs compare by value, the rest case-insensitively.
int naturalCompare(const QString &a, const QString &b) {
    qsizetype i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
        if (a[i].isDigit() && b[j].isDigit()) {
            qsizetype ei = i, ej = j;
            while (ei < a.size() && a[ei].isDigit())
                ++ei;
            while (ej < b.size() && b[ej].isDigit())
                ++ej;
            // Compare by value without overflowing: strip leading zeros, then length, then digits.
            QStringView da = QStringView(a).mid(i, ei - i), db = QStringView(b).mid(j, ej - j);
            while (da.size() > 1 && da.front() == u'0')
                da = da.mid(1);
            while (db.size() > 1 && db.front() == u'0')
                db = db.mid(1);
            if (da.size() != db.size())
                return da.size() < db.size() ? -1 : 1;
            if (const int c = da.compare(db); c != 0)
                return c;
            i = ei;
            j = ej;
            continue;
        }
        const QChar ca = a[i].toCaseFolded(), cb = b[j].toCaseFolded();
        if (ca != cb)
            return ca < cb ? -1 : 1;
        ++i;
        ++j;
    }
    if (i < a.size() || j < b.size())
        return i < a.size() ? 1 : -1;
    return a.compare(b);
}

// Mount points are written with octal escapes for spaces and the like.
QString unescapeMount(QString s) {
    static const QRegularExpression octal("\\\\([0-7]{3})");
    QRegularExpressionMatch m;
    while ((m = octal.match(s)).hasMatch())
        s.replace(m.capturedStart(), 4, QChar(m.captured(1).toInt(nullptr, 8)));
    return s;
}
}

Browser::Browser(const QString &cacheDir, const QString &mountsFile, QObject *parent)
    : QAbstractListModel(parent),
      m_cache(cacheDir.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + "/encodr/browser" : cacheDir),
      m_mounts(mountsFile.isEmpty() ? "/proc/mounts" : mountsFile) {
    QDir().mkpath(m_cache);
    m_relist.setSingleShot(true);
    m_relist.setInterval(300);
    connect(&m_relist, &QTimer::timeout, this, &Browser::list);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] { m_relist.start(); });
    // Drives come and go; /proc/mounts can't be watched, but it's cheap to read.
    m_placesTimer.setInterval(4000);
    connect(&m_placesTimer, &QTimer::timeout, this, &Browser::updatePlaces);
    m_placesTimer.start();
    updatePlaces();
}

Browser::~Browser() {
    for (QProcess *p : findChildren<QProcess *>()) {
        p->disconnect();
        p->kill();
        p->waitForFinished(1000);
    }
}

QString Browser::cacheKey(const QString &path, qint64 size, qint64 modifiedMs) {
    const QByteArray id = QString("%1|%2|%3").arg(path).arg(size).arg(modifiedMs).toUtf8();
    return QString::fromLatin1(QCryptographicHash::hash(id, QCryptographicHash::Sha1).toHex().left(24));
}

int Browser::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : int(m_entries.size());
}

QHash<int, QByteArray> Browser::roleNames() const {
    return {{NameRole, "name"}, {PathRole, "path"}, {UrlRole, "url"}, {IsDirRole, "isDir"}, {IsMediaRole, "isMedia"},
            {IsAudioRole, "isAudio"}, {SizeRole, "size"}, {ModifiedRole, "modified"}, {ThumbRole, "thumb"},
            {DurationRole, "duration"}, {DetailsRole, "details"}, {ConvertedRole, "converted"},
            {ConvertedNameRole, "convertedName"}, {ReadRole, "read"}};
}

QVariant Browser::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() >= m_entries.size())
        return {};
    const Entry &e = m_entries[index.row()];
    switch (role) {
    case NameRole: return e.name;
    case PathRole: return e.path;
    case UrlRole: return QUrl::fromLocalFile(e.path);
    case IsDirRole: return e.isDir;
    case IsMediaRole: return e.isMedia;
    case IsAudioRole: return e.isAudio;
    case SizeRole: return e.size;
    case ModifiedRole: return QDateTime::fromMSecsSinceEpoch(e.modified);
    case ThumbRole: return e.thumb.isEmpty() ? QString() : QUrl::fromLocalFile(e.thumb).toString();
    case DurationRole: return e.duration;
    case DetailsRole: return e.details;
    case ConvertedRole: return !e.converted.isEmpty();
    case ConvertedNameRole: return e.converted;
    case ReadRole: return e.read;
    }
    return {};
}

QString Browser::folderName() const {
    if (m_path == QDir::homePath())
        return "Home";
    if (m_path == "/")
        return "Computer";
    return QFileInfo(m_path).fileName();
}

bool Browser::canGoUp() const {
    return !m_path.isEmpty() && m_path != "/";
}

int Browser::mediaCount() const {
    int n = 0;
    for (const Entry &e : m_entries)
        n += e.isMedia;
    return n;
}

void Browser::open(const QString &path) {
    const QString clean = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    if (!QFileInfo(clean).isDir())
        return;
    if (!m_path.isEmpty())
        m_watcher.removePath(m_path);
    m_path = clean;
    m_watcher.addPath(m_path);
    // Work queued for the folder we left is no longer wanted.
    m_queue.clear();
    m_queued.clear();
    beginResetModel();
    m_entries.clear();
    endResetModel();
    list();
    emit pathChanged();
}

void Browser::up() {
    if (canGoUp())
        open(QFileInfo(m_path).absolutePath());
}

void Browser::refresh() {
    list();
}

QStringList Browser::mediaIn(const QVariantList &paths) const {
    QStringList out;
    for (const QVariant &v : paths)
        if (isMediaFile(v.toString()))
            out << v.toString();
    return out;
}

void Browser::setOutputPatterns(const QStringList &patterns) {
    if (patterns == m_patterns)
        return;
    m_patterns = patterns;
    markConverted();
}

void Browser::setOutputSuffixes(const QStringList &suffixes) {
    m_suffixes = suffixes;
    markConverted();
}

void Browser::setOutputFolders(const QStringList &folders) {
    m_outputFolders = folders;
    markConverted();
}

void Browser::list() {
    QDir dir(m_path);
    const QFileInfoList infos = dir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Readable);
    QVector<Entry> fresh;
    for (const QFileInfo &fi : infos) {
        if (fi.fileName().startsWith('.'))
            continue;
        Entry e;
        e.name = fi.fileName();
        e.path = fi.absoluteFilePath();
        e.isDir = fi.isDir();
        e.isMedia = !e.isDir && isMediaFile(e.name);
        e.isAudio = e.isMedia && isAudioFile(e.name);
        e.size = e.isDir ? 0 : fi.size();
        e.modified = fi.lastModified().toMSecsSinceEpoch();
        if (e.isMedia) {
            e.key = cacheKey(e.path, e.size, e.modified);
            loadCached(e);
        }
        fresh.append(e);
    }
    std::sort(fresh.begin(), fresh.end(), [](const Entry &a, const Entry &b) {
        if (a.isDir != b.isDir)
            return a.isDir;
        // By name without the extension, so an original sits just above its
        // converted copies (clip.mov, then clip_review.mp4).
        const int byStem = naturalCompare(QFileInfo(a.name).completeBaseName(), QFileInfo(b.name).completeBaseName());
        return byStem != 0 ? byStem < 0 : naturalCompare(a.name, b.name) < 0;
    });

    // The same files as before (an output being written, say) only need their
    // marks refreshed; a reset would throw the list back to the top.
    bool same = fresh.size() == m_entries.size();
    for (int i = 0; same && i < fresh.size(); ++i)
        same = fresh[i].path == m_entries[i].path && fresh[i].key == m_entries[i].key;
    if (!same) {
        beginResetModel();
        m_entries = fresh;
        endResetModel();
    }
    markConverted();

    for (const Entry &e : std::as_const(m_entries))
        if (e.isMedia && !e.read && !m_queued.contains(e.key)) {
            m_queue.enqueue({e.key, e.path});
            m_queued.insert(e.key);
        }
    pump();
    emit countsChanged();
}

void Browser::loadCached(Entry &e) const {
    QFile f(m_cache + "/" + e.key + ".json");
    if (!f.open(QIODevice::ReadOnly))
        return;
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    e.read = true;
    e.duration = o.value("duration").toDouble();
    e.details = o.value("details").toString();
    const QString thumb = m_cache + "/" + e.key + ".jpg";
    if (QFileInfo::exists(thumb))
        e.thumb = thumb;
}

void Browser::markConverted() {
    QStringList here;
    for (const Entry &e : std::as_const(m_entries))
        here << e.name;
    // Each output folder is listed once, not once per file.
    QList<QStringList> elsewhere;
    for (const QString &folder : std::as_const(m_outputFolders))
        elsewhere << (QDir(folder) == QDir(m_path) ? QStringList() : QDir(folder).entryList(QDir::Files));
    const QStringList patterns = m_patterns.isEmpty() ? QStringList{naming::kDefault} : m_patterns;
    for (int i = 0; i < m_entries.size(); ++i) {
        Entry &e = m_entries[i];
        if (!e.isMedia)
            continue;
        const QString base = QFileInfo(e.name).completeBaseName();
        QString found;
        for (const QString &pattern : patterns) {
            const QRegularExpression output = naming::matcher(pattern, base, m_suffixes);
            for (const QString &name : std::as_const(here))
                if (name != e.name && output.match(name).hasMatch()) {
                    found = name;
                    break;
                }
            for (qsizetype f = 0; found.isEmpty() && f < elsewhere.size(); ++f)
                for (const QString &name : std::as_const(elsewhere[f]))
                    if (output.match(name).hasMatch()) {
                        found = name;
                        break;
                    }
            if (!found.isEmpty())
                break;
        }
        if (found != e.converted) {
            e.converted = found;
            emit dataChanged(index(i), index(i), {ConvertedRole, ConvertedNameRole});
        }
    }
}

void Browser::pump() {
    while (m_running < kParallel && !m_queue.isEmpty()) {
        const auto [key, path] = m_queue.dequeue();
        ++m_running;
        probe(key, path, isAudioFile(path));
    }
    emit countsChanged();
}

void Browser::probe(const QString &key, const QString &path, bool audio) {
    auto *ffprobe = new QProcess(this);
    connect(ffprobe, &QProcess::finished, this, [=, this](int code, QProcess::ExitStatus status) {
        ffprobe->deleteLater();
        const MediaInfo info = code == 0 && status == QProcess::NormalExit
                                   ? parseProbe(ffprobe->readAllStandardOutput(), path) : MediaInfo();
        QJsonObject o{{"duration", info.duration}, {"details", info.valid ? describe(info) : "Can't read this file"}};
        auto save = [=, this] {
            {
                // Closed (and flushed) before finish() reads it back.
                QFile f(m_cache + "/" + key + ".json");
                if (f.open(QIODevice::WriteOnly))
                    f.write(QJsonDocument(o).toJson(QJsonDocument::Compact));
            }
            finish(key);
        };
        if (!info.valid || !info.hasVideo || audio) {
            save();
            return;
        }
        // Written beside the cache and renamed, so a half-made picture is never shown.
        const QString part = m_cache + "/" + key + ".part.jpg";
        auto *ffmpeg = new QProcess(this);
        connect(ffmpeg, &QProcess::finished, this, [=, this](int, QProcess::ExitStatus) {
            ffmpeg->deleteLater();
            if (QFileInfo(part).size() > 0)
                QFile::rename(part, m_cache + "/" + key + ".jpg");
            QFile::remove(part);
            save();
        });
        ffmpeg->start("ffmpeg", posterArguments(info, path, std::min(info.duration * 0.1, 60.0), part, kThumbHeight));
    });
    connect(ffprobe, &QProcess::errorOccurred, this, [=, this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) {
            ffprobe->deleteLater();
            finish(key);
        }
    });
    ffprobe->start("ffprobe", {"-v", "error", "-print_format", "json", "-show_format", "-show_streams", path});
}

void Browser::finish(const QString &key) {
    --m_running;
    m_queued.remove(key);
    const int row = rowFor(key);
    if (row >= 0) {
        loadCached(m_entries[row]);
        m_entries[row].read = true;
        emit dataChanged(index(row), index(row), {ThumbRole, DurationRole, DetailsRole, ReadRole});
    }
    pump();
}

int Browser::rowFor(const QString &key) const {
    for (int i = 0; i < m_entries.size(); ++i)
        if (m_entries[i].key == key)
            return i;
    return -1;
}

void Browser::updatePlaces() {
    QVariantList places;
    auto add = [&](const QString &name, const QString &path, const QString &kind) {
        if (QFileInfo(path).isDir())
            places.append(QVariantMap{{"name", name}, {"path", QDir::cleanPath(path)}, {"kind", kind}});
    };
    add("Home", QDir::homePath(), "home");
    add("Videos", QStandardPaths::writableLocation(QStandardPaths::MoviesLocation), "folder");
    add("Downloads", QStandardPaths::writableLocation(QStandardPaths::DownloadLocation), "folder");
    add("Documents", QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation), "folder");

    // Removable drives, plus FUSE mounts in the home folder (a Mac drive
    // through apfs-fuse, a phone).
    QFile mounts(m_mounts);
    if (mounts.open(QIODevice::ReadOnly)) {
        const QString home = QDir::homePath() + "/";
        for (const QByteArray &line : mounts.readAll().split('\n')) {
            const auto f = line.split(' ');
            if (f.size() < 3)
                continue;
            const QString where = unescapeMount(QString::fromUtf8(f[1]));
            const QString type = QString::fromUtf8(f[2]);
            const bool removable = where.startsWith("/run/media/") || where.startsWith("/media/") || where.startsWith("/mnt/");
            const bool homeFuse = where.startsWith(home) && type.startsWith("fuse") && !type.contains("portal")
                                  && !type.contains("gvfs") && !type.contains("doc");
            if (removable || homeFuse)
                add(QFileInfo(where).fileName(), where, "drive");
        }
    }
    add("Computer", "/", "computer");

    if (places != m_places) {
        m_places = places;
        emit placesChanged();
    }
}
