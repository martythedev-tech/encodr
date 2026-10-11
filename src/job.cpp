#include "job.h"

#include <algorithm>

#include <QHash>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QTimer>
#include <signal.h>

namespace {
constexpr int kHistoryLimit = 720;         // points kept per graph; older ones are merged
constexpr double kHistoryInterval = 2.0;   // seconds of work between graph points
constexpr double kWindow = 20.0;           // seconds behind the "current" speed, fps and bitrate
constexpr qsizetype kErrorTail = 16 * 1024;

QString extensionFor(const Preset &p, const MediaInfo &info) {
    return containerFor(p, info);
}
}

Job::Job(int id, const QString &inputPath, QObject *parent)
    : QObject(parent), m_id(id), m_input(inputPath) {
    m_outputFolder = QFileInfo(inputPath).absolutePath();
    // Preview frames live in the cache, named per process so two Encodr
    // windows never draw over each other's.
    const QString cache = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + "/encodr";
    QDir().mkpath(cache);
    const QString stem = QString("%1/preview-%2-%3").arg(cache).arg(QCoreApplication::applicationPid()).arg(id);
    m_previewOut = stem + "-out.jpg";
    m_previewSource = stem + "-source.jpg";
    m_strip = stem + "-strip.jpg";
}

Job::~Job() {
    // ffmpeg, ffprobe, mediainfo and the level scans are all children of the job.
    for (QProcess *p : findChildren<QProcess *>()) {
        if (p->state() != QProcess::NotRunning) {
            p->kill();
            p->waitForFinished(2000);
        }
    }
    if (!m_finalPath.isEmpty() && isActive())
        QFile::remove(partialPathFor(m_finalPath));
    QFile::remove(m_previewOut);
    QFile::remove(m_previewSource);
    QFile::remove(m_strip);
}

QString Job::fileName() const {
    return QFileInfo(m_input).fileName();
}

void Job::setState(const QString &state, const QString &message) {
    m_state = state;
    m_message = message;
    emit stateChanged();
}

void Job::setOutputFolder(const QString &folder) {
    if (folder == m_outputFolder || isActive())
        return;
    m_outputFolder = folder;
    emit presetChanged();
}

void Job::setPreset(const Preset &preset) {
    if (isActive())
        return;
    m_preset = preset;
    emit presetChanged();
}

QString Job::outputPath() const {
    if (!m_finalPath.isEmpty())
        return m_finalPath;
    const QString base = QFileInfo(m_input).completeBaseName() + m_preset.suffix;
    return uniquePath(QDir(m_outputFolder).filePath(base + "." + extensionFor(m_preset, m_info)));
}

Preset Job::runPreset() const {
    Preset p = m_preset;
    if (p.usesVideoEncoder()) {
        p.hardware = m_engine;
        p.hardwareDevice = m_engineDevice;
    }
    p.cropLeft = m_crop.left();
    p.cropTop = m_crop.top();
    p.cropRight = m_crop.right();
    p.cropBottom = m_crop.bottom();
    return p;
}

void Job::setEngine(const QString &engine, const QString &device) {
    if (isActive() || (engine == m_engine && device == m_engineDevice))
        return;
    m_engine = engine.isEmpty() ? QString("cpu") : engine;
    m_engineDevice = device;
    emit presetChanged();
}

Command Job::command(const QString &output, bool previews) const {
    const bool cut = trimmed();
    return buildCommand(runPreset(), m_info, m_input, output, cut ? m_trimIn : -1, cut ? length() : -1,
                        previews ? m_previewOut : QString(), previews ? m_previewSource : QString(),
                        m_loudnessReady ? m_loudness : QList<Loudness>());
}

QString Job::commandLine() const {
    if (!m_info.valid)
        return {};
    return "ffmpeg " + shellQuote(command(outputPath(), false).arguments);
}

QStringList Job::notes() const {
    if (isActive() || isFinished())
        return m_runNotes;
    return m_info.valid ? command(outputPath(), false).notes : QStringList();
}

