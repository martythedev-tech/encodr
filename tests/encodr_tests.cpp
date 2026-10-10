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
#include "browser.h"
#include "hardware.h"
#include "appmenu.h"

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

    void everyAudioTrackAndSubtitleKept() {
        const QByteArray probe = R"({"format": {"format_name": "matroska,webm", "duration": "60.0", "size": "1000"},
            "streams": [
              {"codec_type": "video", "codec_name": "h264", "width": 1920, "height": 1080, "avg_frame_rate": "25/1", "pix_fmt": "yuv420p"},
              {"codec_type": "audio", "codec_name": "aac", "channels": 2, "sample_rate": "48000", "tags": {"language": "eng"}},
              {"codec_type": "audio", "codec_name": "aac", "channels": 2, "sample_rate": "48000", "tags": {"language": "spa"}},
              {"codec_type": "audio", "codec_name": "ac3", "channels": 6, "sample_rate": "48000"},
              {"codec_type": "subtitle", "codec_name": "subrip", "tags": {"language": "eng"}},
              {"codec_type": "subtitle", "codec_name": "hdmv_pgs_subtitle", "tags": {"language": "fra", "title": "Forced"}}
            ]})";
        const MediaInfo info = parseProbe(probe, "/x/film.mkv");
        QCOMPARE(info.audio.size(), 3);
        QCOMPARE(info.subtitles.size(), 2);
        QVERIFY(info.subtitles[0].isText());
        QVERIFY(!info.subtitles[1].isText());
        QCOMPARE(info.subtitles[1].language, QString("fra"));
        QVERIFY(info.summary().contains("2 subtitle tracks"));
        QStringList titles;
        for (const auto &i : inspect(info))
            titles << i.level + ":" + i.title;
        QVERIFY(titles.contains("info:3 audio tracks"));
        QVERIFY(titles.contains("info:1 picture subtitle track"));

        // MP4: all audio; the text subtitle as MP4 text, the picture one left out.
        Command cmd = buildCommand(builtinPresets().first(), info, "/x/film.mkv", "/y/out.mp4");
        QString joined = cmd.arguments.join(' ');
        QVERIFY(joined.contains("-map 0:a:0 -map 0:a:1 -map 0:a:2"));
        QVERIFY(joined.contains("-map 0:s:0 -c:s mov_text"));
        QVERIFY(!joined.contains("0:s:1"));
        QVERIFY(!joined.contains("-sn"));
        QVERIFY(cmd.notes.contains("All 3 audio tracks kept"));
        QVERIFY(cmd.notes.contains("1 picture subtitle track left out: MP4 can't hold it; MKV can"));

        // MKV takes both subtitles as they are.
        Preset mkv = builtinPresets().first();
        mkv.container = "mkv";
        joined = buildCommand(mkv, info, "/x/film.mkv", "/y/out.mkv").arguments.join(' ');
        QVERIFY(joined.contains("-map 0:s:0 -map 0:s:1 -c:s copy"));

        // An audio file holds one track, and says so.
        Preset wav;
        for (const Preset &p : builtinPresets())
            if (p.container == "wav")
                wav = p;
        cmd = buildCommand(wav, info, "/x/film.mkv", "/y/out.wav");
        joined = cmd.arguments.join(' ');
        QVERIFY(joined.contains("-map 0:a:0"));
        QVERIFY(!joined.contains("0:a:1"));
        QVERIFY(joined.contains("-sn"));
        QVERIFY(cmd.notes.contains("Only audio track 1 of 3 converted: a WAV file holds one track"));
    }

    void copyCutsAndRewraps() {
        Preset copy, rewrap;
        for (const Preset &p : builtinPresets()) {
            if (p.id == "copy")
                copy = p;
            if (p.id == "rewrap-mp4")
                rewrap = p;
        }
        QVERIFY(presetProblem(copy).isEmpty());
        QVERIFY(presetProblem(rewrap).isEmpty());

        // A ProRes recording: copying keeps its own MOV; MP4 can't hold ProRes.
        const MediaInfo prores = parseProbe(kKiPro, "/x/CLIP0001.MOV");
        QCOMPARE(containerFor(copy, prores), QString("mov"));
        QVERIFY(sourceProblem(copy, prores).isEmpty());
        QVERIFY(sourceProblem(rewrap, prores).contains("ProRes video can't go into MP4"));

        // A lossless cut from 10 s to 25 s.
        const Command cmd = buildCommand(copy, prores, "/x/CLIP0001.MOV", "/y/out.mov", 10.0, 15.0,
                                         "/tmp/pv.jpg", "/tmp/sv.jpg");
        const QString joined = cmd.arguments.join(' ');
        QVERIFY(joined.contains("-ss 10.000 -i /x/CLIP0001.MOV -t 15.000"));
        QVERIFY(joined.contains("-map 0:V:0 -c:v copy"));
        QVERIFY(joined.contains("-map 0:a:0 -map 0:a:1 -c:a copy"));    // mono tracks stay as they are
        QVERIFY(!joined.contains("-filter_complex"));                    // nothing decoded, no previews
        QVERIFY(!joined.contains("join="));
        QVERIFY(!joined.contains("pv.jpg"));
        QVERIFY(joined.contains("-avoid_negative_ts make_zero"));
        QVERIFY(cmd.notes.contains("Cut without re-encoding: it starts on the keyframe at or before the in point"));

        // PCM audio can't be copied into MP4 either.
        const QByteArray probe = R"({"format": {"duration": "10"}, "streams": [
            {"codec_type": "video", "codec_name": "h264", "width": 1920, "height": 1080, "avg_frame_rate": "25/1"},
            {"codec_type": "audio", "codec_name": "pcm_s24le", "channels": 2, "sample_rate": "48000"}]})";
        const MediaInfo h264pcm = parseProbe(probe, "/x/a.mov");
        QVERIFY(sourceProblem(rewrap, h264pcm).contains("Audio track 1 is uncompressed (PCM), which MP4 can't hold"));
        Preset mkv = rewrap;
        mkv.container = "mkv";
        QVERIFY(sourceProblem(mkv, h264pcm).isEmpty());
    }

    void trimSetsTheRange() {
        Job job(1, "/x/in.mov");
        QCOMPARE(job.trimmed(), false);
        job.setTrim(5, 0);
        QCOMPARE(job.trimIn(), 0.0);    // nothing probed yet: no duration to trim within
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
        QVERIFY(joined.contains("-timecode 01:02:25:12"));    // the trimmed part keeps its place on the clock
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

    void appImageOffersAnAppMenuEntry() {
        QTemporaryDir home, system, config;
        const QString entry = home.filePath("applications/encodr.desktop");

        // Not an AppImage, or installed system-wide: nothing to offer.
        QVERIFY(!AppMenu({}, home.path(), {system.path()}, config.path()).offer());
        writeFile(system.filePath("applications/encodr.desktop"), "[Desktop Entry]\n");
        QVERIFY(!AppMenu("/apps/Encodr.AppImage", home.path(), {system.path()}, config.path()).offer());

        AppMenu menu("/apps/Encodr.AppImage", home.path(), {home.filePath("none")}, config.path());
        QVERIFY(menu.offer());
        QVERIFY(menu.add());
        QVERIFY(!menu.offer());
        QVERIFY(QFileInfo(home.filePath("icons/hicolor/scalable/apps/encodr.svg")).size() > 0);
        QFile f(entry);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QByteArray text = f.readAll();
        QVERIFY(text.contains("Exec=\"/apps/Encodr.AppImage\" %F\n"));
        QVERIFY(text.contains("Icon=encodr\n"));
        f.close();

        // A newer AppImage elsewhere: the entry follows it, without asking.
        AppMenu moved("/new/Encodr-0.3.0.AppImage", home.path(), {home.filePath("none")}, config.path());
        QVERIFY(!moved.offer());
        QVERIFY(f.open(QIODevice::ReadOnly));
        QVERIFY(f.readAll().contains("Exec=\"/new/Encodr-0.3.0.AppImage\" %F"));
        f.close();

        // Declined once stays declined.
        QTemporaryDir home2;
        AppMenu ask("/apps/Encodr.AppImage", home2.path(), {home2.filePath("none")}, config.path());
        ask.decline();
        QVERIFY(!AppMenu("/apps/Encodr.AppImage", home2.path(), {home2.filePath("none")}, config.path()).offer());
    }

    void clockPoliciesSortByNumber() {
        QTemporaryDir sys, proc;
        writeFile(proc.filePath("stat"), "cpu  100 0 100 800 0 0 0 0 0 0\n");
        for (int n : {0, 1, 2, 10, 11}) {
            const QString base = sys.filePath(QString("devices/system/cpu/cpufreq/policy%1/").arg(n));
            writeFile(base + "scaling_cur_freq", "2000000\n");
            writeFile(base + "cpuinfo_max_freq", "4800000\n");
            writeFile(base + "related_cpus", QByteArray::number(n) + "\n");
        }
        Telemetry t(sys.path(), proc.path());
        QStringList order;
        for (const QVariant &c : t.clocks())
            order << c.toMap().value("cpus").toString();
        QCOMPARE(order, (QStringList{"0", "1", "2", "10", "11"}));
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

    void browserListsFoldersFirstAndMarksConverted() {
        if (!hasTool("ffmpeg") || !hasTool("ffprobe"))
            QSKIP("ffmpeg isn't installed");
        QTemporaryDir dir, cache;
        QDir(dir.path()).mkdir("Card 2");
        QDir(dir.path()).mkdir("Card 10");
        QDir(dir.path()).mkdir("card 9");
        QProcess make;
        make.start("ffmpeg", {"-nostdin", "-loglevel", "error", "-f", "lavfi", "-i", "testsrc2=size=320x180:rate=25:duration=3",
                              "-c:v", "mpeg4", dir.filePath("clip.mov")});
        QVERIFY(make.waitForFinished(30000) && make.exitCode() == 0);
        writeFile(dir.filePath("clip_review.mp4"), "not really a video");
        writeFile(dir.filePath("notes.txt"), "hello");
        writeFile(dir.filePath(".clip_proxy.encodr-partial.mov"), "being written");

        writeFile(cache.filePath("mounts"), "/dev/sda1 / ext4 rw 0 0\n");
        Browser browser(cache.filePath("thumbs"), cache.filePath("mounts"));
        browser.setOutputSuffixes({"_review", "_proxy"});
        browser.open(dir.path());

        auto at = [&](int row, int role) { return browser.data(browser.index(row), role); };
        QCOMPARE(browser.rowCount(), 6);    // the hidden partial file isn't listed
        // Folders first, numbers in order whatever the case, then files by name.
        QCOMPARE(at(0, Browser::NameRole).toString(), QString("Card 2"));
        QCOMPARE(at(1, Browser::NameRole).toString(), QString("card 9"));
        QCOMPARE(at(2, Browser::NameRole).toString(), QString("Card 10"));
        browser.open(dir.path());    // the rest of the checks count from clip.mov
        QDir(dir.path()).rmdir("card 9");
        browser.refresh();
        QCOMPARE(browser.rowCount(), 5);
        QCOMPARE(at(2, Browser::NameRole).toString(), QString("clip.mov"));
        QCOMPARE(at(4, Browser::NameRole).toString(), QString("notes.txt"));
        QVERIFY(at(0, Browser::IsDirRole).toBool());
        QVERIFY(!at(4, Browser::IsMediaRole).toBool());
        QCOMPARE(browser.mediaCount(), 2);

        // clip.mov has a review copy beside it; the review copy itself doesn't.
        QVERIFY(at(2, Browser::ConvertedRole).toBool());
        QCOMPARE(at(2, Browser::ConvertedNameRole).toString(), QString("clip_review.mp4"));
        QVERIFY(!at(3, Browser::ConvertedRole).toBool());

        // Details and a thumbnail arrive in the background, and are cached.
        QTRY_VERIFY_WITH_TIMEOUT(at(2, Browser::ReadRole).toBool(), 20000);
        QCOMPARE(at(2, Browser::DetailsRole).toString(), QString("0:03 · 320×180 · MPEG4 · 25p"));
        QVERIFY(QFileInfo::exists(QUrl(at(2, Browser::ThumbRole).toString()).toLocalFile()));
        QTRY_VERIFY_WITH_TIMEOUT(at(3, Browser::ReadRole).toBool(), 20000);
        QCOMPARE(at(3, Browser::DetailsRole).toString(), QString("Can't read this file"));
        QTRY_COMPARE_WITH_TIMEOUT(browser.pending(), 0, 5000);

        Browser again(cache.filePath("thumbs"), cache.filePath("mounts"));
        again.open(dir.path());
        QVERIFY(again.data(again.index(2), Browser::ReadRole).toBool());    // straight from the cache
        QCOMPARE(again.pending(), 0);

        // A new output appearing marks its original.
        QVERIFY(!at(3, Browser::ConvertedRole).toBool());
        browser.up();
        QCOMPARE(browser.path(), QFileInfo(dir.path()).absolutePath());
    }

    void browserPlacesIncludeDrives() {
        QTemporaryDir cache;
        writeFile(cache.filePath("mounts"),
                  "/dev/sda1 / ext4 rw 0 0\n"
                  "/dev/sdb1 /run/media/nobody/My\\040Drive exfat rw 0 0\n"
                  "proc /proc proc rw 0 0\n");
        Browser browser(cache.filePath("thumbs"), cache.filePath("mounts"));
        const auto places = browser.places();
        QVERIFY(places.size() >= 2);
        QCOMPARE(places.first().toMap().value("name").toString(), QString("Home"));
        QCOMPARE(places.last().toMap().value("path").toString(), QString("/"));
        // The drive isn't really mounted here, so it's left out rather than shown broken.
        for (const auto &p : places)
            QVERIFY(p.toMap().value("path").toString() != "/run/media/nobody/My Drive");
    }

    void gpuCommandsForEachFamily() {
        const MediaInfo info = parseProbe(kKiPro, "/x/in.mov");
        Preset p = builtinPresets().first();    // review copy, H.264

        p.hardware = "nvenc";
        QString joined = buildCommand(p, info, "/x/in.mov", "/y/out.mp4").arguments.join(' ');
        QVERIFY2(joined.contains("-c:v h264_nvenc -preset p4 -tune hq -rc vbr -cq 19 -b:v 0 -profile:v high"), qPrintable(joined));
        QVERIFY(joined.contains("-maxrate 16000k"));
        QVERIFY(joined.contains("format=yuv420p[v]"));
        QVERIFY(!joined.contains("libx264"));

        p.hardware = "qsv";
        joined = buildCommand(p, info, "/x/in.mov", "/y/out.mp4").arguments.join(' ');
        QVERIFY2(joined.contains("-c:v h264_qsv -preset fast -global_quality 18"), qPrintable(joined));
        QVERIFY(joined.contains("format=nv12[v]"));

        // VA-API: the device comes first, and only the encoder's branch is
        // uploaded to the GPU; the preview frames stay ordinary pictures.
        p.hardware = "vaapi";
        p.hardwareDevice = "/dev/dri/renderD129";
        const Command cmd = buildCommand(p, info, "/x/in.mov", "/y/out.mp4", -1, -1, "/c/out.jpg", "/c/src.jpg");
        joined = cmd.arguments.join(' ');
        QVERIFY2(joined.indexOf("-vaapi_device /dev/dri/renderD129") < joined.indexOf("-i /x/in.mov"), qPrintable(joined));
        QVERIFY(joined.contains("format=nv12,split=2[vsw][pv];[vsw]hwupload[v]"));
        QVERIFY(joined.contains("-c:v h264_vaapi -rc_mode CQP -qp 18"));
        QVERIFY(!joined.contains("-maxrate"));    // no rate cap in constant-QP mode
        QVERIFY(cmd.notes.contains("Encoded on the GPU (VA-API)"));

        // HEVC keeps 10-bit and the Apple-friendly tag on the GPU too.
        Preset h = builtinPresets()[3];
        QCOMPARE(h.video, QString("hevc"));
        h.hardware = "nvenc";
        joined = buildCommand(h, info, "/x/in.mov", "/y/out.mp4").arguments.join(' ');
        QVERIFY2(joined.contains("format=p010le[v]") && joined.contains("-c:v hevc_nvenc") && joined.contains("-profile:v main10")
                 && joined.contains("-tag:v hvc1"), qPrintable(joined));

        // ProRes never goes to the GPU, whatever the setting.
        Preset prores = builtinPresets()[4];
        prores.hardware = "nvenc";
        QVERIFY(buildCommand(prores, info, "/x/in.mov", "/y/out.mov").arguments.contains("prores_ks"));
    }

    void gpuDetectionParsesFfmpeg() {
        const QByteArray listing =
            "Encoders:\n V..... = Video\n ------\n"
            " V....D libx264              libx264 H.264 / AVC\n"
            " V....D h264_nvenc           NVIDIA NVENC H.264 encoder (codec h264)\n"
            " V....D hevc_vaapi           H.265/HEVC (VAAPI) (codec hevc)\n"
            " A....D aac                  AAC (Advanced Audio Coding)\n";
        const QStringList names = Hardware::parseEncoderList(listing);
        QVERIFY(names.contains("h264_nvenc"));
        QVERIFY(names.contains("hevc_vaapi"));
        QVERIFY(!names.contains("aac"));
        QVERIFY(!names.contains("="));

        const QString vaapi = Hardware::testArguments("vaapi", "hevc", "/dev/dri/renderD128").join(' ');
        QVERIFY(vaapi.contains("-vaapi_device /dev/dri/renderD128"));
        QVERIFY(vaapi.contains("-vf format=p010le,hwupload -c:v hevc_vaapi -profile:v main10 -f null -"));
        const QString h264 = Hardware::testArguments("qsv", "h264", {}).join(' ');
        QVERIFY(h264.contains("-vf format=nv12 -c:v h264_qsv -profile:v high -f null -"));
    }

    void gpuEncodingIsOffUntilSwitchedOn() {
        QTemporaryDir config;
        Backend backend(nullptr, config.path());
        Hardware hardware;
        hardware.setEncoders({{"nvenc", "NVIDIA NVENC", {}, {"h264"}}, {"vaapi", "AMD VA-API", "/dev/dri/renderD128", {"h264", "hevc"}}});
        backend.setHardware(&hardware);
        Preset p = builtinPresets().first();

        QCOMPARE(backend.engineFor(p).first, QString("cpu"));    // experimental: off by default
        backend.setGpuEnabled(true);
        QCOMPARE(backend.engineFor(p).first, QString("nvenc"));  // automatic picks the fastest
        p.video = "hevc";
        QCOMPARE(backend.engineFor(p), qMakePair(QString("vaapi"), QString("/dev/dri/renderD128")));
        p.hardware = "nvenc";                                    // asked for, but it can't do HEVC
        QCOMPARE(backend.engineFor(p).first, QString("cpu"));
        p.hardware = "cpu";
        QCOMPARE(backend.engineFor(p).first, QString("cpu"));
        QVERIFY(Backend::autoParallel() >= 1 && Backend::autoParallel() <= 4);
    }

    // A GPU that refuses the job: the file is redone on the CPU, and says so.
    void failedGpuRunFallsBackToCpu() {
        if (!hasTool("ffmpeg") || !hasTool("ffprobe"))
            QSKIP("ffmpeg is not installed");
        QTemporaryDir dir;
        const QString source = dir.filePath("clip.mov");
        QProcess make;
        make.start("ffmpeg", {"-nostdin", "-loglevel", "error", "-f", "lavfi", "-i", "testsrc2=size=320x180:rate=25:duration=2",
                              "-c:v", "mpeg4", source});
        QVERIFY(make.waitForFinished(30000) && make.exitCode() == 0);

        Job job(1, source);
        Preset p = builtinPresets().first();
        p.speed = "ultrafast";
        job.setPreset(p);
        job.setOutputFolder(dir.filePath("out"));
        job.probe();
        QTRY_VERIFY_WITH_TIMEOUT(job.state() == "ready", 15000);
        job.setEngine("vaapi", dir.filePath("no-such-gpu"));
        QVERIFY(job.commandLine().contains("h264_vaapi"));

        QSignalSpy finished(&job, &Job::finished);
        QVERIFY(job.start());
        QVERIFY(finished.wait(60000));
        QVERIFY2(finished.first().first().toBool(), qPrintable(job.message()));
        QCOMPARE(job.engine(), QString("cpu"));
        QVERIFY(job.notes().first().startsWith("The GPU encoder failed"));
        QVERIFY(QFileInfo::exists(dir.filePath("out/clip_review.mp4")));
    }

    void severalFilesConvertAtOnce() {
        if (!hasTool("ffmpeg") || !hasTool("ffprobe"))
            QSKIP("ffmpeg is not installed");
        QTemporaryDir dir, config;
        QStringList files;
        for (int i = 1; i <= 3; ++i) {
            const QString path = dir.filePath(QString("clip%1.mov").arg(i));
            QProcess make;
            make.start("ffmpeg", {"-nostdin", "-loglevel", "error", "-f", "lavfi", "-i",
                                  "testsrc2=size=640x360:rate=25:duration=6", "-c:v", "mpeg4", path});
            QVERIFY(make.waitForFinished(30000) && make.exitCode() == 0);
            files << path;
        }
        Backend backend(nullptr, config.path());
        backend.setParallel(2);
        backend.addPaths(files);
        auto allReady = [&] {
            for (int i = 0; i < 3; ++i)
                if (backend.jobAt(i)->state() != "ready")
                    return false;
            return true;
        };
        QTRY_VERIFY_WITH_TIMEOUT(allReady(), 20000);
        backend.startQueue();
        QCOMPARE(backend.runningCount(), 2);    // two side by side, the third waits
        QCOMPARE(backend.jobAt(2)->state(), QString("ready"));

        backend.toggleQueue();                  // pause pauses both
        QCOMPARE(backend.jobAt(0)->state(), QString("paused"));
        QCOMPARE(backend.jobAt(1)->state(), QString("paused"));
        backend.toggleQueue();
        QCOMPARE(backend.jobAt(0)->state(), QString("running"));

        QTRY_COMPARE_WITH_TIMEOUT(backend.doneCount(), 3, 120000);
    }

    void gpuStatsFromDriversAndNvidiaSmi() {
        const QVariantList nv = Telemetry::parseNvidiaSmi("NVIDIA GeForce RTX 4070, 37, 82, 2475, 3105, 61, 115.42\n", true);
        QCOMPARE(nv.size(), 1);
        const QVariantMap g = nv.first().toMap();
        QCOMPARE(g["name"].toString(), QString("NVIDIA GeForce RTX 4070"));
        QCOMPARE(g["encoder"].toDouble(), 82.0);
        QCOMPARE(g["maxMhz"].toDouble(), 3105.0);
        QCOMPARE(Telemetry::parseNvidiaSmi("Tesla T4, 5, [N/A], 585, 1590, 40, [N/A]\n", true).first().toMap()["watts"].toDouble(), -1.0);

        QTemporaryDir sys, proc;
        writeFile(proc.filePath("stat"), "cpu  100 0 100 800 0 0 0 0 0 0\ncpu0 100 0 100 800 0 0 0 0 0 0\n");
        writeFile(sys.filePath("class/drm/card0/device/uevent"), "DRIVER=amdgpu\nPCI_ID=1002:744C\n");
        writeFile(sys.filePath("class/drm/card0/device/gpu_busy_percent"), "64\n");
        writeFile(sys.filePath("class/drm/card0/device/pp_dpm_sclk"), "0: 500Mhz\n1: 1854Mhz *\n2: 2500Mhz\n");
        writeFile(sys.filePath("class/drm/card0/device/hwmon/hwmon4/temp1_input"), "58000\n");
        writeFile(sys.filePath("class/drm/card0/device/hwmon/hwmon4/power1_average"), "142000000\n");
        writeFile(sys.filePath("class/drm/card1/device/uevent"), "DRIVER=i915\n");
        writeFile(sys.filePath("class/drm/card1/gt_act_freq_mhz"), "1150\n");
        writeFile(sys.filePath("class/drm/card1/gt_max_freq_mhz"), "1300\n");
        writeFile(sys.filePath("class/drm/card1-DP-1/status"), "connected\n");    // a connector, not a card
        writeFile(sys.filePath("class/drm/card2/device/uevent"), "DRIVER=asahi\n");  // nothing to report

        Telemetry t(sys.path(), proc.path());
        const QVariantList gpus = t.gpus();
        QCOMPARE(gpus.size(), 2);
        const QVariantMap amd = gpus[0].toMap(), intel = gpus[1].toMap();
        QCOMPARE(amd["name"].toString(), QString("AMD GPU"));
        QCOMPARE(amd["busy"].toDouble(), 64.0);
        QCOMPARE(amd["mhz"].toDouble(), 1854.0);
        QCOMPARE(amd["maxMhz"].toDouble(), 2500.0);
        QCOMPARE(amd["celsius"].toDouble(), 58.0);
        QCOMPARE(amd["watts"].toDouble(), 142.0);
        QCOMPARE(intel["name"].toString(), QString("Intel GPU"));
        QCOMPARE(intel["mhz"].toDouble(), 1150.0);
        QCOMPARE(intel["busy"].toDouble(), -1.0);

        // A VA-API job finds its card through the render node's device.
        QDir().mkpath(sys.filePath("class/drm/renderD129"));
        QVERIFY(QFile::link(sys.filePath("class/drm/card0/device"), sys.filePath("class/drm/renderD129/device")));
        QCOMPARE(t.gpuFor("vaapi", "/dev/dri/renderD129")["name"].toString(), QString("AMD GPU"));
        QCOMPARE(t.gpuFor("qsv", {})["name"].toString(), QString("Intel GPU"));
        QVERIFY(t.gpuFor("vaapi", "/dev/dri/renderD128").isEmpty());
        QVERIFY(t.gpuFor("nvenc", {}).isEmpty());
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
    void cutsWithoutReencodingForReal() {
        if (!hasTool("ffmpeg") || !hasTool("ffprobe"))
            QSKIP("ffmpeg is not installed");
        QTemporaryDir dir;
        const QString source = dir.filePath("long.mp4");
        QProcess make;    // 10 s, a keyframe every second
        make.start("ffmpeg", {"-nostdin", "-loglevel", "error", "-f", "lavfi", "-i", "testsrc2=size=320x180:rate=25:duration=10",
                              "-f", "lavfi", "-i", "sine=frequency=440:duration=10", "-ac", "2",
                              "-c:v", "libx264", "-g", "25", "-c:a", "aac", source});
        QVERIFY(make.waitForFinished(60000));
        QCOMPARE(make.exitCode(), 0);

        Job job(1, source);
        for (const Preset &p : builtinPresets())
            if (p.id == "copy")
                job.setPreset(p);
        job.setOutputFolder(dir.filePath("out"));
        job.probe();
        QTRY_VERIFY_WITH_TIMEOUT(job.state() == "ready", 15000);
        job.setTrim(3, 6);
        QVERIFY(job.trimmed());
        QCOMPARE(job.length(), 3.0);
        QCOMPARE(job.totalFrames(), qint64(75));

        QSignalSpy finished(&job, &Job::finished);
        QVERIFY(job.start());
        QVERIFY(finished.wait(60000));
        QVERIFY2(finished.first().first().toBool(), qPrintable(job.message()));
        const QString out = dir.filePath("out/long_copy.mp4");
        QProcess check;
        check.start("ffprobe", {"-v", "error", "-show_entries", "format=duration:stream=codec_name", "-of", "csv=p=0", out});
        QVERIFY(check.waitForFinished(10000));
        const QString result = QString::fromUtf8(check.readAllStandardOutput());
        QVERIFY2(result.contains("h264") && result.contains("aac"), qPrintable(result));
        const double seconds = result.split('\n', Qt::SkipEmptyParts).last().toDouble();
        QVERIFY2(seconds > 2.9 && seconds < 3.2, qPrintable(result));
    }

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
        QVERIFY2(finished.first().first().toBool(), qPrintable(job.message()));
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
