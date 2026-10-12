#include "job.h"
#include "naming.h"
#include "quality.h"

#include <algorithm>
#include <memory>

#include <QHash>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QThread>
#include <QTimer>
#include <signal.h>
#include <sys/stat.h>

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
    m_outputStill = stem + "-output-still.jpg";
    m_finishedStill = stem + "-finished-still.jpg";
    m_stillTimer.setSingleShot(true);
    m_stillTimer.setInterval(120);
    connect(&m_stillTimer, &QTimer::timeout, this, &Job::makeOutputStill);
    // A change to the processing redraws the still that's showing.
    auto redraw = [this] {
        if (m_stillTime >= 0 && m_stillWanted < 0 && !isActive())
            showOutputAt(m_stillTime);
    };
    connect(this, &Job::presetChanged, this, redraw);
    connect(this, &Job::cropChanged, this, redraw);
    connect(this, &Job::padChanged, this, redraw);
    connect(this, &Job::subtitlesChanged, this, redraw);
    m_subtitleLink = stem + "-subtitles";
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
    QFile::remove(m_outputStill);
    for (const QString &ext : {"srt", "ass", "ssa", "vtt"})
        QFile::remove(m_subtitleLink + "." + ext);
    for (const QString &name : {"-lut.cube", "-logo.png", "-logo.jpg", "-logo.jpeg", "-logo.webp"})
        QFile::remove(m_subtitleLink + name);
    QFile::remove(m_subtitleLink + "-source." + QFileInfo(m_input).suffix().toLower());
    QFile::remove(m_subtitleLink + "-join.txt");
    QFile::remove(m_finishedStill);
    for (const QString &name : {"-quality.log", "-quality.json", "-reference.y4m", "-output.y4m"})
        QFile::remove(m_subtitleLink + name);
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

void Job::setNamePattern(const QString &pattern) {
    if (pattern == m_namePattern || isActive())
        return;
    m_namePattern = pattern;
    emit presetChanged();
}

QString Job::outputPath() const {
    if (!m_finalPath.isEmpty())
        return m_finalPath;
    const Preset p = runPreset();
    naming::Parts parts;
    parts.name = QFileInfo(m_input).completeBaseName();
    parts.suffix = p.suffix;
    parts.preset = p.id;
    parts.when = QDateTime::currentDateTime();
    parts.height = outputHeight(p, m_info);
    const QString pattern = m_namePattern.isEmpty() ? naming::kDefault : m_namePattern;
    // Numbered names pick a free number themselves.
    const QString path = naming::pathFor(m_outputFolder, pattern, parts, extensionFor(m_preset, m_info));
    return naming::numbered(naming::clean(pattern)) ? path : uniquePath(path);
}