QVariantList Job::issues() const {
    QList<Issue> list = inspect(m_info);
    if (m_analysed)
        list += analysisIssues(m_moments, m_info.duration);
    return issuesToVariant(list);
}

int Job::cutCount() const {
    return int(std::count_if(m_moments.begin(), m_moments.end(), [](const Moment &m) { return m.kind == "cut"; }));
}

void Job::analyse() {
    if (!m_info.valid || m_analysis)
        return;
    auto *process = new QProcess(this);
    m_analysis = process;
    m_analysisPercent = 0;
    process->setReadChannel(QProcess::StandardOutput);
    connect(process, &QProcess::readyReadStandardOutput, this, [this, process] {
        // -progress: out_time_us=… lines; the log itself is on stderr.
        while (process->canReadLine()) {
            const QByteArray line = process->readLine().trimmed();
            if (line.startsWith("out_time_us=") && m_info.duration > 0) {
                m_analysisPercent = std::clamp(line.mid(12).toDouble() / 1e6 / m_info.duration * 100, 0.0, 100.0);
                emit analysisChanged();
            }
        }
    });
    connect(process, &QProcess::finished, this, [this, process](int code, QProcess::ExitStatus status) {
        process->deleteLater();
        if (status == QProcess::NormalExit && code == 0) {
            m_moments = parseAnalysis(process->readAllStandardError(), m_info.duration);
            m_analysed = true;
        }
        m_analysis = nullptr;
        emit analysisChanged();
        emit infoChanged();    // the findings
    });
    process->start("ffmpeg", analysisArguments(m_info, m_input));
    emit analysisChanged();
}

void Job::stopAnalysis() {
    if (m_analysis)
        m_analysis->kill();
}

QString Job::saveEdl() {
    if (!m_analysed || cutCount() == 0)
        return {};
    const QString base = QFileInfo(m_input).completeBaseName();
    const QString path = uniquePath(QDir(m_outputFolder).filePath(base + "_cuts.edl"));
    QFile f(path);
    if (!QDir().mkpath(m_outputFolder) || !f.open(QIODevice::WriteOnly | QIODevice::Text))
        return {};
    f.write(edlFromCuts(m_moments, m_info, base).toUtf8());
    return path;
}

QMargins Job::centredCrop(int width, int height, int across, int down) {
    if (width <= 0 || height <= 0 || across <= 0 || down <= 0)
        return {};
    int w = width, h = height;
    if (qint64(width) * down > qint64(height) * across)
        w = int(qint64(height) * across / down) & ~1;    // too wide: trim the sides
    else
        h = int(qint64(width) * down / across) & ~1;     // too tall: trim top and bottom
    const int left = ((width - w) / 2) & ~1, top = ((height - h) / 2) & ~1;
    return QMargins(left, top, width - w - left, height - h - top);
}

void Job::setCropMode(const QString &mode) {
    if (isActive())
        return;
    m_cropMode = mode;
    static const QHash<QString, QPair<int, int>> ratios{{"1:1", {1, 1}}, {"9:16", {9, 16}}, {"4:5", {4, 5}}, {"16:9", {16, 9}}};
    if (mode == "off") {
        m_crop = {};
    } else if (mode == "bars") {
        if (m_barsChecked)
            m_crop = m_detectedBars;
        else
            detectBars();    // sets the crop when it's done
    } else if (ratios.contains(mode)) {
        m_crop = centredCrop(m_info.width, m_info.height, ratios[mode].first, ratios[mode].second);
    }    // custom keeps the margins as they are, to adjust
    emit cropChanged();
    emit presetChanged();
}

void Job::setCropMargins(int left, int top, int right, int bottom) {
    if (isActive())
        return;
    // Even numbers: 4:2:0 video can't split a pixel pair.
    auto even = [](int v) { return std::max(0, v) & ~1; };
    m_crop = QMargins(even(left), even(top), even(right), even(bottom));
    m_cropMode = "custom";
    emit cropChanged();
    emit presetChanged();
}

