#include "cli.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QRegularExpression>
#include <QTextStream>
#include <QTimer>
#include <atomic>
#include <csignal>
#include <cstdio>
#include <functional>
#include <unistd.h>

#include "backend.h"
#include "hardware.h"
#include "job.h"
#include "naming.h"
#include "preset.h"

namespace cli {
namespace {

const QStringList kCommands{"presets", "probe", "convert", "guide"};

// Settings an agent may change with --set, and the values each one takes
// (empty: any value of the setting's type).
const QHash<QString, QStringList> &choices() {
    static const QHash<QString, QStringList> c{
        {"container", {"source", "mp4", "mov", "mkv", "webm", "wav", "flac", "m4a"}},
        {"video", {"copy", "h264", "hevc", "av1", "vp9", "prores_proxy", "prores_lt", "prores_422", "prores_hq",
                   "dnxhr_lb", "dnxhr_sq", "dnxhr_hq", "dnxhr_hqx", "ffv1", "none"}},
        {"audio", {"copy", "aac", "opus", "mp3", "alac", "flac", "pcm24", "pcm16", "none"}},
        {"loudness", {"off", "ebu", "atsc", "web", "stream"}},
        {"speed", {"ultrafast", "superfast", "veryfast", "faster", "fast", "medium", "slow", "slower", "veryslow"}},
        {"hardware", {"auto", "cpu", "nvenc", "qsv", "vaapi"}},
        {"logoPosition", {"tl", "tr", "bl", "br", "c"}},
        {"crf", {}}, {"maxrateKbps", {}}, {"targetSizeMB", {}}, {"maxHeight", {}}, {"audioKbps", {}},
        {"deinterlace", {}}, {"burnTimecode", {}}, {"watermark", {}}, {"lut", {}}, {"logo", {}},
        {"logoSize", {}}, {"logoOpacity", {}}, {"suffix", {}},
    };
    return c;
}

std::atomic<int> g_interrupted{0};

void onSignal(int) {
    g_interrupted = 1;
}

QTextStream &out() {
    static QTextStream s(stdout);
    return s;
}

QTextStream &err() {
    static QTextStream s(stderr);
    return s;
}

// One event per line, "event" first so a reader sees what it is at once.
void emitJson(QVariantMap event) {
    const QString name = event.take("event").toString();
    QByteArray rest = QJsonDocument(QJsonObject::fromVariantMap(event)).toJson(QJsonDocument::Compact);
    rest.remove(0, 1);    // the opening brace
    out() << "{\"event\":\"" << name << "\"" << (rest == "}" ? "" : ",") << rest << "\n";
    out().flush();
}

// Runs the event loop until `done` is true, polling; false when `timeoutMs`
// (0 = none) or, unless `throughStop`, Ctrl-C came first.
bool waitUntil(const std::function<bool()> &done, int timeoutMs = 0, bool throughStop = false) {
    QEventLoop loop;
    QTimer tick;
    tick.setInterval(25);
    QObject::connect(&tick, &QTimer::timeout, &loop, [&] {
        if (done() || (g_interrupted && !throughStop))
            loop.quit();
    });
    QTimer limit;
    limit.setSingleShot(true);
    QObject::connect(&limit, &QTimer::timeout, &loop, &QEventLoop::quit);
    if (done())
        return true;
    tick.start();
    if (timeoutMs > 0)
        limit.start(timeoutMs);
    loop.exec();
    return done();
}

QString clock(double seconds) {
    if (seconds < 0 || !std::isfinite(seconds))
        return "--:--";
    const qint64 s = qint64(seconds + 0.5);
    return s >= 3600 ? QString("%1:%2:%3").arg(s / 3600).arg(s / 60 % 60, 2, 10, QChar('0')).arg(s % 60, 2, 10, QChar('0'))
                     : QString("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QChar('0'));
}

QString megabytes(qint64 bytes) {
    return QString::number(bytes / 1e6, 'f', bytes < 10e6 ? 1 : 0) + " MB";
}

// --- presets ----------------------------------------------------------------

int presets(QCommandLineParser &parser, Backend &backend, bool json) {
    if (!parser.positionalArguments().isEmpty()) {
        err() << "encodr presets takes no files.\n";
        return 2;
    }
    const QVariantList list = backend.presets();
    if (json) {
        QVariantMap all{{"presets", list}, {"default", backend.defaultPresetId()}};
        out() << QJsonDocument(QJsonObject::fromVariantMap(all)).toJson(QJsonDocument::Indented);
        return 0;
    }
    int width = 0;
    for (const QVariant &v : list)
        width = std::max(width, int(v.toMap().value("id").toString().size()));
    for (const QVariant &v : list) {
        const QVariantMap p = v.toMap();
        out() << p.value("id").toString().leftJustified(width + 2) << p.value("name").toString()
              << (p.value("builtin").toBool() ? "" : "  (yours)")
              << (p.value("id") == backend.defaultPresetId() ? "  (default)" : "") << "\n"
              << QString(width + 2, ' ') << p.value("description").toString() << "\n";
    }
    out() << "\nSettings of each: encodr presets --json\n";
    return 0;
}

// --- probe ------------------------------------------------------------------

int probe(QCommandLineParser &parser, bool json) {
    const QStringList files = parser.positionalArguments();
    if (files.isEmpty()) {
        err() << "encodr probe needs a file.\n";
        return 2;
    }
    int failed = 0;
    QVariantList results;
    for (const QString &file : files) {
        Job job(0, QFileInfo(file).absoluteFilePath());
        job.setExtras(false);
        job.probe();
        waitUntil([&] { return job.state() != "probing"; });
        if (g_interrupted)
            return 130;
        if (job.state() == "ready") {
            job.scanAudioLevels();
            waitUntil([&] { return job.levelsScanned(); }, 120000);
        }
        const bool ok = job.state() == "ready";
        failed += ok ? 0 : 1;
        QVariantMap r{{"file", job.inputPath()}, {"ok", ok}};
        if (ok) {
            r["info"] = job.infoVariant();
            r["issues"] = job.issues();
            r["suggestion"] = job.suggestion();
            // What --subtitles takes for this file: its tracks and the
            // subtitle files beside it, [{value, text}].
            QVariantList subtitles;
            for (const QVariant &v : job.subtitleChoices()) {
                const QString value = v.toMap().value("value").toString();
                if (value != "off" && value != "pick")
                    subtitles << v;
            }
            r["subtitles"] = subtitles;
        } else {
            r["error"] = job.message();
        }
        if (json) {
            results << r;
            continue;
        }
        out() << job.inputPath() << "\n";
        if (!ok) {
            out() << "  Can't read it: " << job.message() << "\n\n";
            continue;
        }
        const MediaInfo &i = job.info();
        out() << "  " << i.container << ", " << clock(i.duration) << ", " << megabytes(i.size) << "\n";
        if (i.hasVideo)
            out() << "  Video: " << i.videoCodec << " " << i.width << "x" << i.height << " " << job.infoVariant().value("fpsText").toString() << " fps, "
                  << i.pixelFormat << (i.interlaced() ? ", interlaced" : "")
                  << (i.colorPrimaries.isEmpty() ? "" : ", " + i.colorPrimaries + "/" + i.colorTransfer + "/" + i.colorSpace)
                  << "\n";
        for (int k = 0; k < i.audio.size(); ++k) {
            const AudioTrack &t = i.audio[k];
            out() << "  Audio " << k + 1 << ": " << t.codec << " " << t.channels << " ch " << t.sampleRate << " Hz"
                  << (t.bits > 0 ? QString(" %1-bit").arg(t.bits) : QString())
                  << (t.language.isEmpty() ? "" : " (" + t.language + ")")
                  << (std::isnan(t.meanDb) ? QString() : QString(", mean %1 dB").arg(t.meanDb, 0, 'f', 1)) << "\n";
        }
        for (const QVariant &v : r["subtitles"].toList())
            out() << "  Subtitles: " << v.toMap().value("text").toString() << "  (--subtitles "
                  << v.toMap().value("value").toString() << ")\n";
        for (const QVariant &v : job.issues()) {
            const QVariantMap issue = v.toMap();
            const QString level = issue.value("level").toString();
            out() << "  " << (level == "warn" ? "Warning" : level == "fix" ? "Handled" : "Note") << ": "
                  << issue.value("title").toString()
                  << (issue.value("detail").toString().isEmpty() ? "" : ". " + issue.value("detail").toString()) << "\n";
        }
        const QVariantMap s = job.suggestion();
        if (!s.isEmpty())
            out() << "  Suggestion: " << s.value("title").toString()
                  << (s.value("presetId").toString().isEmpty() ? "" : " (--preset " + s.value("presetId").toString() + ")")
                  << "\n";
        out() << "\n";
    }
    if (json)
        out() << QJsonDocument(QJsonObject::fromVariantMap({{"files", results}})).toJson(QJsonDocument::Indented);
    return failed ? 1 : 0;
}

// --- convert ----------------------------------------------------------------

struct ConvertOptions {
    Preset preset;
    QString out;
    QString name;
    QString crop = "off";
    QList<int> cropMargins;
    QString pad = "off";
    QString padFill = "black";
    QPair<double, double> trim{0, 0};
    QString subtitles = "off";
    bool check = false;
    bool dryRun = false;
};

// Sets a probed job up the way the options say; returns what's wrong.
QString setUp(Job &job, const ConvertOptions &o) {
    if (o.trim.first > 0 || o.trim.second > 0) {
        const double end = o.trim.second > 0 ? o.trim.second : job.info().duration;
        if (o.trim.first >= job.info().duration || end <= o.trim.first)
            return QString("The trim is outside the file (it's %1 long).").arg(clock(job.info().duration));
        job.setTrim(o.trim.first, std::min(end, job.info().duration));
    }
    if (!o.cropMargins.isEmpty()) {
        job.setCropMargins(o.cropMargins[0], o.cropMargins[1], o.cropMargins[2], o.cropMargins[3]);
    } else if (o.crop != "off") {
        job.setCropMode(o.crop);
        if (o.crop == "bars" && !waitUntil([&] { return job.barsChecked(); }, 120000))
            return g_interrupted ? "Stopped." : "Looking for black bars took too long.";
    }
    if (o.pad != "off") {
        job.setPadMode(o.pad);
        job.setPadFill(o.padFill);
    }
    if (o.subtitles != "off") {
        bool known = false;
        for (const QVariant &v : job.subtitleChoices())
            known = known || v.toMap().value("value").toString() == o.subtitles;
        if (!known && !QFileInfo(o.subtitles).isFile())
            return QString("No subtitles \"%1\" for this file. Use track:N (see encodr probe) or a subtitle file.")
                .arg(o.subtitles);
        job.setSubtitleBurn(o.subtitles);
    }
    return {};
}

QString engineText(const Job &job) {
    const QString e = job.engine();
    if (!job.presetValue().usesVideoEncoder())
        return job.presetValue().audioOnly() ? "audio only" : "CPU";
    if (e == "cpu" || e.isEmpty())
        return "CPU";
    return e == "nvenc" ? "NVIDIA NVENC" : e == "qsv" ? "Intel Quick Sync" : e == "vaapi" ? "VA-API" : e;
}

int convert(QCommandLineParser &parser, Backend &backend, const ConvertOptions &o, bool json) {
    const QStringList files = parser.positionalArguments();
    if (files.isEmpty()) {
        err() << "encodr convert needs at least one file.\n";
        return 2;
    }
    const bool tty = !json && isatty(fileno(stdout));

    // GPU encoders are found by test-encoding a few frames, so only when
    // they could be used; AV1 needs ffmpeg's encoder list.
    Hardware hardware;
    const bool gpu = backend.gpuEnabled() && o.preset.usesVideoEncoder() && o.preset.hardware != "cpu"
                     && !o.preset.fitsSize();
    if (gpu || o.preset.video == "av1") {
        hardware.detect();
        waitUntil([&] { return hardware.ready(); }, 60000);
        backend.setHardware(&hardware);
        if (g_interrupted)
            return 130;
    }

    int done = 0, failed = 0;
    for (const QString &file : files) {
        if (g_interrupted)
            break;
        const QString input = QFileInfo(file).absoluteFilePath();
        Job job(0, input);
        job.setExtras(false);
        job.setPreset(o.preset);
        job.setOutputFolder(o.out.isEmpty() ? backend.folderFor(input) : QFileInfo(o.out).absoluteFilePath());
        job.setNamePattern(o.name.isEmpty() ? backend.namePattern() : naming::clean(o.name));
        job.probe();
        waitUntil([&] { return job.state() != "probing"; });
        if (g_interrupted)
            break;

        auto fail = [&](const QString &why) {
            ++failed;
            if (json)
                emitJson({{"event", "done"}, {"file", input}, {"ok", false}, {"state", "failed"}, {"message", why}});
            else
                err() << QFileInfo(input).fileName() << ": " << why << "\n";
        };
        if (job.state() != "ready") {
            fail(job.message());
            continue;
        }
        const QString problem = setUp(job, o);
        if (!problem.isEmpty()) {
            fail(problem);
            continue;
        }
        const auto [engine, device] = backend.engineFor(job.presetValue());
        job.setEngine(engine, device);
        if (!job.presetProblemText().isEmpty()) {
            fail(job.presetProblemText());
            continue;
        }

        const QVariantMap plan{{"file", input}, {"output", job.outputPath()}, {"preset", job.presetValue().id},
                               {"presetName", job.presetValue().name}, {"engine", job.engine()},
                               {"duration", job.length()}, {"command", job.commandLine()},
                               {"notes", job.notes()}};
        if (o.dryRun) {
            ++done;
            if (json) {
                QVariantMap e = plan;
                e["event"] = "plan";
                emitJson(e);
            } else {
                out() << QFileInfo(input).fileName() << " → " << job.outputPath() << "\n  " << job.presetValue().name
                      << ", " << engineText(job) << "\n  " << job.commandLine() << "\n";
                for (const QString &n : job.notes())
                    out() << "  Note: " << n << "\n";
            }
            continue;
        }

        if (json) {
            QVariantMap e = plan;
            e["event"] = "start";
            emitJson(e);
        } else {
            out() << QFileInfo(input).fileName() << " → " << QFileInfo(job.outputPath()).fileName() << "  ("
                  << job.presetValue().name << ", " << engineText(job) << ")\n";
            out().flush();
        }
        if (!job.start()) {
            fail(job.message().isEmpty() ? "Couldn't start." : job.message());
            continue;
        }

        // Progress: JSON every 2 s, a live line on a terminal, a plain line
        // every 10 s otherwise (a log, an agent reading the output).
        QTimer progress;
        progress.setInterval(json ? 2000 : tty ? 500 : 10000);
        QObject::connect(&progress, &QTimer::timeout, [&] {
            if (!job.isActive())
                return;
            if (json) {
                emitJson({{"event", "progress"}, {"file", input}, {"percent", std::round(job.percent() * 10) / 10},
                          {"speed", std::round(job.speed() * 100) / 100}, {"fps", std::round(job.fps())},
                          {"eta", std::round(job.remaining())}, {"size", job.size()},
                          {"projectedSize", job.projectedSize()}, {"state", job.state()}});
                return;
            }
            const QString line = QString("  %1%  %2x  %3 left  %4")
                                     .arg(job.percent(), 5, 'f', 1)
                                     .arg(job.speed(), 0, 'f', 2)
                                     .arg(clock(job.remaining()), megabytes(job.size()));
            out() << (tty ? "\r" + line.leftJustified(60) : line + "\n");
            out().flush();
        });
        progress.start();
        // Ctrl-C stops ffmpeg; the job then deletes the half-written file.
        bool stopped = false;
        waitUntil(
            [&] {
                if (g_interrupted && !stopped && job.isActive()) {
                    stopped = true;
                    job.cancel();
                }
                return job.isFinished();
            },
            0, true);
        progress.stop();
        if (tty)
            out() << "\r" << QString(62, ' ') << "\r";

        const bool ok = job.state() == "done";
        QVariantMap result{{"file", input}, {"ok", ok}, {"state", job.state()}, {"message", job.message()},
                           {"output", ok ? job.outputPath() : QString()}, {"seconds", std::round(job.elapsed())},
                           {"size", ok ? QFileInfo(job.outputPath()).size() : 0}, {"engine", job.engine()},
                           {"notes", job.notes()}};
        if (ok && !json) {
            out() << "  Done in " << clock(job.elapsed()) << ": " << job.outputPath() << " ("
                  << megabytes(result["size"].toLongLong()) << ")\n";
            for (const QString &n : job.notes())
                out() << "  Note: " << n << "\n";
        }
        if (ok && o.check) {
            job.checkQuality();
            if (!json)
                out() << "  Checking quality…\n";
            out().flush();
            waitUntil([&] { return job.qualityState() != "checking"; });
            if (g_interrupted) {
                job.stopQualityCheck();
            } else {
                const QVariantMap q = job.quality();
                result["quality"] = q;
                result["qualityState"] = job.qualityState();
                if (job.qualityState() == "done")
                    result["qualityReport"] = job.saveQualityReport();
                // An audio track that should have been bit-exact and isn't.
                for (const QVariant &a : q.value("audio").toList())
                    if (a.toMap().value("state").toString() == "differs")
                        result["ok"] = false;
                if (job.qualityState() == "failed")
                    result["ok"] = false;
            }
        }

        result["ok"].toBool() ? ++done : ++failed;
        if (json) {
            result["event"] = "done";
            emitJson(result);
            continue;
        }
        if (!ok) {
            err() << "  " << (job.state() == "cancelled" ? "Stopped" : "Failed") << ": " << job.message() << "\n";
            continue;
        }
        if (result.contains("quality")) {
            const QVariantMap q = result["quality"].toMap();
            if (job.qualityState() != "done") {
                out() << "  Quality check failed: " << job.qualityStep() << "\n";
            } else {
                if (q.value("measured").toBool())
                    out() << "  " << q.value("metric").toString() << " " << QString::number(q.value("mean").toDouble(), 'f', 2)
                          << " average, " << QString::number(q.value("min").toDouble(), 'f', 2) << " lowest. "
                          << q.value("verdict").toString() << "\n";
                else if (!q.value("videoNote").toString().isEmpty())
                    out() << "  " << q.value("videoNote").toString() << "\n";
                for (const QVariant &a : q.value("audio").toList())
                    out() << "  Audio " << a.toMap().value("track").toInt() + 1 << ": " << a.toMap().value("text").toString()
                          << "\n";
                out() << "  Report: " << result["qualityReport"].toString() << "\n";
            }
        }
    }

    if (json)
        emitJson({{"event", "summary"}, {"done", done}, {"failed", failed}, {"interrupted", bool(g_interrupted)}});
    else if (files.size() > 1)
        out() << done << " done, " << failed << " failed.\n";
    if (g_interrupted)
        return 130;
    return failed ? 1 : 0;
}

QString guide() {
    QFile f(":/cli-guide.md");
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString("The guide is missing from this build.\n");
}

}  // namespace

double parseTime(const QString &text) {
    static const QRegularExpression re("^(?:(\\d+):)?(?:(\\d+):)?(\\d+(?:\\.\\d+)?)$");
    const auto m = re.match(text.trimmed());
    if (!m.hasMatch() || text.trimmed().isEmpty())
        return -1;
    // One colon is minutes:seconds, two are hours:minutes:seconds.
    const bool two = !m.captured(2).isEmpty();
    const double h = two ? m.captured(1).toDouble() : 0;
    const double min = two ? m.captured(2).toDouble() : m.captured(1).toDouble();
    const double s = m.captured(3).toDouble();
    if ((two || !m.captured(1).isEmpty()) && s >= 60)
        return -1;
    if (two && min >= 60)
        return -1;
    return h * 3600 + min * 60 + s;
}

QPair<double, double> parseTrim(const QString &text) {
    const int dash = text.indexOf('-');
    if (dash < 0)
        return {-1, -1};
    const QString a = text.left(dash).trimmed(), b = text.mid(dash + 1).trimmed();
    if (a.isEmpty() && b.isEmpty())
        return {-1, -1};
    const double in = a.isEmpty() ? 0 : parseTime(a);
    const double outAt = b.isEmpty() ? 0 : parseTime(b);
    if (in < 0 || outAt < 0 || (outAt > 0 && outAt <= in))
        return {-1, -1};
    return {in, outAt};
}

QString applySettings(QVariantMap &preset, const QStringList &pairs) {
    for (const QString &pair : pairs) {
        const int eq = pair.indexOf('=');
        const QString key = eq > 0 ? pair.left(eq).trimmed() : pair.trimmed();
        const QString value = eq > 0 ? pair.mid(eq + 1).trimmed() : QString();
        if (eq <= 0)
            return QString("--set takes key=value, not \"%1\".").arg(pair);
        if (!choices().contains(key)) {
            QStringList keys = choices().keys();
            keys.sort();
            return QString("There's no setting \"%1\". Settings: %2.").arg(key, keys.join(", "));
        }
        const QStringList allowed = choices().value(key);
        if (!allowed.isEmpty() && !allowed.contains(value))
            return QString("%1 can't be \"%2\". It takes: %3.").arg(key, value, allowed.join(", "));
        const QVariant current = preset.value(key);
        if (current.typeId() == QMetaType::Bool) {
            if (value != "true" && value != "false")
                return QString("%1 takes true or false.").arg(key);
            preset[key] = value == "true";
        } else if (current.typeId() == QMetaType::Int) {
            bool ok = false;
            const int n = value.toInt(&ok);
            if (!ok || n < 0)
                return QString("%1 takes a whole number of 0 or more.").arg(key);
            preset[key] = n;
        } else {
            if ((key == "lut" || key == "logo") && !value.isEmpty() && !QFileInfo(value).isFile())
                return QString("No file at %1.").arg(value);
            preset[key] = (key == "lut" || key == "logo") && !value.isEmpty() ? QFileInfo(value).absoluteFilePath() : value;
        }
    }
    return {};
}

bool wanted(int argc, char *argv[]) {
    return argc > 1 && kCommands.contains(QString::fromLocal8Bit(argv[1]));
}

int run(int argc, char *argv[], const QString &version) {
    QCoreApplication app(argc, argv);
    app.setApplicationName("encodr");
    app.setOrganizationName("encodr");
    app.setApplicationVersion(version);
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    // As the window does: the AppImage's own ffmpeg first.
    const QString here = QCoreApplication::applicationDirPath();
    if (QFileInfo(here + "/ffmpeg").isExecutable() && QFileInfo(here + "/ffprobe").isExecutable())
        qputenv("PATH", (here + ":" + qEnvironmentVariable("PATH")).toLocal8Bit());

    QStringList args = app.arguments();
    const QString command = args.takeAt(1);
    if (command == "guide") {
        out() << guide();
        return 0;
    }

    QCommandLineParser parser;
    parser.setApplicationDescription(QString("encodr %1: see `encodr guide` for everything, with examples.").arg(command));
    parser.addHelpOption();
    const QCommandLineOption json(QStringList{"json"}, "Machine-readable output (JSON; convert writes one event per line).");
    parser.addOption(json);
    QCommandLineOption preset(QStringList{"p", "preset"}, "Preset id (encodr presets). Default: the window's.", "id");
    QCommandLineOption set(QStringList{"set"}, "Change one preset setting, key=value. Repeatable.", "key=value");
    QCommandLineOption outDir(QStringList{"o", "out"}, "Output folder. Default: the window's choice.", "folder");
    QCommandLineOption name(QStringList{"name"}, "File-name pattern, e.g. {name}{suffix} or {name}_{height}p.", "pattern");
    QCommandLineOption crop(QStringList{"crop"}, "off, bars, 1:1, 9:16, 4:5, 16:9, or L,T,R,B pixels.", "mode");
    QCommandLineOption pad(QStringList{"pad"}, "Bars up to an aspect: 16:9, 4:3, 1:1, 4:5, 9:16; add :blur for blurred bars.",
                           "aspect");
    QCommandLineOption trim(QStringList{"trim"}, "Part to convert: IN-OUT, IN- or -OUT (seconds or h:mm:ss).", "range");
    QCommandLineOption subs(QStringList{"subtitles"}, "Burn in subtitles: a value encodr probe lists (track:0 is the first) or a file.", "which");
    QCommandLineOption engine(QStringList{"engine"}, "H.264/HEVC encoder: auto, cpu, nvenc, qsv, vaapi. Default: the window's.",
                              "engine");
    QCommandLineOption check(QStringList{"check"}, "Check the quality after converting (VMAF or SSIM, audio bit-exactness).");
    QCommandLineOption dryRun(QStringList{"dry-run"}, "Show what would run (output path, ffmpeg command) without converting.");
    if (command == "convert")
        parser.addOptions({preset, set, outDir, name, crop, pad, trim, subs, engine, check, dryRun});
    parser.addPositionalArgument("files", command == "presets" ? QString() : "Files to read or convert.", "[files...]");
    if (!parser.parse(args)) {
        err() << parser.errorText() << "\nencodr " << command << " --help shows the options.\n";
        return 2;
    }
    if (parser.isSet("help")) {
        // "Usage: /path/encodr [options]" → "Usage: encodr convert [options]".
        QString help = parser.helpText();
        const int end = help.indexOf('\n');
        const QString files = command == "presets" ? "" : command == "convert" ? " files..." : " file...";
        help.replace(0, end, QString("Usage: encodr %1 [options]%2").arg(command, files));
        out() << help;
        return 0;
    }

    if (command == "probe")
        return probe(parser, parser.isSet(json));

    Backend backend(nullptr);
    backend.setReadOnly(true);
    if (command == "presets")
        return presets(parser, backend, parser.isSet(json));

    ConvertOptions o;
    const QString presetId = parser.isSet(preset) ? parser.value(preset) : backend.defaultPresetId();
    bool known = false;
    for (const QVariant &v : backend.presets())
        known = known || v.toMap().value("id").toString() == presetId;
    if (!known) {
        err() << "There's no preset \"" << presetId << "\". encodr presets lists them.\n";
        return 2;
    }
    QVariantMap p = backend.presetById(presetId).toVariant();
    if (parser.isSet(set)) {
        const QString wrong = applySettings(p, parser.values(set));
        if (!wrong.isEmpty()) {
            err() << wrong << "\n";
            return 2;
        }
        p["id"] = "custom";
        p["builtin"] = false;
        p["name"] = p.value("name").toString() + " (edited)";
    }
    if (parser.isSet(engine)) {
        const QString e = parser.value(engine);
        if (!choices().value("hardware").contains(e)) {
            err() << "--engine takes auto, cpu, nvenc, qsv or vaapi.\n";
            return 2;
        }
        p["hardware"] = e;
        backend.setGpuEnabled(e != "cpu");    // in memory only: read-only
    }
    o.preset = Preset::fromVariant(p);
    o.out = parser.value(outDir);
    o.name = parser.value(name);
    if (parser.isSet(crop)) {
        const QString c = parser.value(crop);
        static const QStringList modes{"off", "bars", "1:1", "9:16", "4:5", "16:9"};
        const QStringList parts = c.split(',');
        if (parts.size() == 4) {
            for (const QString &n : parts) {
                bool ok = false;
                const int v = n.trimmed().toInt(&ok);
                if (!ok || v < 0) {
                    err() << "--crop L,T,R,B takes four whole numbers of pixels.\n";
                    return 2;
                }
                o.cropMargins << v;
            }
        } else if (modes.contains(c)) {
            o.crop = c;
        } else {
            err() << "--crop takes off, bars, 1:1, 9:16, 4:5, 16:9, or L,T,R,B.\n";
            return 2;
        }
    }
    if (parser.isSet(pad)) {
        QString a = parser.value(pad);
        if (a.endsWith(":blur")) {
            o.padFill = "blur";
            a.chop(5);
        }
        static const QStringList aspects{"off", "16:9", "4:3", "1:1", "4:5", "9:16"};
        if (!aspects.contains(a)) {
            err() << "--pad takes 16:9, 4:3, 1:1, 4:5 or 9:16 (add :blur for blurred bars).\n";
            return 2;
        }
        o.pad = a;
    }
    if (parser.isSet(trim)) {
        o.trim = parseTrim(parser.value(trim));
        if (o.trim.first < 0) {
            err() << "--trim takes IN-OUT, IN- or -OUT, in seconds or h:mm:ss (e.g. 1:30-2:45).\n";
            return 2;
        }
    }
    if (parser.isSet(subs)) {
        o.subtitles = parser.value(subs);
        if (QFileInfo(o.subtitles).isFile())
            o.subtitles = QFileInfo(o.subtitles).absoluteFilePath();
    }
    o.check = parser.isSet(check);
    o.dryRun = parser.isSet(dryRun);
    return convert(parser, backend, o, parser.isSet(json));
}

}  // namespace cli