Preset Job::runPreset() const {
    Preset p = m_preset;
    if (p.usesVideoEncoder()) {
        p.hardware = m_engine;
        p.hardwareDevice = m_engineDevice;
    }
    if (m_subtitleBurn.startsWith("track:")) {
        const int track = m_subtitleBurn.mid(6).toInt();
        // si= counts subtitle streams only, as the track number does.
        p.burnSubtitles = m_subtitleLink + "-source." + QFileInfo(m_input).suffix().toLower();
        p.burnSubtitleTrack = track;
        for (const SubtitleTrack &t : m_info.subtitles)
            if (t.index == track)
                p.burnSubtitlePicture = !t.isText();
        QString name = QString("subtitle track %1").arg(track + 1);
        for (const SubtitleTrack &t : m_info.subtitles)
            if (t.index == track && !t.language.isEmpty() && t.language != "und")
                name += " (" + t.language + ")";
        p.burnSubtitleName = name;
    } else if (m_subtitleBurn != "off" && !m_subtitleBurn.isEmpty()) {
        p.burnSubtitles = m_subtitleLink + "." + QFileInfo(m_subtitleBurn).suffix().toLower();
        p.burnSubtitleName = QFileInfo(m_subtitleBurn).fileName();
    }
    if (!p.lut.isEmpty())
        p.lutFile = m_subtitleLink + "-lut.cube";
    if (!p.logo.isEmpty())
        p.logoFile = m_subtitleLink + "-logo." + QFileInfo(p.logo).suffix().toLower();
    p.cropLeft = m_crop.left();
    p.cropTop = m_crop.top();
    p.cropRight = m_crop.right();
    p.cropBottom = m_crop.bottom();
    // Set afresh every time, like the crop: a preset taken from here and
    // set again mustn't carry the clips twice.
    p.joinCount = joined() ? clipCount() : 0;
    p.joinDifference = joined() ? m_joinDifference : QString();
    p.joinList = joined() && m_joinDifference.isEmpty() ? m_joinList : QString();
    p.joinClips.clear();
    if (joined() && !m_joinDifference.isEmpty()) {
        p.joinClips << m_info;
        for (const Job *c : m_clips)
            p.joinClips << c->info();
    }
    const QStringList aspect = m_padMode.split(':');
    if (aspect.size() == 2) {
        p.padAcross = aspect[0].toInt();
        p.padDown = aspect[1].toInt();
        p.padFill = m_padFill;
    }
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
    return buildCommand(runPreset(), runInfo(), m_input, output, cut ? m_trimIn : -1, cut ? length() : -1,
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

QVariantMap Job::suggestion() const {
    // Not when the preset already copies, or the job changes the picture
    // (crop, bars, burned-in subtitles), which a rewrap can't do. Joined
    // clips that match can be joined by copying, in seconds.
    if (joined() && m_joinDifference.isEmpty() && !m_preset.copiesVideo() && m_crop.isNull() && m_padMode == "off") {
        Preset copy;
        for (const Preset &b : builtinPresets())
            if (b.id == "copy")
                copy = b;
        copy.joinCount = clipCount();
        copy.joinList = m_joinList;
        if (!copy.id.isEmpty() && sourceProblem(copy, runInfo()).isEmpty())
            return {{"title", "These clips can be joined as they are"},
                    {"detail", "They match, so they can be put back to back without re-encoding: in seconds, "
                               "nothing lost."},
                    {"presetId", "copy"}, {"action", "Join without re-encoding"}};
        return {};
    }
    if (joined())
        return {};
    if (m_preset.copiesVideo() || !m_crop.isNull() || m_padMode != "off" || (m_subtitleBurn != "off" && !m_subtitleBurn.isEmpty()))
        return {};
    const QString reason = rewrapReason(m_info, m_input);
    if (reason.isEmpty())
        return {};
    return {{"title", "This only needs a rewrap"}, {"detail", reason}, {"presetId", "rewrap-mp4"},
            {"action", "Rewrap to MP4"}};
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

void Job::setPadMode(const QString &mode) {
    static const QStringList modes{"off", "16:9", "4:3", "1:1", "4:5", "9:16"};
    if (isActive() || mode == m_padMode || !modes.contains(mode))
        return;
    m_padMode = mode;
    emit padChanged();
    emit presetChanged();
}

void Job::setPadFill(const QString &fill) {
    if (isActive() || fill == m_padFill || (fill != "black" && fill != "blur"))
        return;
    m_padFill = fill;
    emit padChanged();
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

QVariantList Job::subtitleChoices() const {
    QVariantList list{QVariantMap{{"value", "off"}, {"text", "None"}}};
    for (const SubtitleTrack &t : m_info.subtitles) {
        QString text = QString("Track %1").arg(t.index + 1);
        if (!t.language.isEmpty() && t.language != "und")
            text += " · " + t.language;
        if (!t.title.isEmpty())
            text += " · " + t.title;
        if (!t.isText())
            text += " · " + codecName(t.codec) + " pictures";
        list.append(QVariantMap{{"value", QString("track:%1").arg(t.index)}, {"text", text}});
    }
    // Beside the source, named after it: film.srt, film.en.srt, film.forced.ass.
    const QFileInfo source(m_input);
    QStringList files;
    for (const QFileInfo &f : source.dir().entryInfoList({"*.srt", "*.ass", "*.ssa", "*.vtt"}, QDir::Files, QDir::Name))
        if (f.completeBaseName() == source.completeBaseName() || f.fileName().startsWith(source.completeBaseName() + "."))
            files << f.absoluteFilePath();
    if (!m_pickedSubtitles.isEmpty() && !files.contains(m_pickedSubtitles))
        files << m_pickedSubtitles;
    for (const QString &f : files)
        list.append(QVariantMap{{"value", f}, {"text", QFileInfo(f).fileName()}});
    list.append(QVariantMap{{"value", "pick"}, {"text", "Choose a file…"}});
    return list;
}

void Job::setSubtitleBurn(const QString &value) {
    if (isActive() || joined() || value == "pick")
        return;
    if (value != "off" && !value.startsWith("track:")) {
        const QString suffix = QFileInfo(value).suffix().toLower();
        if (!QStringList{"srt", "ass", "ssa", "vtt"}.contains(suffix) || !QFileInfo::exists(value))
            return;
        if (QFileInfo(value).absolutePath() != QFileInfo(m_input).absolutePath())
            m_pickedSubtitles = value;
    }
    m_subtitleBurn = value;
    emit subtitlesChanged();
    emit presetChanged();
}

// ffmpeg's filter syntax trips on ':', ',', quotes and brackets in a path;
// a link with a plain name in the cache sidesteps all of it.
void Job::linkSubtitles() const {
    const Preset p = runPreset();
    auto link = [](const QString &target, const QString &name) {
        QFile::remove(name);
        QFile::link(QFileInfo(target).absoluteFilePath(), name);
    };
    if (p.burnsSubtitles())
        link(m_subtitleBurn.startsWith("track:") ? m_input : m_subtitleBurn, p.burnSubtitles);
    if (!p.lutFile.isEmpty())
        link(p.lut, p.lutFile);
    if (!p.logoFile.isEmpty())
        link(p.logo, p.logoFile);
}

QString Job::presetProblemText() const {
    if (m_preset.video == "av1" && m_engine == "missing")
        return "This ffmpeg can't make AV1: it was built without SVT-AV1. The Encodr AppImage's ffmpeg has it, "
               "as do most distributions' (on Arch, ffmpeg with svt-av1). Choose another codec meanwhile.";
    const QString problem = presetProblem(m_preset);
    return problem.isEmpty() ? sourceProblem(runPreset(), runInfo()) : problem;
}

double Job::length() const {
    if (!joined())
        return trimOut() - m_trimIn;
    double total = m_info.duration;
    for (const Job *c : m_clips)
        total += c->info().duration;
    return total;
}

QVariantList Job::clipsVariant() const {
    QVariantList list;
    auto add = [&list](const Job *j) {
        list << QVariantMap{{"name", j->fileName()}, {"path", j->inputPath()}, {"duration", j->info().duration},
                            {"summary", j->info().summary()}};
    };
    add(this);
    for (const Job *c : m_clips)
        add(c);
    return list;
}

MediaInfo Job::runInfo() const {
    if (!joined())
        return m_info;
    MediaInfo info = m_info;
    info.duration = length();
    if (!m_joinDifference.isEmpty()) {
        // The join gives one sound, shaped like the first clip's that has any.
        info.audio.clear();
        info.subtitles.clear();
        QList<const Job *> all{this};
        for (const Job *c : m_clips)
            all << c;
        for (const Job *j : all)
            if (!j->info().audio.isEmpty()) {
                AudioTrack t = j->info().audio.first();
                t.index = 0;
                info.audio << t;
                break;
            }
    }
    return info;
}

void Job::join(const QList<Job *> &clips) {
    if (isActive() || clips.isEmpty())
        return;
    for (Job *c : clips) {
        c->setParent(this);
        m_clips << c;
    }
    m_joinDifference.clear();
    QStringList paths{QFileInfo(m_input).absoluteFilePath()};
    for (const Job *c : m_clips) {
        paths << QFileInfo(c->inputPath()).absoluteFilePath();
        if (m_joinDifference.isEmpty())
            m_joinDifference = clipDifference(m_info, c->info());
    }
    m_joinList = m_subtitleLink + "-join.txt";
    QFile list(m_joinList);
    if (list.open(QIODevice::WriteOnly | QIODevice::Truncate))
        list.write(concatList(paths).toUtf8());
    m_trimIn = m_trimOut = 0;
    m_subtitleBurn = "off";
    emit trimChanged();
    emit subtitlesChanged();
    emit infoChanged();
    emit presetChanged();
}

QList<Job *> Job::split() {
    if (isActive() || !joined())
        return {};
    const QList<Job *> clips = m_clips;
    m_clips.clear();
    m_joinDifference.clear();
    for (Job *c : clips)
        c->setParent(nullptr);
    QFile::remove(m_joinList);
    emit infoChanged();
    emit presetChanged();
    return clips;
}

void Job::setTrim(double in, double out) {
    if (isActive() || joined())
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

bool Job::canShowOutput() const {
    const Preset p = runPreset();
    return m_info.hasVideo && !p.audioOnly() && !p.copiesVideo();
}

void Job::showOutputAt(double seconds) {
    if (!canShowOutput())
        return;
    m_stillWanted = std::clamp(seconds, 0.0, std::max(0.0, m_info.duration - 0.05));
    m_stillTimer.start();
    emit outputStillChanged();
}

void Job::makeOutputStill() {
    if (m_stillBusy || m_stillWanted < 0 || isActive())
        return;    // a running still picks the latest request up when it ends
    const double at = m_stillWanted;
    m_stillWanted = -1;
    const QStringList args = outputStillArguments(runPreset(), m_info, m_input, at, m_outputStill);
    if (args.isEmpty()) {
        emit outputStillChanged();
        return;
    }
    linkSubtitles();    // the LUT, logo and subtitle files under the names the filters read
    m_stillBusy = true;
    auto *process = new QProcess(this);
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        process->deleteLater();
        m_stillBusy = false;
        m_stillError = "ffmpeg isn't installed";
        emit outputStillChanged();
    });
    connect(process, &QProcess::finished, this, [this, process, at](int code, QProcess::ExitStatus status) {
        process->deleteLater();
        m_stillBusy = false;
        if (status == QProcess::NormalExit && code == 0 && QFileInfo::exists(m_outputStill)) {
            m_stillTime = at;
            m_stillError.clear();
            ++m_stillStamp;
        } else {
            const QStringList lines = QString::fromUtf8(process->readAllStandardError()).trimmed().split('\n');
            m_stillError = lines.last().isEmpty() ? QString("ffmpeg couldn't make the frame") : lines.last();
        }
        if (m_stillWanted >= 0)
            m_stillTimer.start();
        emit outputStillChanged();
    });
    emit outputStillChanged();
    process->start("ffmpeg", args);
}

namespace {
// How the picture gets measured here: ffmpeg's libvmaf if it has it, else
// the vmaf tool, else SSIM. Looked up once; ENCODR_QUALITY_TOOL (libvmaf,
// vmaf, ssim) picks one instead, for tests.
quality::Tool qualityTool() {
    const QByteArray forced = qgetenv("ENCODR_QUALITY_TOOL");
    if (forced == "libvmaf")
        return quality::Tool::LibVmaf;
    if (forced == "vmaf")
        return quality::Tool::VmafCli;
    if (forced == "ssim")
        return quality::Tool::Ssim;
    static int found = -1;
    if (found < 0) {
        QProcess p;
        p.start("ffmpeg", {"-hide_banner", "-filters"});
        p.waitForFinished(5000);
        if (p.readAllStandardOutput().contains(" libvmaf "))
            found = int(quality::Tool::LibVmaf);
        else if (!QStandardPaths::findExecutable("vmaf").isEmpty())
            found = int(quality::Tool::VmafCli);
        else
            found = int(quality::Tool::Ssim);
    }
    return quality::Tool(found);
}

QString lastLine(const QByteArray &bytes) {
    const QStringList lines = QString::fromUtf8(bytes).trimmed().split('\n');
    return lines.isEmpty() ? QString() : lines.last().trimmed();
}
}

QProcess *Job::qualityProcess() {
    auto *process = new QProcess(this);
    m_qualityProcesses << process;
    return process;
}

void Job::checkQuality() {
    if (m_state != "done" || m_qualityState == "checking" || !QFileInfo::exists(outputPath()))
        return;
    m_quality.clear();
    m_qualityState = "checking";
    m_qualityStep = "Reading the converted file…";
    m_qualityPercent = 0;
    emit qualityChanged();
    QProcess *probe = qualityProcess();
    connect(probe, &QProcess::finished, this, [this, probe](int, QProcess::ExitStatus) {
        probe->deleteLater();
        if (m_qualityState != "checking")
            return;
        m_outputInfo = parseProbe(probe->readAllStandardOutput(), outputPath());
        if (!m_outputInfo.valid)
            return finishQuality("Couldn't read the converted file.");
        measurePicture();
    });
    probe->start("ffprobe", {"-v", "error", "-print_format", "json", "-show_format", "-show_streams", outputPath()});
}

void Job::stopQualityCheck() {
    for (const QPointer<QProcess> &p : std::as_const(m_qualityProcesses))
        if (p) {
            p->disconnect(this);
            p->kill();
            p->waitForFinished(1000);
            p->deleteLater();
        }
    m_qualityProcesses.clear();
    if (m_qualityState == "checking") {
        m_qualityState.clear();
        emit qualityChanged();
    }
}

void Job::measurePicture() {
    const Preset p = runPreset();
    if (!m_outputInfo.hasVideo || p.audioOnly()) {
        m_quality["videoNote"] = "No picture to compare.";
        return checkAudio();
    }
    if (p.copiesVideo()) {
        m_quality["videoNote"] = "Video copied as it is: identical to the source, nothing lost.";
        return checkAudio();
    }
    linkSubtitles();    // the LUT, logo and subtitle files under the names the filters read
    const bool cut = trimmed();
    const double start = cut ? m_trimIn : -1, len = cut ? length() : -1;
    const MediaInfo source = runInfo();
    const quality::Tool tool = qualityTool();
    const int threads = std::max(1, QThread::idealThreadCount());
    const double fps = m_outputInfo.fps > 0 ? m_outputInfo.fps : 25;
    const int subsample = tool == quality::Tool::Ssim ? 1 : quality::subsampleFor(qint64(length() * fps));
    const double total = std::max(0.1, length());
    m_qualityStep = QString("Measuring the picture (%1)…").arg(quality::metricName(tool));
    emit qualityChanged();

    auto progress = [this, total](QProcess *process) {
        connect(process, &QProcess::readyReadStandardOutput, this, [this, process, total] {
            while (process->canReadLine()) {
                const QByteArray line = process->readLine().trimmed();
                if (line.startsWith("out_time_us=")) {
                    m_qualityPercent = std::clamp(line.mid(12).toDouble() / 1e6 / total * 100, 0.0, 99.0);
                    emit qualityChanged();
                }
            }
        });
    };
    auto store = [this, tool](const quality::Result &r) {
        if (!r.valid())
            return finishQuality("The measurement gave no scores.");
        m_quality["measured"] = true;
        m_quality["metric"] = quality::metricName(tool);
        m_quality["tool"] = quality::toolName(tool);
        m_quality["mean"] = r.mean;
        m_quality["min"] = r.min;
        m_quality["low"] = r.low;
        m_quality["subsample"] = r.subsample;
        m_quality["verdict"] = quality::verdict(r);
        QVariantList worst;
        for (const quality::Moment &m : r.worst)
            worst << QVariantMap{{"time", m.time}, {"score", m.score}};
        m_quality["worst"] = worst;
        // The curve for the graph, at most 300 points.
        QVariantList curve;
        const int step = std::max<int>(1, int((r.scores.size() + 299) / 300));
        for (int i = 0; i < r.scores.size(); i += step) {
            double sum = 0;
            int n = 0;
            for (int j = i; j < std::min<int>(i + step, r.scores.size()); ++j, ++n)
                sum += r.scores[j];
            curve << sum / n;
        }
        m_quality["curve"] = curve;
        checkAudio();
    };

    const QString log = m_subtitleLink + (tool == quality::Tool::Ssim ? "-quality.log" : "-quality.json");
    QFile::remove(log);
    if (tool != quality::Tool::VmafCli) {
        QProcess *process = qualityProcess();
        progress(process);
        connect(process, &QProcess::finished, this, [this, process, log, tool, fps, store](int code, QProcess::ExitStatus status) {
            process->deleteLater();
            if (m_qualityState != "checking")
                return;
            if (status != QProcess::NormalExit || code != 0)
                return finishQuality("ffmpeg couldn't measure the picture: " + lastLine(process->readAllStandardError()));
            QFile f(log);
            if (!f.open(QIODevice::ReadOnly))
                return finishQuality("The measurement left no scores.");
            store(tool == quality::Tool::Ssim ? quality::parseSsimLog(f.readAll(), fps) : quality::parseVmafLog(f.readAll(), fps));
        });
        process->start("ffmpeg", quality::measureArguments(tool, p, source, m_input, start, len, outputPath(),
                                                           m_outputInfo, log, threads, subsample));
        return;
    }

    // The vmaf tool reads two y4m streams: the reference and the output,
    // each written into a pipe by an ffmpeg of its own.
    const QString refPipe = m_subtitleLink + "-reference.y4m", outPipe = m_subtitleLink + "-output.y4m";
    for (const QString &pipe : {refPipe, outPipe}) {
        QFile::remove(pipe);
        if (::mkfifo(QFile::encodeName(pipe).constData(), 0600) != 0)
            return finishQuality("Couldn't make the pipes for the vmaf tool.");
    }
    QProcess *vmaf = qualityProcess();
    QProcess *reference = qualityProcess();
    QProcess *output = qualityProcess();
    progress(reference);
    auto failed = [this](QProcess *process, const QString &what) {
        connect(process, &QProcess::finished, this, [this, process, what](int code, QProcess::ExitStatus status) {
            if (m_qualityState == "checking" && (status != QProcess::NormalExit || code != 0))
                finishQuality(what + lastLine(process->readAllStandardError()));
        });
    };
    failed(reference, "ffmpeg couldn't read the source for comparing: ");
    failed(output, "ffmpeg couldn't read the converted file for comparing: ");
    connect(vmaf, &QProcess::finished, this, [this, vmaf, log, fps, store](int code, QProcess::ExitStatus status) {
        if (m_qualityState != "checking")
            return;
        if (status != QProcess::NormalExit || code != 0)
            return finishQuality("The vmaf tool stopped: " + lastLine(vmaf->readAll()));
        QFile f(log);
        if (!f.open(QIODevice::ReadOnly))
            return finishQuality("The vmaf tool left no scores.");
        store(quality::parseVmafLog(f.readAll(), fps));
    });
    vmaf->setProcessChannelMode(QProcess::MergedChannels);
    vmaf->start("vmaf", quality::vmafToolArguments(refPipe, outPipe, log, m_outputInfo.height, threads, subsample));
    reference->start("ffmpeg", quality::referenceY4mArguments(p, source, m_input, start, len, m_outputInfo, refPipe));
    output->start("ffmpeg", quality::outputY4mArguments(outputPath(), m_outputInfo, outPipe));
}

void Job::checkAudio() {
    if (m_qualityState != "checking")
        return;
    const QList<quality::AudioPlan> plans = quality::planAudio(runPreset(), runInfo(), trimmed());
    QVariantList audio;
    QList<int> compared;
    for (const quality::AudioPlan &plan : plans) {
        audio << QVariantMap{{"track", plan.track}, {"state", plan.compare ? "pending" : "skipped"},
                             {"text", plan.compare ? QString() : plan.reason}};
        if (plan.compare)
            compared << plan.track;
    }
    m_quality["audio"] = audio;
    if (compared.isEmpty())
        return finishQuality();
    m_qualityStep = "Comparing the audio sample by sample…";
    m_qualityPercent = 99;
    emit qualityChanged();
    // Both files hashed side by side; the comparison waits for the second.
    auto hashes = std::make_shared<QList<QStringList>>(QList<QStringList>{{}, {}});
    auto pending = std::make_shared<int>(2);
    const QStringList files{m_input, outputPath()};
    for (int side = 0; side < 2; ++side) {
        QProcess *process = qualityProcess();
        connect(process, &QProcess::finished, this, [this, process, side, hashes, pending, compared](int, QProcess::ExitStatus) {
            process->deleteLater();
            (*hashes)[side] = quality::parseStreamHashes(process->readAllStandardOutput());
            if (--*pending > 0 || m_qualityState != "checking")
                return;
            QVariantList audio = m_quality.value("audio").toList();
            for (int i = 0; i < audio.size(); ++i) {
                QVariantMap a = audio[i].toMap();
                const int at = int(compared.indexOf(a.value("track").toInt()));
                if (at < 0)
                    continue;
                const QString src = at < (*hashes)[0].size() ? (*hashes)[0][at] : QString();
                const QString out = at < (*hashes)[1].size() ? (*hashes)[1][at] : QString();
                const bool same = !src.isEmpty() && src == out;
                a["state"] = same ? "exact" : "differs";
                a["text"] = same ? "Bit-exact: every sample matches the source."
                                 : src.isEmpty() || out.isEmpty() ? "Couldn't be read for comparing."
                                                                  : "Differs from the source, though it should match.";
                audio[i] = a;
            }
            m_quality["audio"] = audio;
            finishQuality();
        });
        process->start("ffmpeg", quality::audioHashArguments(files[side], compared));
    }
}

void Job::finishQuality(const QString &failure) {
    for (const QPointer<QProcess> &p : std::as_const(m_qualityProcesses))
        if (p) {
            p->disconnect(this);
            if (p->state() != QProcess::NotRunning)
                p->kill();
            p->deleteLater();
        }
    m_qualityProcesses.clear();
    for (const QString &name : {"-reference.y4m", "-output.y4m"})
        QFile::remove(m_subtitleLink + name);
    m_qualityState = failure.isEmpty() ? "done" : "failed";
    m_qualityStep = failure;
    m_qualityPercent = failure.isEmpty() ? 100 : 0;
    if (failure.isEmpty())
        m_quality["report"] = quality::report(m_quality, {});
    emit qualityChanged();
}

QString Job::saveQualityReport() {
    if (m_qualityState != "done")
        return {};
    const QFileInfo out(outputPath());
    const QString path = out.absolutePath() + "/" + out.completeBaseName() + ".quality.txt";
    QStringList header{"Source:    " + m_input, "Converted: " + outputPath(),
                       "Preset:    " + m_preset.name, "Checked:   " + QDateTime::currentDateTime().toString(Qt::ISODate)};
    if (joined())
        header << QString("Joined:    %1 clips").arg(clipCount());
    if (trimmed())
        header << QString("Trimmed:   %1 s to %2 s of the source").arg(m_trimIn, 0, 'f', 2).arg(trimOut(), 0, 'f', 2);
    if (m_outputInfo.valid)
        header << "Output:    " + m_outputInfo.summary();
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return {};
    f.write(quality::report(m_quality, header).toUtf8());
    return path;
}

void Job::showFinishedAt(double seconds) {
    if (m_state != "done")
        return;
    m_finishedWanted = std::max(0.0, seconds);
    makeFinishedStill();
}

void Job::makeFinishedStill() {
    if (m_finishedBusy || m_finishedWanted < 0)
        return;    // a running one picks the latest request up when it ends
    const double at = m_finishedWanted;
    m_finishedWanted = -1;
    m_finishedBusy = true;
    auto *process = new QProcess(this);
    connect(process, &QProcess::finished, this, [this, process, at](int code, QProcess::ExitStatus status) {
        process->deleteLater();
        m_finishedBusy = false;
        if (status == QProcess::NormalExit && code == 0 && QFileInfo::exists(m_finishedStill)) {
            m_finishedTime = at;
            ++m_finishedStamp;
            emit finishedStillChanged();
        }
        makeFinishedStill();
    });
    // Sized and ranged like the Output still, so the two line up.
    process->start("ffmpeg", {"-nostdin", "-hide_banner", "-y", "-loglevel", "error", "-ss", QString::number(at, 'f', 3),
                              "-i", outputPath(), "-frames:v", "1", "-vf",
                              "scale=-2:'min(ih,1080)':flags=bicubic:out_range=full,format=yuvj420p",
                              "-q:v", "3", "-update", "1", m_finishedStill});
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
                         && audioStreamCount(m_preset, runInfo()) > 0;
    if (measure)
        measureLoudness(0);
    else
        encode();
    return true;
}

void Job::measureLoudness(int stream) {
    if (stream >= audioStreamCount(m_preset, runInfo())) {
        m_loudnessReady = true;
        encode();
        return;
    }
    const int count = audioStreamCount(m_preset, runInfo());
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
    process->start("ffmpeg", loudnessArguments(runPreset(), runInfo(), m_input, stream, cut ? m_trimIn : -1,
                                               cut ? length() : -1));
}

void Job::encode() {
    linkSubtitles();
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
    // SVT-AV1 prints its banner whatever ffmpeg's log level; only its
    // errors belong with ffmpeg's.
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("SVT_LOG", "1");
    process->setProcessEnvironment(env);
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
    stopQualityCheck();
    m_qualityState.clear();
    m_quality.clear();
    emit qualityChanged();
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