void Job::detectBars() {
    if (m_bars || !m_info.hasVideo)
        return;
    auto *process = new QProcess(this);
    m_bars = process;
    process->setProcessChannelMode(QProcess::MergedChannels);
    connect(process, &QProcess::finished, this, [this, process](int, QProcess::ExitStatus) {
        process->deleteLater();
        m_detectedBars = parseBars(process->readAll(), m_info);
        m_barsChecked = true;
        m_bars = nullptr;
        if (m_cropMode == "bars" && !isActive())
            m_crop = m_detectedBars;
        emit cropChanged();
        emit presetChanged();
    });
    process->start("ffmpeg", barArguments(m_info, m_input));
}

QString Job::presetProblemText() const {
    const QString problem = presetProblem(m_preset);
    return problem.isEmpty() ? sourceProblem(runPreset(), m_info) : problem;
}

void Job::setTrim(double in, double out) {
    if (isActive())
        return;
    const double end = m_info.duration;
    in = std::clamp(in, 0.0, std::max(0.0, end));
    out = out <= 0 || out >= end ? 0 : std::max(out, in + 0.1);
    if (qFuzzyCompare(in + 1, m_trimIn + 1) && qFuzzyCompare(out + 1, m_trimOut + 1))
        return;
    m_trimIn = in;
    m_trimOut = out;
    emit trimChanged();
    emit presetChanged();    // the command and its notes
    emit infoChanged();      // the frame count
}

void Job::showFrameAt(double seconds) {
    if (!m_info.hasVideo)
        return;
    seconds = std::clamp(seconds, 0.0, std::max(0.0, m_info.duration - 0.05));
    if (m_posterBusy) {
        m_posterWanted = seconds;
        return;
    }
    makePoster(seconds);
}

QString Job::partialPathFor(const QString &output) {
    const QFileInfo fi(output);
    return fi.dir().filePath("." + fi.completeBaseName() + ".encodr-partial." + fi.suffix());
}

QString Job::uniquePath(const QString &path) {
    if (!QFileInfo::exists(path))
        return path;
    const QFileInfo fi(path);
    for (int n = 2; n < 1000; ++n) {
        const QString candidate = fi.dir().filePath(QString("%1 (%2).%3").arg(fi.completeBaseName()).arg(n).arg(fi.suffix()));
        if (!QFileInfo::exists(candidate))
            return candidate;
    }
    return path;
}

void Job::probe() {
    setState("probing", "Reading the file…");
    auto *process = new QProcess(this);
    m_helper = process;
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        process->deleteLater();
        setState("failed", "ffprobe isn't installed, so Encodr can't read media files. Install ffmpeg.");
    });
    connect(process, &QProcess::finished, this, [this, process](int code, QProcess::ExitStatus) {
        process->deleteLater();
        m_info = parseProbe(process->readAllStandardOutput(), m_input);
        if (!m_info.valid) {
            const QString err = QString::fromUtf8(process->readAllStandardError()).trimmed();
            setState("failed", err.isEmpty() || code == 0 ? m_info.error : err);
            emit infoChanged();
            return;
        }
        setState("ready");
        emit infoChanged();
        emit presetChanged();
        emit trimChanged();    // the out point defaults to the end, now known
        makePoster(m_info.duration * 0.1);
        makeStrip();
        scanLevels(0);
    });
    process->start("ffprobe", {"-v", "error", "-print_format", "json", "-show_format", "-show_streams", m_input});

    auto *report = new QProcess(this);
    connect(report, &QProcess::errorOccurred, this, [this, report](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        report->deleteLater();
        m_mediainfo = "The full report needs mediainfo. Install it with:  sudo pacman -S mediainfo";
        emit infoChanged();
    });
    connect(report, &QProcess::finished, this, [this, report](int, QProcess::ExitStatus) {
        report->deleteLater();
        m_mediainfo = QString::fromUtf8(report->readAllStandardOutput()).trimmed();
        emit infoChanged();
    });
    report->start("mediainfo", {m_input});
}

