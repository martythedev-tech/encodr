#include <QDir>
#include <QFile>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include "backend.h"
#include "job.h"
#include "mediainfo.h"
#include "preset.h"
#include "progress.h"
#include "telemetry.h"
#include "cyclecounter.h"

#include <atomic>
#include <thread>
#include "theme.h"

namespace {
// Trimmed from ffprobe on a Ki Pro recording: ProRes LT 720p59.94 with blank
// colour tags and two separate mono 24-bit tracks.
const char *kKiPro = R"({
  "streams": [
    {"index": 0, "codec_name": "pcm_s24le", "codec_type": "audio", "sample_rate": "48000", "channels": 1,
     "bits_per_sample": 24, "bits_per_raw_sample": "24", "bit_rate": "1152000"},
    {"index": 1, "codec_name": "pcm_s24le", "codec_type": "audio", "sample_rate": "48000", "channels": 1,
     "bits_per_sample": 24, "bits_per_raw_sample": "24", "bit_rate": "1152000"},
    {"index": 2, "codec_name": "prores", "codec_long_name": "Apple ProRes", "profile": "LT", "codec_type": "video",
     "width": 1280, "height": 720, "pix_fmt": "yuv422p10le", "color_space": "reserved", "color_transfer": "reserved",
     "color_primaries": "reserved", "field_order": "progressive", "r_frame_rate": "2997/50",
     "avg_frame_rate": "2997/50", "bit_rate": "91974880", "bits_per_raw_sample": "10",
     "tags": {"timecode": "01:02:15:13"}},
    {"index": 3, "codec_type": "data", "tags": {"reel_name": "001", "timecode": "01:02:15:13"}}
  ],
  "format": {"format_name": "mov,mp4,m4a,3gp,3g2,mj2", "format_long_name": "QuickTime / MOV",
             "duration": "7310.226894", "size": "86155726947", "bit_rate": "94285146",
             "tags": {"creation_time": "2024-01-01T09:00:00.000000Z"}}
})";

bool hasTool(const QString &tool) {
    QProcess p;
    p.start(tool, {"-version"});
    return p.waitForFinished(5000) && p.exitCode() == 0;
}

void writeFile(const QString &path, const QByteArray &bytes) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(bytes);
}
}

class EncodrTests : public QObject {
    Q_OBJECT

private slots:
    void probeReadsKiProRecording() {
        const MediaInfo info = parseProbe(kKiPro, "/x/CLIP0001.mov");
        QVERIFY(info.valid);
        QCOMPARE(info.videoCodec, QString("prores"));
        QCOMPARE(info.width, 1280);
        QCOMPARE(info.height, 720);
        QVERIFY(qAbs(info.fps - 59.94) < 0.001);
        QCOMPARE(info.timecode, QString("01:02:15:13"));
        QCOMPARE(info.audio.size(), 2);
        QVERIFY(info.splitMono());
        QVERIFY(info.colourTagsBroken());
        QVERIFY(!info.interlaced());
        QCOMPARE(info.estimatedFrames(), qint64(438175));
    }

    void probeRejectsGarbage() {
        const MediaInfo info = parseProbe("not json", "/x/a.mov");
        QVERIFY(!info.valid);
        QVERIFY(!info.error.isEmpty());
    }

    void inspectExplainsFixes() {
        MediaInfo info = parseProbe(kKiPro, "/x/a.mov");
        info.audio[0].meanDb = -30;
        info.audio[0].peakDb = 0.0;
        info.audio[1].meanDb = -90;
        info.audio[1].peakDb = -80;
        QStringList titles;
        for (const auto &i : inspect(info))
            titles << i.level + ":" + i.title;
        QVERIFY(titles.contains("fix:Colour information is blank"));
        QVERIFY(titles.contains("fix:2 separate mono audio tracks"));
        QVERIFY(titles.contains("info:Track 1 peaks at full scale"));
        QVERIFY(titles.contains("warn:Track 2 is silent"));
    }

