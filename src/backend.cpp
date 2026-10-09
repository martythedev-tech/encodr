#include "backend.h"

#include <QClipboard>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>

#include "filepicker.h"

namespace {
const QStringList kMediaExtensions = {"mov", "mp4", "m4v", "mkv", "mxf", "avi", "mts", "m2ts", "ts", "mpg", "mpeg",
                                      "webm", "dv", "wav", "aif", "aiff", "flac", "mp3", "m4a"};

QString slug(const QString &name) {
    QString s = name.toLower();
    s.replace(QRegularExpression("[^a-z0-9]+"), "-");
    s.remove(QRegularExpression("^-+|-+$"));
    return s.isEmpty() ? "preset" : s.left(40);
}

QJsonObject readJson(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(f.readAll()).object();
}

void writeJson(const QString &path, const QJsonObject &object) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return;
    f.write(QJsonDocument(object).toJson());
    f.commit();
}
}

Backend::Backend(FilePicker *picker, const QString &configDir, Telemetry *telemetry, QObject *parent)
    : QObject(parent), m_picker(picker), m_telemetry(telemetry) {
    m_configDir = configDir.isEmpty()
        ? QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/encodr"
        : configDir;
    if (!m_telemetry) {
        m_telemetry = new Telemetry({}, {}, this);
        m_ownsTelemetry = true;
    }
    connect(m_telemetry, &Telemetry::updated, this, &Backend::onTick);

    if (m_picker) {
        connect(m_picker, &FilePicker::mediaSelected, this, [this](const QList<QUrl> &urls) {
            QStringList paths;
            for (const auto &url : urls)
                paths << url.toLocalFile();
            addPaths(paths);
        });
        connect(m_picker, &FilePicker::folderSelected, this, [this](const QUrl &url) {
            m_outputMode = "folder";
            m_outputFolder = url.toLocalFile();
            for (Job *job : m_jobs)
                if (job->isWaiting())
                    job->setOutputFolder(folderFor(job->inputPath()));
            saveConfig();
            emit settingsChanged();
        });
        connect(m_picker, &FilePicker::failed, this, &Backend::setNotice);
    }
    loadConfig();
}

Backend::~Backend() {
    setInhibit(false);
    for (const auto &r : m_undo + m_redo)
        delete r.job;
}

QList<QObject *> Backend::jobs() const {
    QList<QObject *> list;
    for (Job *job : m_jobs)
        list.append(job);
    return list;
}

Job *Backend::jobAt(int index) const {
    return index >= 0 && index < m_jobs.size() ? m_jobs[index] : nullptr;
}

void Backend::setSelectedIndex(int index) {
    if (m_jobs.isEmpty())
        index = -1;
    else
        index = std::clamp(index, 0, int(m_jobs.size()) - 1);
    if (index == m_selected)
        return;
    m_selected = index;
    emit selectionChanged();
}

QObject *Backend::selectedJob() const {
    return jobAt(m_selected);
}

QVariantList Backend::presets() const {
    QVariantList list;
    for (const auto &p : builtinPresets())
        list.append(p.toVariant());
    for (const auto &p : m_userPresets)
        list.append(p.toVariant());
    return list;
}

Preset Backend::presetById(const QString &id) const {
    for (const auto &p : m_userPresets)
        if (p.id == id)
            return p;
    const auto builtins = builtinPresets();
    for (const auto &p : builtins)
        if (p.id == id)
            return p;
    return builtins.first();
}

QString Backend::folderFor(const QString &input) const {
    if (m_outputMode == "folder" && !m_outputFolder.isEmpty())
        return m_outputFolder;
    const QString source = QFileInfo(input).absolutePath();
    // Read-only sources (a camera card, a Mac drive mounted read-only) can't
    // take the output, so it goes to Videos instead.
    if (QFileInfo(source).isWritable())
        return source;
    return QStandardPaths::writableLocation(QStandardPaths::MoviesLocation) + "/Encodr";
}

void Backend::addFilesDialog() {
    if (m_picker)
        m_picker->openMedia();
}