// A frame a tenth of the way in stands for the file until conversion starts
// (the very first frames are often black or a slate).
void Job::makePoster(double at) {
    if (!m_info.hasVideo)
        return;
    m_posterBusy = true;
    auto *process = new QProcess(this);
    connect(process, &QProcess::finished, this, [this, process, at](int code, QProcess::ExitStatus) {
        process->deleteLater();
        m_posterBusy = false;
        if (code == 0 && QFileInfo::exists(m_previewSource)) {
            m_posterTime = at;
            ++m_previewStamp;
            emit previewChanged();
        }
        if (m_posterWanted >= 0) {
            const double next = m_posterWanted;
            m_posterWanted = -1;
            if (!isActive())
                makePoster(next);
        }
    });
    process->start("ffmpeg", posterArguments(m_info, m_input, at, m_previewSource));
}

void Job::makeStrip() {
    if (!m_info.hasVideo || m_info.duration <= 0)
        return;
    auto *process = new QProcess(this);
    connect(process, &QProcess::finished, this, [this, process](int code, QProcess::ExitStatus) {
        process->deleteLater();
        m_hasStrip = code == 0 && QFileInfo::exists(m_strip);
        emit previewChanged();
    });
    process->start("ffmpeg", stripArguments(m_info, m_input, stripCount(), 72, m_strip));
}

void Job::checkPreview() {
    const QDateTime modified = QFileInfo(m_previewOut).lastModified();
    if (!modified.isValid() || modified == m_previewTime)
        return;
    m_previewTime = modified;
    m_hasLivePreview = true;
    ++m_previewStamp;
    emit previewChanged();
}

// Levels come from a 30-second stretch in the middle of the file: reading a
// whole long recording off an external drive just to show a meter isn't worth it.
void Job::scanLevels(int track) {
    if (track >= m_info.audio.size()) {
        m_levelsScanned = true;
        emit infoChanged();
        return;
    }
    const double start = std::max(0.0, m_info.duration / 2 - 15);
    auto *process = new QProcess(this);
    m_helper = process;
    connect(process, &QProcess::finished, this, [this, process, track](int, QProcess::ExitStatus) {
        process->deleteLater();
        const QString out = QString::fromUtf8(process->readAllStandardError());
        static const QRegularExpression mean("mean_volume: (-?[0-9.]+|-inf) dB");
        static const QRegularExpression peak("max_volume: (-?[0-9.]+|-inf) dB");
        auto value = [](const QRegularExpressionMatch &m) {
            return !m.hasMatch() ? NAN : m.captured(1) == "-inf" ? -120.0 : m.captured(1).toDouble();
        };
        if (track < m_info.audio.size()) {
            m_info.audio[track].meanDb = value(mean.match(out));
            m_info.audio[track].peakDb = value(peak.match(out));
        }
        emit infoChanged();
        scanLevels(track + 1);
    });
    process->start("ffmpeg", {"-nostdin", "-hide_banner", "-ss", QString::number(start, 'f', 2), "-t", "30",
                              "-i", m_input, "-map", QString("0:a:%1").arg(track),
                              "-af", "volumedetect", "-f", "null", "-"});
}

bool Job::start() {
    if (!m_info.valid || !presetProblemText().isEmpty() || isActive())
        return false;
    if (!QDir().mkpath(m_outputFolder) || !QFileInfo(m_outputFolder).isWritable()) {
        setState("failed", QString("Can't write to %1. Choose another output folder.").arg(m_outputFolder));
        return false;
    }

    m_finalPath = outputPath();
    if (QFileInfo(m_finalPath).absoluteFilePath() == QFileInfo(m_input).absoluteFilePath()) {
        m_finalPath.clear();
        setState("failed", "The output would replace the original. Choose another folder or preset.");
        return false;
    }
    m_fallbackNote.clear();
    m_loudness.clear();
    m_loudnessReady = false;
    return launch();
}