    void reviewCommandAdaptsToSource() {
        const MediaInfo info = parseProbe(kKiPro, "/x/in.mov");
        const Command cmd = buildCommand(builtinPresets().first(), info, "/x/in.mov", "/y/out.mp4");
        const QString joined = cmd.arguments.join(' ');
        QVERIFY(joined.contains("-bsf:v prores_metadata=color_primaries=bt709:color_trc=bt709:colorspace=bt709"));
        QVERIFY(joined.indexOf("-bsf:v") < joined.indexOf("-i /x/in.mov"));
        QVERIFY(joined.contains("[0:a:0][0:a:1]join=inputs=2:channel_layout=stereo[a]"));
        QVERIFY(joined.contains("-c:v libx264 -preset fast -crf 18"));
        QVERIFY(joined.contains("-maxrate 16000k -bufsize 32000k"));
        QVERIFY(joined.contains("-c:a aac -b:a 320k"));
        QVERIFY(joined.contains("-timecode 01:02:15:13"));
        QVERIFY(joined.contains("-progress pipe:1"));
        QCOMPARE(cmd.arguments.last(), QString("/y/out.mp4"));
        QVERIFY(cmd.notes.contains("Colour labels set to Rec. 709"));
    }

    void timecodeBurnFollowsPreviewStart() {
        const MediaInfo info = parseProbe(kKiPro, "/x/in.mov");
        Preset p = builtinPresets()[1];
        QVERIFY(p.burnTimecode);
        const Command cmd = buildCommand(p, info, "/x/in.mov", "/y/out.mp4", 10.0, 3.0);
        const QString joined = cmd.arguments.join(' ');
        // 10 s at 59.94 fps is 599 frames after 01:02:15:13 (base 60) → 01:02:25:12.
        QVERIFY2(joined.contains("timecode='01\\:02\\:25\\:12'"), qPrintable(joined));
        QVERIFY(joined.contains("rate=2997/50"));
        QVERIFY(!joined.contains("-timecode "));
    }

    void wavPresetIsAudioOnly() {
        const MediaInfo info = parseProbe(kKiPro, "/x/in.mov");
        Preset wav;
        for (const auto &p : builtinPresets())
            if (p.id == "wav")
                wav = p;
        const QStringList a = buildCommand(wav, info, "/x/in.mov", "/y/out.wav").arguments;
        QVERIFY(a.contains("-vn"));
        QVERIFY(a.contains("pcm_s24le"));
        QVERIFY(!a.join(' ').contains("-bsf:v"));
        QVERIFY(!a.contains("-movflags"));
    }

    void presetProblemsAreCaught() {
        Preset p;
        p.container = "mp4";
        p.audio = "pcm24";
        QVERIFY(!presetProblem(p).isEmpty());
        p.container = "mov";
        QVERIFY(presetProblem(p).isEmpty());
        p.container = "wav";
        QVERIFY(!presetProblem(p).isEmpty());
        for (const auto &builtin : builtinPresets())
            QVERIFY2(presetProblem(builtin).isEmpty(), qPrintable(builtin.id));
    }

    void watermarkDropsDangerousCharacters() {
        const MediaInfo info = parseProbe(kKiPro, "/x/in.mov");
        Preset p = builtinPresets().first();
        p.watermark = "DRAFT: it's 100%";
        const QString joined = buildCommand(p, info, "/x/in.mov", "/y/o.mp4").arguments.join(' ');
        QVERIFY2(joined.contains("text='DRAFT its 100'"), qPrintable(joined));
    }

    void progressParserReadsBlocks() {
        ProgressParser parser;
        auto samples = parser.feed("frame=120\nfps=60.5\nstream_0_0_q=23.0\nbitrate=4049.1kbits/s\ntotal_size=1000000\nout_time_us=2002000\n");
        QVERIFY(samples.isEmpty());
        samples = parser.feed("dup_frames=1\ndrop_frames=0\nspeed=1.44x\nprogress=continue\nframe=240\nspeed=N/A\nprogress=end\n");
        QCOMPARE(samples.size(), 2);
        QCOMPARE(samples[0].frame, qint64(120));
        QCOMPARE(samples[0].quantizer, 23.0);
        QVERIFY(qAbs(samples[0].outTime - 2.002) < 1e-9);
        QVERIFY(qAbs(samples[0].speed - 1.44) < 1e-9);
        QVERIFY(qAbs(samples[0].bitrateKbps - 4049.1) < 1e-9);
        QVERIFY(!samples[0].end);
        QCOMPARE(samples[1].frame, qint64(240));
        QCOMPARE(samples[1].speed, 0.0);
        QVERIFY(samples[1].end);
    }