void Backend::chooseOutputFolder() {
    if (m_picker)
        m_picker->chooseFolder(m_outputFolder.isEmpty() ? QDir::homePath() : m_outputFolder);
}

void Backend::useSourceFolders() {
    m_outputMode = "beside";
    for (Job *job : m_jobs)
        if (job->isWaiting())
            job->setOutputFolder(folderFor(job->inputPath()));
    saveConfig();
    emit settingsChanged();
}

void Backend::addUrls(const QVariantList &urls) {
    QStringList paths;
    for (const auto &v : urls) {
        const QUrl url = v.canConvert<QUrl>() ? v.toUrl() : QUrl(v.toString());
        paths << (url.isLocalFile() ? url.toLocalFile() : v.toString());
    }
    addPaths(paths);
}

void Backend::addPaths(const QStringList &paths) {
    QStringList files;
    for (const auto &path : paths) {
        const QFileInfo fi(path);
        if (fi.isDir()) {
            const auto entries = QDir(path).entryInfoList(QDir::Files | QDir::Readable, QDir::Name);
            for (const auto &e : entries)
                if (kMediaExtensions.contains(e.suffix().toLower()) && !e.fileName().startsWith('.'))
                    files << e.absoluteFilePath();
        } else if (fi.isFile()) {
            files << fi.absoluteFilePath();
        } else if (!path.isEmpty()) {
            setNotice(QString("Can't open %1 — it doesn't exist or isn't readable.").arg(path));
        }
    }
    int skipped = 0;
    for (const auto &file : files) {
        bool queued = false;
        for (Job *job : m_jobs)
            if (job->inputPath() == file && !job->isFinished())
                queued = true;
        if (queued)
            ++skipped;
        else
            addJob(file);
    }
    if (skipped > 0)
        setNotice(skipped == 1 ? "That file is already in the queue." : QString("%1 files were already in the queue.").arg(skipped));
}

void Backend::addJob(const QString &path) {
    auto *job = new Job(m_nextId++, path, this);
    job->setPreset(presetById(m_defaultPreset));
    job->setOutputFolder(folderFor(path));
    connect(job, &Job::stateChanged, this, [this, job] {
        if (job->state() == "ready" && m_running && !hasActiveWork())
            startNext();
        emit queueChanged();
    });
    connect(job, &Job::finished, this, [this, job](bool ok) { onJobFinished(job, ok); });
    m_jobs.append(job);
    emit jobsChanged();
    if (m_selected < 0)
        setSelectedIndex(0);
    emit queueChanged();
    job->probe();
}

bool Backend::paused() const {
    for (Job *job : m_jobs)
        if (job->state() == "paused")
            return true;
    return false;
}

bool Backend::hasActiveWork() const {
    for (Job *job : m_jobs)
        if (job->isActive())
            return true;
    return false;
}

int Backend::waitingCount() const {
    int n = 0;
    for (Job *job : m_jobs)
        if (job->isWaiting() || job->state() == "probing")
            ++n;
    return n;
}

int Backend::doneCount() const {
    int n = 0;
    for (Job *job : m_jobs)
        if (job->state() == "done")
            ++n;
    return n;
}

double Backend::queuePercent() const {
    double total = 0, done = 0;
    for (Job *job : m_jobs) {
        const QString s = job->state();
        if (s == "failed" || s == "cancelled" || s == "probing")
            continue;
        const double d = std::max(1.0, job->info().duration);
        total += d;
        done += d * job->percent() / 100.0;
    }
    return total > 0 ? 100.0 * done / total : 0;
}

double Backend::queueRemaining() const {
    double speed = 0;
    for (Job *job : m_jobs)
        if (job->isActive() || job->state() == "done")
            speed = job->averageSpeed() > 0 ? job->averageSpeed() : speed;
    if (speed <= 0)
        speed = 1.0;
    double remaining = 0;
    bool any = false;
    for (Job *job : m_jobs) {
        if (job->isActive()) {
            any = true;
            remaining += job->remaining() >= 0 ? job->remaining()
                                               : job->info().duration * (1 - job->percent() / 100.0) / speed;
        } else if (job->isWaiting()) {
            any = true;
            remaining += job->info().duration / speed;
        }
    }
    return any ? remaining : -1;
}