bool Job::launch() {
    QFile::remove(m_previewOut);
    m_previewTime = QDateTime();
    m_hasLivePreview = false;
    emit previewChanged();

    m_parser = ProgressParser();
    m_last = ProgressSample();
    m_estimate = Estimate();
    m_window.clear();
    m_errors.clear();
    m_recentSpeed = m_recentFps = m_recentBitrate = 0;
    m_energyWh = m_peakCpu = 0;
    m_clockShare = m_lowestClockShare = 0;
    m_heatSeconds = 0;
    m_lastHistoryAt = -1;
    m_speedHistory.clear();
    m_fpsHistory.clear();
    m_bitrateHistory.clear();
    m_cpuHistory.clear();
    m_wattHistory.clear();
    m_cancelling = false;
    m_freeSpace = QStorageInfo(m_outputFolder).bytesAvailable();

    m_activeMsBeforePause = 0;
    m_clock.start();
    m_startedAt = QDateTime::currentDateTime();
    m_finishedAt = QDateTime();
    setState("running");
    emit statsChanged();

    // A loudness target needs each audio stream measured first (once per
    // run: a GPU fallback reuses the numbers).
    const bool measure = !m_loudnessReady && !m_preset.copiesAudio() && loudnessTarget(m_preset.loudness).lufs < 0
                         && audioStreamCount(m_preset, m_info) > 0;
    if (measure)
        measureLoudness(0);
    else
        encode();
    return true;
}

void Job::measureLoudness(int stream) {
    if (stream >= audioStreamCount(m_preset, m_info)) {
        m_loudnessReady = true;
        encode();
        return;
    }
    const int count = audioStreamCount(m_preset, m_info);
    m_message = count > 1 ? QString("Measuring loudness, audio %1 of %2…").arg(stream + 1).arg(count)
                          : QString("Measuring loudness…");
    emit stateChanged();

    auto *process = new QProcess(this);
    m_process = process;
    process->setProcessChannelMode(QProcess::MergedChannels);
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        process->deleteLater();
        m_clock.invalidate();
        setState("failed", "ffmpeg isn't installed, so Encodr can't convert anything. Install ffmpeg.");
        emit finished(false);
    });
    connect(process, &QProcess::finished, this, [this, process, stream](int code, QProcess::ExitStatus status) {
        if (m_cancelling || status != QProcess::NormalExit || code != 0) {
            m_errors = process->readAll().right(kErrorTail);
            onFinished(code, status);    // stopped, or ffmpeg couldn't read the audio
            return;
        }
        m_loudness.append(parseLoudnorm(process->readAll()));
        process->deleteLater();
        measureLoudness(stream + 1);
    });
    const bool cut = trimmed();
    process->start("ffmpeg", loudnessArguments(m_preset, m_info, m_input, stream, cut ? m_trimIn : -1,
                                               cut ? length() : -1));
}

void Job::encode() {
    const Command command = this->command(partialPathFor(m_finalPath), true);
    m_runNotes = command.notes;
    if (!m_fallbackNote.isEmpty())
        m_runNotes.prepend(m_fallbackNote);
    m_message.clear();
    emit stateChanged();

    auto *process = new QProcess(this);
    m_process = process;
    connect(process, &QProcess::readyReadStandardOutput, this, &Job::readProgress);
    connect(process, &QProcess::readyReadStandardError, this, [this, process] {
        m_errors.append(process->readAllStandardError());
        if (m_errors.size() > kErrorTail)
            m_errors = m_errors.right(kErrorTail);
    });
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        process->deleteLater();
        m_clock.invalidate();
        setState("failed", "ffmpeg isn't installed, so Encodr can't convert anything. Install ffmpeg.");
        emit finished(false);
    });
    connect(process, &QProcess::finished, this, &Job::onFinished);

    emit presetChanged();
    process->start("ffmpeg", command.arguments);
}