    void estimateBlendsRecentSpeed() {
        const Estimate early = estimate(100, 1, 1, 1000, 1);
        QCOMPARE(early.remaining, -1.0);
        const Estimate e = estimate(100, 50, 50, 5'000'000, 2.0);
        QCOMPARE(e.fraction, 0.5);
        QCOMPARE(e.averageSpeed, 1.0);
        // 0.6 × 2.0 + 0.4 × 1.0 = 1.6×; 50 s of media left → 31.25 s.
        QVERIFY(qAbs(e.remaining - 31.25) < 1e-9);
        QCOMPARE(e.projectedSize, qint64(10'000'000));
    }

    void outputNamesNeverClobber() {
        QTemporaryDir dir;
        const QString path = dir.filePath("clip_review.mp4");
        QCOMPARE(Job::uniquePath(path), path);
        writeFile(path, "x");
        QCOMPARE(Job::uniquePath(path), dir.filePath("clip_review (2).mp4"));
        QCOMPARE(Job::partialPathFor(path), dir.filePath(".clip_review.encodr-partial.mp4"));
    }

    void telemetryReadsFakeMachine() {
        QTemporaryDir sys, proc;
        writeFile(proc.filePath("stat"), "cpu  100 0 100 800 0 0 0 0 0 0\ncpu0 50 0 50 400 0 0 0 0 0 0\ncpu1 50 0 50 400 0 0 0 0 0 0\n");
        writeFile(sys.filePath("class/hwmon/hwmon0/name"), "macsmc_hwmon\n");
        writeFile(sys.filePath("class/hwmon/hwmon0/temp1_input"), "54459\n");
        writeFile(sys.filePath("class/hwmon/hwmon0/temp1_label"), "NAND Flash Temperature\n");
        writeFile(sys.filePath("class/hwmon/hwmon0/power1_input"), "25500322\n");
        writeFile(sys.filePath("class/hwmon/hwmon0/power1_label"), "Total System Power\n");
        writeFile(sys.filePath("devices/system/cpu/cpufreq/policy0/scaling_cur_freq"), "2424000\n");
        writeFile(sys.filePath("devices/system/cpu/cpufreq/policy0/cpuinfo_max_freq"), "3204000\n");
        writeFile(sys.filePath("devices/system/cpu/cpufreq/policy0/related_cpus"), "0 1\n");

        Telemetry t(sys.path(), proc.path());
        writeFile(proc.filePath("stat"), "cpu  280 0 280 820 0 0 0 0 0 0\ncpu0 100 0 100 400 0 0 0 0 0 0\ncpu1 50 0 50 500 0 0 0 0 0 0\n");
        t.sample();
        QVERIFY(qAbs(t.cpuTotal() - 36000.0 / 380) < 0.01);
        QCOMPARE(t.cores().size(), 2);
        QCOMPARE(t.cores()[0].toDouble(), 100.0);
        QCOMPARE(t.cores()[1].toDouble(), 0.0);
        QVERIFY(qAbs(t.systemWatts() - 25.500322) < 1e-6);
        QVERIFY(qAbs(t.hottest() - 54.459) < 1e-6);
        QCOMPARE(t.clocks().size(), 1);
        QVERIFY(t.throttled());   // busy (95%) and 2.4 of 3.2 GHz
        QVERIFY(!t.clocksMeasured());   // no PMUs in the fake machine
    }

    void cpuListsParse() {
        QCOMPARE(CycleCounter::parseCpuList("4-7"), (QList<int>{4, 5, 6, 7}));
        QCOMPARE(CycleCounter::parseCpuList("0 1 2 3"), (QList<int>{0, 1, 2, 3}));
        QCOMPARE(CycleCounter::parseCpuList("0-1,6"), (QList<int>{0, 1, 6}));
        QVERIFY(CycleCounter::parseCpuList("").isEmpty());
    }

    void cycleCounterMeasuresARealThread() {
        CycleCounter counter;
        if (!counter.available())
            QSKIP("No CPU cycle counters on this machine");
        counter.setProcess(QCoreApplication::applicationPid());
        counter.sample();    // opens the counters
        std::atomic<bool> stop = false;
        std::thread spin([&] {
            volatile double x = 0;
            while (!stop)
                x = x + 0.5;
        });
        // The spinning thread only exists now, so the next sample picks it up.
        counter.sample();
        QThread::msleep(400);
        const auto clusters = counter.sample();
        stop = true;
        spin.join();
        double best = 0;
        for (const auto &c : clusters)
            best = std::max(best, c.mhz);
        if (best == 0)
            QSKIP("The kernel refused perf events (perf_event_paranoid?)");
        QVERIFY2(best > 300 && best < 6000, qPrintable(QString::number(best)));
    }