QDateTime Backend::queueFinishAt() const {
    const double r = queueRemaining();
    if (r < 0 || !m_running || paused())
        return {};
    return QDateTime::currentDateTime().addMSecs(qint64(r * 1000));
}

void Backend::toggleQueue() {
    for (Job *job : m_jobs) {
        if (job->state() == "running") {
            job->pause();
            // A paused conversion uses no CPU, so let the machine sleep (and
            // the lid close it) as usual; it carries on after waking.
            setInhibit(false);
            emit queueChanged();
            return;
        }
        if (job->state() == "paused") {
            job->resume();
            setInhibit(true);
            emit queueChanged();
            return;
        }
    }
    startQueue();
}

void Backend::startQueue() {
    if (waitingCount() == 0) {
        setNotice(m_jobs.isEmpty() ? "Add files first — press O or drop them on the window."
                                   : "Nothing is waiting. Retry a file (R) or add more.");
        return;
    }
    m_running = true;
    setInhibit(true);
    emit queueChanged();
    startNext();
}

void Backend::startNext() {
    if (hasActiveWork())
        return;
    for (int i = 0; i < m_jobs.size(); ++i) {
        Job *job = m_jobs[i];
        if (!job->isWaiting())
            continue;
        if (!presetProblem(job->presetValue()).isEmpty())
            continue;
        if (job->start()) {
            Job *selected = jobAt(m_selected);
            if (!selected || selected->isFinished())
                setSelectedIndex(i);
            setInhibit(true);
            emit queueChanged();
            return;
        }
    }
    // Something still being read will start once it's ready.
    for (Job *job : m_jobs)
        if (job->state() == "probing")
            return;

    const bool wasRunning = m_running;
    m_running = false;
    setInhibit(false);
    emit queueChanged();
    if (wasRunning) {
        int done = 0, failed = 0;
        for (Job *job : m_jobs) {
            done += job->state() == "done";
            failed += job->state() == "failed";
        }
        if (done + failed > 1)
            notify("Queue finished", QString("%1 converted%2").arg(done).arg(failed ? QString(", %1 failed").arg(failed) : QString()));
    }
}

void Backend::onJobFinished(Job *job, bool ok) {
    m_watchedPid = 0;
    m_telemetry->setProcess(0);
    if (ok) {
        const int seconds = int(job->elapsed() + 0.5);
        const QString took = seconds >= 3600 ? QString("%1 h %2 min").arg(seconds / 3600).arg(seconds % 3600 / 60)
                           : seconds >= 60   ? QString("%1 min %2 s").arg(seconds / 60).arg(seconds % 60)
                                             : QString("%1 s").arg(seconds);
        notify(QString("Converted %1").arg(job->fileName()),
               QString("%1 · %2 · took %3").arg(QFileInfo(job->outputPath()).fileName(),
                                                QLocale().formattedDataSize(QFileInfo(job->outputPath()).size(), 1, QLocale::DataSizeSIFormat),
                                                took));
    } else if (job->state() == "failed") {
        notify(QString("Couldn't convert %1").arg(job->fileName()), job->message());
    }
    emit queueChanged();
    if (m_running)
        startNext();
}

void Backend::onTick() {
    for (Job *job : m_jobs) {
        if (!job->isActive())
            continue;
        const qint64 pid = job->processId();
        if (pid != m_watchedPid) {
            m_watchedPid = pid;
            m_telemetry->setProcess(pid);
        }
        job->recordSystem(m_telemetry->processCpu(), m_telemetry->systemWatts(), m_telemetry->clockShare(), m_telemetry->throttled());
    }
    if (hasActiveWork())
        emit queueChanged();
}

void Backend::cancelJob(int index) {
    if (Job *job = jobAt(index))
        job->cancel();
}

void Backend::removeJob(int index) {
    Job *job = jobAt(index);
    if (!job || job->isActive())
        return;
    m_jobs.removeAt(index);
    m_undo.append({job, index});
    for (const auto &r : m_redo)
        delete r.job;
    m_redo.clear();
    while (m_undo.size() > 30)
        delete m_undo.takeFirst().job;
    emit jobsChanged();
    setSelectedIndex(std::min(index, int(m_jobs.size()) - 1));
    emit selectionChanged();
    emit queueChanged();
}