void Job::pause() {
    if (m_state != "running" || !m_process)
        return;
    ::kill(pid_t(m_process->processId()), SIGSTOP);
    m_activeMsBeforePause += m_clock.elapsed();
    m_clock.invalidate();
    setState("paused", "Paused — ffmpeg is frozen in place and resumes exactly where it stopped.");
    emit statsChanged();
}

void Job::resume() {
    if (m_state != "paused" || !m_process)
        return;
    ::kill(pid_t(m_process->processId()), SIGCONT);
    m_clock.start();
    // The speed window would otherwise span the pause and read as a slowdown.
    m_window.clear();
    setState("running");
}

void Job::cancel() {
    if (!isActive() || !m_process)
        return;
    m_cancelling = true;
    if (m_state == "paused")
        ::kill(pid_t(m_process->processId()), SIGCONT);
    m_process->terminate();
    QPointer<QProcess> process = m_process;
    QTimer::singleShot(4000, this, [process] {
        if (process && process->state() != QProcess::NotRunning)
            process->kill();
    });
}

void Job::reset() {
    if (isActive() || !m_info.valid)
        return;
    m_finalPath.clear();
    m_estimate = Estimate();
    m_last = ProgressSample();
    m_activeMsBeforePause = 0;
    m_clock.invalidate();
    setState("ready");
    emit presetChanged();
    emit statsChanged();
}

double Job::activeSeconds() const {
    return (m_activeMsBeforePause + (m_clock.isValid() ? m_clock.elapsed() : 0)) / 1000.0;
}

double Job::elapsed() const {
    return activeSeconds();
}

QDateTime Job::finishAt() const {
    if (m_state != "running" || m_estimate.remaining < 0)
        return {};
    return QDateTime::currentDateTime().addMSecs(qint64(m_estimate.remaining * 1000));
}

double Job::averageFps() const {
    const double t = activeSeconds();
    return t > 0 ? m_last.frame / t : 0;
}

qint64 Job::projectedSize() const {
    if (m_state == "done")
        return QFileInfo(m_finalPath).size();
    return m_estimate.projectedSize;
}

QVariantMap Job::history() const {
    return {{"speed", m_speedHistory}, {"fps", m_fpsHistory}, {"bitrate", m_bitrateHistory},
            {"cpu", m_cpuHistory}, {"watts", m_wattHistory}};
}

void Job::pushHistory(QVariantList &list, double value) {
    list.append(value);
    if (list.size() < kHistoryLimit)
        return;
    // Merge neighbours so the graph keeps covering the whole run.
    QVariantList merged;
    for (qsizetype i = 0; i + 1 < list.size(); i += 2)
        merged.append((list[i].toDouble() + list[i + 1].toDouble()) / 2);
    list = merged;
}

void Job::recordSystem(double processCpu, double watts, double clockShare, bool throttled) {
    if (m_state != "running")
        return;
    m_clockShare = clockShare;
    if (clockShare > 0)
        m_lowestClockShare = m_lowestClockShare > 0 ? std::min(m_lowestClockShare, clockShare) : clockShare;
    if (throttled)
        ++m_heatSeconds;
    m_pendingCpu = processCpu;
    m_pendingWatts = watts;
    m_peakCpu = std::max(m_peakCpu, processCpu);
    m_energyWh += watts / 3600.0;
}