    void themeFallsBack() {
        QTemporaryDir dir;
        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QString("#FFD60A"));
        QCOMPARE(theme.accentForeground(), QString("black"));
        QCOMPARE(theme.background(), QString("#0e0e10"));
    }

    void themeReadsPalette() {
        QTemporaryDir dir;
        writeFile(dir.filePath("theme/colors.toml"),
                  "mode = \"dark\"\naccent = \"#112233\"\nbackground = \"#1a1b26\"\ngreen = \"#9ece6a\"\n");
        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QString("#112233"));
        QCOMPARE(theme.accentForeground(), QString("white"));
        QCOMPARE(theme.background(), QString("#1a1b26"));
        QCOMPARE(theme.good(), QString("#9ece6a"));
    }

    void userPresetsPersist() {
        QTemporaryDir config, media;
        writeFile(media.filePath("a.mov"), "not really a movie");
        {
            Backend backend(nullptr, config.path());
            backend.addPaths({media.filePath("a.mov")});
            QCOMPARE(backend.jobs().size(), 1);
            backend.editJobPreset(0, "crf", 21);
            QCOMPARE(backend.jobAt(0)->presetValue().crf, 21);
            QVERIFY(backend.jobAt(0)->presetValue().name.endsWith("(edited)"));
            const QString id = backend.savePreset(0, "Client Review");
            QCOMPARE(id, QString("user-client-review"));
        }
        Backend again(nullptr, config.path());
        QCOMPARE(again.defaultPresetId(), QString("user-client-review"));
        const Preset p = again.presetById("user-client-review");
        QCOMPARE(p.crf, 21);
        QCOMPARE(p.suffix, QString("_client_review"));
    }

    void removeUndoRedo() {
        QTemporaryDir config, media;
        writeFile(media.filePath("a.wav"), "x");
        writeFile(media.filePath("b.wav"), "x");
        Backend backend(nullptr, config.path());
        backend.addPaths({media.filePath("a.wav"), media.filePath("b.wav")});
        QTRY_VERIFY_WITH_TIMEOUT(backend.jobAt(0)->state() != "probing", 10000);
        backend.removeJob(0);
        QCOMPARE(backend.jobs().size(), 1);
        QVERIFY(backend.canUndo());
        backend.undo();
        QCOMPARE(backend.jobs().size(), 2);
        QCOMPARE(backend.jobAt(0)->fileName(), QString("a.wav"));
        backend.redo();
        QCOMPARE(backend.jobAt(0)->fileName(), QString("b.wav"));
    }

    // The whole path: probe a real file, convert it, and land the result
    // atomically under its final name.
    void convertsARealClip() {
        if (!hasTool("ffmpeg") || !hasTool("ffprobe"))
            QSKIP("ffmpeg is not installed");
        QTemporaryDir dir;
        const QString source = dir.filePath("bars.mov");
        QProcess make;
        make.start("ffmpeg", {"-nostdin", "-loglevel", "error", "-f", "lavfi", "-i", "testsrc2=size=320x180:rate=30:duration=2",
                              "-f", "lavfi", "-i", "sine=frequency=440:duration=2", "-f", "lavfi", "-i", "sine=frequency=660:duration=2",
                              "-map", "0", "-map", "1", "-map", "2", "-c:v", "prores_ks", "-profile:v", "1",
                              "-c:a", "pcm_s24le", "-timecode", "01:00:00:00", source});
        QVERIFY(make.waitForFinished(60000));
        QCOMPARE(make.exitCode(), 0);

        Job job(1, source);
        Preset p = builtinPresets()[1];   // review with burned-in timecode
        p.speed = "ultrafast";
        job.setPreset(p);
        job.setOutputFolder(dir.filePath("out"));
        job.probe();
        QTRY_VERIFY_WITH_TIMEOUT(job.state() == "ready", 15000);
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(job.previewSource()), 15000);   // poster frame
        QVERIFY(job.info().splitMono());
        QCOMPARE(job.info().timecode, QString("01:00:00:00"));

        QSignalSpy finished(&job, &Job::finished);
        QVERIFY(job.start());
        QVERIFY(finished.wait(60000));
        QCOMPARE(finished.first().first().toBool(), true);
        QCOMPARE(job.state(), QString("done"));
        QCOMPARE(job.percent(), 100.0);
        QVERIFY(QFileInfo::exists(dir.filePath("out/bars_review_tc.mp4")));
        QVERIFY(!QFileInfo::exists(Job::partialPathFor(dir.filePath("out/bars_review_tc.mp4"))));

        QProcess check;
        check.start("ffprobe", {"-v", "error", "-show_entries", "stream=codec_name,channels", "-of", "csv=p=0",
                                dir.filePath("out/bars_review_tc.mp4")});
        QVERIFY(check.waitForFinished(10000));
        const QString streams = QString::fromUtf8(check.readAllStandardOutput());
        QVERIFY2(streams.contains("h264"), qPrintable(streams));
        QVERIFY2(streams.contains("aac,2"), qPrintable(streams));

        // The live preview wrote both frames as real JPEGs.
        QVERIFY(job.hasLivePreview());
        QVERIFY(QFileInfo(job.previewOut()).size() > 1000);
        QVERIFY(QFileInfo(job.previewSource()).size() > 1000);
        QFile jpeg(job.previewOut());
        QVERIFY(jpeg.open(QIODevice::ReadOnly));
        QCOMPARE(jpeg.read(2), QByteArray("\xFF\xD8"));
    }

    void previewOutputsComeAfterTheRealOne() {
        const MediaInfo info = parseProbe(kKiPro, "/x/in.mov");
        const QStringList a = buildCommand(builtinPresets().first(), info, "/x/in.mov", "/y/out.mp4", -1, -1,
                                           "/c/out.jpg", "/c/src.jpg").arguments;
        const qsizetype main = a.indexOf("/y/out.mp4");
        QVERIFY(main > 0);
        QVERIFY(a.indexOf("/c/out.jpg") > main);
        QVERIFY(a.indexOf("/c/src.jpg") > main);
        QCOMPARE(a.last(), QString("/c/src.jpg"));
        QVERIFY(a.join(' ').contains("[0:v:0]split=2[vin][sv]"));
        QVERIFY(a.join(' ').contains(",split=2[v][pv]"));
        // Audio-only presets get no preview outputs at all.
        Preset wav;
        for (const auto &p : builtinPresets())
            if (p.id == "wav")
                wav = p;
        QVERIFY(!buildCommand(wav, info, "/x/in.mov", "/y/o.wav", -1, -1, "/c/o.jpg", "/c/s.jpg").arguments.contains("/c/o.jpg"));
    }

    void cancelDeletesThePartialFile() {
        if (!hasTool("ffmpeg"))
            QSKIP("ffmpeg is not installed");
        QTemporaryDir dir;
        const QString source = dir.filePath("long.mkv");
        QProcess make;
        make.start("ffmpeg", {"-nostdin", "-loglevel", "error", "-f", "lavfi", "-i", "testsrc2=size=1280x720:rate=30:duration=60",
                              "-c:v", "ffv1", source});
        QVERIFY(make.waitForFinished(120000));

        Job job(1, source);
        Preset p = builtinPresets().first();
        p.speed = "veryslow";
        job.setPreset(p);
        job.setOutputFolder(dir.path());
        job.probe();
        QTRY_VERIFY_WITH_TIMEOUT(job.state() == "ready", 15000);
        QSignalSpy finished(&job, &Job::finished);
        QVERIFY(job.start());
        QTRY_VERIFY_WITH_TIMEOUT(job.frame() > 0, 30000);
        job.pause();
        QCOMPARE(job.state(), QString("paused"));
        job.resume();
        job.cancel();
        QVERIFY(finished.wait(15000));
        QCOMPARE(job.state(), QString("cancelled"));
        QVERIFY(!QFileInfo::exists(dir.filePath("long_review.mp4")));
        QVERIFY(!QFileInfo::exists(Job::partialPathFor(dir.filePath("long_review.mp4"))));
    }
};

QTEST_MAIN(EncodrTests)
#include "encodr_tests.moc"