void Backend::undo() {
    if (m_undo.isEmpty())
        return;
    const Removed r = m_undo.takeLast();
    const int at = std::clamp(r.index, 0, int(m_jobs.size()));
    m_jobs.insert(at, r.job);
    m_redo.append({r.job, at});
    emit jobsChanged();
    setSelectedIndex(at);
    emit selectionChanged();
    emit queueChanged();
}

void Backend::redo() {
    if (m_redo.isEmpty())
        return;
    const Removed r = m_redo.takeLast();
    const int at = m_jobs.indexOf(r.job);
    if (at < 0 || r.job->isActive())
        return;
    m_jobs.removeAt(at);
    m_undo.append({r.job, at});
    emit jobsChanged();
    setSelectedIndex(std::min(at, int(m_jobs.size()) - 1));
    emit selectionChanged();
    emit queueChanged();
}

void Backend::retryJob(int index) {
    Job *job = jobAt(index);
    if (!job || !job->isFinished())
        return;
    job->reset();
    job->setOutputFolder(folderFor(job->inputPath()));
    if (m_running && !hasActiveWork())
        startNext();
}

void Backend::moveJob(int index, int delta) {
    const int to = index + delta;
    if (!jobAt(index) || to < 0 || to >= m_jobs.size())
        return;
    m_jobs.swapItemsAt(index, to);
    emit jobsChanged();
    if (m_selected == index)
        setSelectedIndex(to);
}

void Backend::clearFinished() {
    for (int i = int(m_jobs.size()) - 1; i >= 0; --i)
        if (m_jobs[i]->state() == "done")
            removeJob(i);
}

void Backend::setJobPreset(int index, const QString &presetId) {
    Job *job = jobAt(index);
    if (!job || job->isActive())
        return;
    job->setPreset(presetById(presetId));
    if (job->isFinished())
        job->reset();
    m_defaultPreset = presetId;
    saveConfig();
    emit presetsChanged();
}

void Backend::editJobPreset(int index, const QString &key, const QVariant &value) {
    Job *job = jobAt(index);
    if (!job || job->isActive())
        return;
    QVariantMap map = job->presetValue().toVariant();
    if (map.value(key) == value)
        return;
    map.insert(key, value);
    map.insert("builtin", false);
    map.insert("id", "custom");
    const QString name = map.value("name").toString();
    if (!name.endsWith("(edited)"))
        map.insert("name", name + " (edited)");
    job->setPreset(Preset::fromVariant(map));
    if (job->isFinished())
        job->reset();
}

void Backend::applyToWaiting(int index) {
    Job *source = jobAt(index);
    if (!source)
        return;
    int n = 0;
    for (Job *job : m_jobs) {
        if (job != source && job->isWaiting()) {
            job->setPreset(source->presetValue());
            ++n;
        }
    }
    setNotice(n == 0 ? "No other files are waiting." : QString("Applied to %1 waiting file%2.").arg(n).arg(n == 1 ? "" : "s"));
}

QString Backend::savePreset(int index, const QString &name) {
    Job *job = jobAt(index);
    const QString trimmed = name.trimmed();
    if (!job || trimmed.isEmpty())
        return {};
    Preset p = job->presetValue();
    p.name = trimmed;
    p.id = "user-" + slug(trimmed);
    p.suffix = "_" + slug(trimmed).replace('-', '_');
    p.builtin = false;
    if (p.description.isEmpty() || p.description.startsWith("Plays") || p.description.startsWith("The "))
        p.description = "Your preset.";
    for (int i = 0; i < m_userPresets.size(); ++i) {
        if (m_userPresets[i].id == p.id) {
            m_userPresets.removeAt(i);
            break;
        }
    }
    m_userPresets.append(p);
    savePresets();
    job->setPreset(p);
    m_defaultPreset = p.id;
    saveConfig();
    emit presetsChanged();
    setNotice(QString("Saved preset \"%1\".").arg(trimmed));
    return p.id;
}