void Job::readProgress() {
    if (!m_process)
        return;
    const auto samples = m_parser.feed(m_process->readAllStandardOutput());
    if (samples.isEmpty())
        return;

    const double t = activeSeconds();
    for (const auto &s : samples) {
        m_last = s;
        m_window.push_back({t, s.outTime, s.frame, s.totalSize});
    }
    while (m_window.size() > 2 && m_window.front().t < t - kWindow)
        m_window.pop_front();

    const Point &a = m_window.front();
    const Point &b = m_window.back();
    const double dt = b.t - a.t;
    if (dt >= 1.0) {
        m_recentSpeed = (b.outTime - a.outTime) / dt;
        m_recentFps = (b.frame - a.frame) / dt;
        const double media = b.outTime - a.outTime;
        if (media > 0.5 && b.size >= a.size)
            m_recentBitrate = (b.size - a.size) * 8.0 / 1000.0 / media;
    } else {
        m_recentSpeed = m_last.speed;
        m_recentFps = m_last.fps;
        m_recentBitrate = m_last.bitrateKbps;
    }

    m_estimate = estimate(length(), m_last.outTime, t, m_last.totalSize, m_recentSpeed);
    if (m_last.end) {
        m_estimate.fraction = 1;
        m_estimate.remaining = 0;
    }

    checkPreview();
    if (m_lastHistoryAt < 0 || t - m_lastHistoryAt >= kHistoryInterval) {
        m_lastHistoryAt = t;
        pushHistory(m_speedHistory, m_recentSpeed);
        pushHistory(m_fpsHistory, m_recentFps);
        pushHistory(m_bitrateHistory, m_recentBitrate);
        pushHistory(m_cpuHistory, m_pendingCpu);
        pushHistory(m_wattHistory, m_pendingWatts);
        m_freeSpace = QStorageInfo(m_outputFolder).bytesAvailable();
    }
    emit statsChanged();
}

void Job::onFinished(int code, QProcess::ExitStatus status) {
    if (m_clock.isValid()) {
        m_activeMsBeforePause += m_clock.elapsed();
        m_clock.invalidate();
    }
    m_finishedAt = QDateTime::currentDateTime();
    if (m_process)
        m_process->deleteLater();
    const QString partial = partialPathFor(m_finalPath);

    if (m_cancelling) {
        QFile::remove(partial);
        m_finalPath.clear();
        setState("cancelled", "Stopped. The unfinished file was deleted.");
        emit statsChanged();
        emit finished(false);
        return;
    }

    if (status == QProcess::NormalExit && code == 0 && QFileInfo::exists(partial)) {
        if (QFileInfo::exists(m_finalPath))
            m_finalPath = uniquePath(m_finalPath);
        if (QFile::rename(partial, m_finalPath)) {
            m_estimate.fraction = 1;
            m_estimate.remaining = 0;
            setState("done", QString("Saved as %1").arg(QFileInfo(m_finalPath).fileName()));
            emit presetChanged();
            emit statsChanged();
            emit finished(true);
            return;
        }
        setState("failed", QString("Converted, but couldn't rename the result to %1. It's at %2.")
                               .arg(QFileInfo(m_finalPath).fileName(), partial));
        emit finished(false);
        return;
    }

    QFile::remove(partial);
    QStringList lines = QString::fromUtf8(m_errors).split('\n', Qt::SkipEmptyParts);
    while (lines.size() > 6)
        lines.removeFirst();
    QString why = lines.join('\n').trimmed();
    if (why.isEmpty())
        why = status == QProcess::CrashExit ? "ffmpeg crashed." : QString("ffmpeg stopped with code %1.").arg(code);

    // A GPU encoder can refuse a file the CPU handles fine (a size or format
    // the card doesn't support, a driver hiccup): redo it on the CPU rather
    // than fail, and say so.
    if (runPreset().usesVideoEncoder() && m_engine != "cpu") {
        m_fallbackNote = QString("The GPU encoder failed (%1), so this file was converted on the CPU instead")
                             .arg(lines.isEmpty() ? QString("no reason given") : lines.last().trimmed());
        m_engine = "cpu";
        m_engineDevice.clear();
        m_errors.clear();
        emit presetChanged();
        if (launch())
            return;
    }
    m_finalPath.clear();
    setState("failed", why);
    emit statsChanged();
    emit finished(false);
}