void Backend::deletePreset(const QString &id) {
    for (int i = 0; i < m_userPresets.size(); ++i) {
        if (m_userPresets[i].id == id) {
            m_userPresets.removeAt(i);
            savePresets();
            if (m_defaultPreset == id) {
                m_defaultPreset = "review";
                saveConfig();
            }
            emit presetsChanged();
            return;
        }
    }
}

void Backend::openOutput(int index) {
    Job *job = jobAt(index);
    if (job && job->state() == "done")
        QDesktopServices::openUrl(QUrl::fromLocalFile(job->outputPath()));
}

void Backend::revealOutput(int index) {
    Job *job = jobAt(index);
    if (!job)
        return;
    const QString path = job->state() == "done" ? job->outputPath() : QString();
    if (!path.isEmpty()) {
        QDBusInterface fm("org.freedesktop.FileManager1", "/org/freedesktop/FileManager1",
                          "org.freedesktop.FileManager1", QDBusConnection::sessionBus());
        if (fm.isValid()) {
            const QDBusMessage reply = fm.call("ShowItems", QStringList{QUrl::fromLocalFile(path).toString()}, QString());
            if (reply.type() != QDBusMessage::ErrorMessage)
                return;
        }
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(job->outputFolder()));
}

void Backend::copyText(const QString &text) {
    if (auto *clipboard = QGuiApplication::clipboard()) {
        clipboard->setText(text);
        setNotice("Copied to the clipboard.");
    }
}

void Backend::setNotice(const QString &text) {
    m_notice = text;
    emit noticeChanged();
}

void Backend::dismissNotice() {
    setNotice({});
}

// Holding a logind inhibitor keeps the machine awake (lid closed included)
// for as long as the file descriptor stays open.
void Backend::setInhibit(bool on) {
    if (!on) {
        if (m_inhibit.isValid()) {
            m_inhibit = QDBusUnixFileDescriptor();
            emit queueChanged();
        }
        return;
    }
    if (m_inhibit.isValid())
        return;
    QDBusInterface login("org.freedesktop.login1", "/org/freedesktop/login1", "org.freedesktop.login1.Manager",
                         QDBusConnection::systemBus());
    if (!login.isValid())
        return;
    QDBusReply<QDBusUnixFileDescriptor> reply =
        login.call("Inhibit", "sleep:idle:handle-lid-switch", "Encodr", "Converting video", "block");
    if (!reply.isValid()) {
        reply = login.call("Inhibit", "sleep:idle", "Encodr", "Converting video", "block");
    }
    if (reply.isValid()) {
        m_inhibit = reply.value();
        emit queueChanged();
    }
}

void Backend::notify(const QString &title, const QString &body) {
    QProcess::startDetached("notify-send", {"-a", "Encodr", "-i", "encodr", title, body});
}

void Backend::loadConfig() {
    const QJsonObject settings = readJson(m_configDir + "/settings.json");
    m_defaultPreset = settings.value("defaultPreset").toString(m_defaultPreset);
    m_outputMode = settings.value("outputMode").toString(m_outputMode);
    m_outputFolder = settings.value("outputFolder").toString();

    m_userPresets.clear();
    const QJsonObject presets = readJson(m_configDir + "/presets.json");
    for (const auto &value : presets.value("presets").toArray()) {
        Preset p = Preset::fromVariant(value.toObject().toVariantMap());
        p.builtin = false;
        if (!p.id.isEmpty())
            m_userPresets.append(p);
    }
}

void Backend::saveConfig() const {
    writeJson(m_configDir + "/settings.json", QJsonObject{
        {"defaultPreset", m_defaultPreset}, {"outputMode", m_outputMode}, {"outputFolder", m_outputFolder}});
}

void Backend::savePresets() const {
    QJsonArray list;
    for (const auto &p : m_userPresets) {
        QVariantMap map = p.toVariant();
        map.remove("audioOnly");
        list.append(QJsonObject::fromVariantMap(map));
    }
    writeJson(m_configDir + "/presets.json", QJsonObject{{"presets", list}});
}
