#include <QDir>
#include <QImage>
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
#include "analysis.h"
#include "naming.h"
#include "quality.h"

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

// A Blu-ray (PGS) subtitle stream: a white 400×60 box at 440,600 on a
// 1280×720 canvas from 1 s to 4 s. ffmpeg can't make picture subtitles
// from text, so the tests write the segments themselves.
QByteArray pgsBox() {
    auto u16 = [](QByteArray &b, int v) { b.append(char(v >> 8)).append(char(v & 0xff)); };
    auto segment = [&](quint32 pts, quint8 type, const QByteArray &data) {
        QByteArray b("PG");
        for (int shift : {24, 16, 8, 0})
            b.append(char((pts >> shift) & 0xff));
        b.append(4, '\0');    // decoding time, unused
        b.append(char(type));
        u16(b, int(data.size()));
        return b + data;
    };
    const int x = 440, y = 600, w = 400, h = 60;
    auto display = [&](quint32 pts, bool show, int number) {
        QByteArray pcs, wds, out;
        u16(pcs, 1280); u16(pcs, 720);
        pcs.append(char(0x10));
        u16(pcs, number);
        pcs.append(char(show ? 0x80 : 0x00)).append('\0').append('\0').append(char(show ? 1 : 0));
        if (show) {
            u16(pcs, 0);
            pcs.append('\0').append('\0');
            u16(pcs, x); u16(pcs, y);
        }
        wds.append(char(1)).append('\0');
        u16(wds, x); u16(wds, y); u16(wds, w); u16(wds, h);
        out += segment(pts, 0x16, pcs) + segment(pts, 0x17, wds);
        if (show) {
            // Palette: 0 transparent, 1 white.
            out += segment(pts, 0x14, QByteArray::fromHex("0000" "00108080" "00" "01eb8080ff"));
            QByteArray rle, ods;
            for (int line = 0; line < h; ++line)
                rle += QByteArray::fromHex("00") + char(0xC0 | (w >> 8)) + char(w & 0xff) + char(1) + QByteArray(2, '\0');
            u16(ods, 0);
            ods.append('\0').append(char(0xC0));
            const int length = int(rle.size()) + 4;
            ods.append(char(length >> 16)).append(char((length >> 8) & 0xff)).append(char(length & 0xff));
            u16(ods, w); u16(ods, h);
            out += segment(pts, 0x15, ods + rle);
        }
        return out + segment(pts, 0x80, {});
    };
    return display(90000, true, 0) + display(4 * 90000, false, 1);
}

Preset presetNamed(const QString &id) {
    for (const Preset &p : builtinPresets())
        if (p.id == id)
            return p;
    return {};
}

// The average brightness (0-255) of the box's area at `seconds`.
int boxBrightness(const QString &file, double seconds) {
    QProcess p;
    QStringList a{"-nostdin", "-loglevel", "error"};
    if (seconds > 0)
        a << "-ss" << QString::number(seconds);    // not on a still image: it can't seek
    p.start("ffmpeg", a + QStringList{"-i", file, "-frames:v", "1",
                       "-vf", "scale=1280:720,crop=400:60:440:600,scale=1:1:flags=area,format=gray", "-f", "rawvideo", "-"});
    p.waitForFinished(30000);
    const QByteArray out = p.readAllStandardOutput();
    return out.isEmpty() ? -1 : quint8(out.at(0));
}

// An H.264 MKV with the PGS box as its only subtitle track.
bool makePgsSource(const QString &dir, const QString &mkv) {
    QFile sup(dir + "/box.sup");
    if (!sup.open(QIODevice::WriteOnly))
        return false;
    sup.write(pgsBox());
    sup.close();
    QProcess make;
    make.start("ffmpeg", {"-nostdin", "-loglevel", "error", "-f", "lavfi", "-i", "color=c=0x204060:size=1280x720:rate=25:duration=6",
                          "-i", sup.fileName(), "-map", "0", "-map", "1", "-c:v", "libx264", "-pix_fmt", "yuv420p",
                          "-c:s", "copy", "-copyts", mkv});    // keep the subtitle at 1 s, not moved to 0
    return make.waitForFinished(60000) && make.exitCode() == 0;
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
        QVERIFY(joined.contains("[0:a:0][0:a:1]join=inputs=2:channel_layout=stereo[a0]"));
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

    void dnxhrPresets() {
        const MediaInfo info = parseProbe(kKiPro, "/x/in.mov");
        Preset lb, hq;
        for (const Preset &p : builtinPresets()) {
            if (p.id == "dnxhr-lb")
                lb = p;
            if (p.id == "dnxhr-hq")
                hq = p;
        }
        QCOMPARE(lb.container, QString("mov"));
        const QString joined = buildCommand(lb, info, "/x/in.mov", "/y/out.mov").arguments.join(' ');
        QVERIFY2(joined.contains("format=yuv422p[v]") || joined.contains("format=yuv422p,"), qPrintable(joined));
        QVERIFY(joined.contains("-c:v dnxhd -profile:v dnxhr_lb"));
        QVERIFY(joined.contains("-c:a pcm_s24le"));
        Preset hqx = hq;
        hqx.video = "dnxhr_hqx";
        QVERIFY(buildCommand(hqx, info, "/x/in.mov", "/y/out.mov").arguments.join(' ').contains("yuv422p10le"));
        Preset mp4 = hq;
        mp4.container = "mp4";
        QVERIFY(!presetProblem(mp4).isEmpty());
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

    void namingPatterns() {
        const QDateTime when(QDate(2026, 10, 11), QTime(15, 30));
        naming::Parts parts{"CLIP0001", "_review", "review", when, 1080};
        QCOMPARE(naming::expand(naming::kDefault, parts), QString("CLIP0001_review"));
        QCOMPARE(naming::expand("{date}_{name}{suffix}", parts), QString("2026-10-11_CLIP0001_review"));
        QCOMPARE(naming::expand("{name}_{height}p_{time}", parts), QString("CLIP0001_1080p_1530"));
        QCOMPARE(naming::expand("{name}{suffix}_{n}", parts, 7), QString("CLIP0001_review_007"));
        // Audio has no height: the part goes with its "p" and one separator.
        naming::Parts audio = parts;
        audio.height = 0;
        QCOMPARE(naming::expand("{name}_{height}p{suffix}", audio), QString("CLIP0001_review"));
        QCOMPARE(naming::expand("{height}p_{name}", audio), QString("CLIP0001"));
        // No folders, and always the source's name in it.
        QCOMPARE(naming::clean("../{name}/x"), QString("..{name}x"));
        QCOMPARE(naming::clean("{date}"), QString("{name}_{date}"));
        QCOMPARE(naming::clean("  "), naming::kDefault);
        // Braces in the source's own name stay as they are.
        parts.name = "take {n}";
        QCOMPARE(naming::expand("{name}{suffix}_{n}", parts, 1), QString("take {n}_review_001"));

        // {n} takes the first free number.
        QTemporaryDir dir;
        parts.name = "CLIP0001";
        QCOMPARE(naming::pathFor(dir.path(), "{name}{suffix}_{n}", parts, "mp4"), dir.filePath("CLIP0001_review_001.mp4"));
        writeFile(dir.filePath("CLIP0001_review_001.mp4"), "x");
        QCOMPARE(naming::pathFor(dir.path(), "{name}{suffix}_{n}", parts, "mp4"), dir.filePath("CLIP0001_review_002.mp4"));

        // Recognising outputs, for the browser's ticks.
        const QStringList suffixes{"_review", "_review_tc", "_proxy"};
        auto is = [&](const QString &pattern, const QString &file) {
            return naming::matcher(pattern, "CLIP0001", suffixes).match(file).hasMatch();
        };
        QVERIFY(is(naming::kDefault, "CLIP0001_review.mp4"));
        QVERIFY(is(naming::kDefault, "CLIP0001_review_tc (2).mp4"));
        QVERIFY(!is(naming::kDefault, "CLIP0001_review_extra.mp4"));
        QVERIFY(!is(naming::kDefault, "CLIP0001.mov"));
        QVERIFY(!is(naming::kDefault, "CLIP00012_review.mp4"));
        QVERIFY(is("{date}_{name}{suffix}", "2026-10-11_CLIP0001_proxy.mov"));
        QVERIFY(is("{name}{suffix}_{n}", "CLIP0001_review_012.mp4"));
        QVERIFY(is("{name}_{height}p{suffix}", "CLIP0001_720p_review.mp4"));
        QVERIFY(is("{name}_{height}p{suffix}", "CLIP0001_review.wav"));    // audio: no height
        QVERIFY(is("{name}_{preset}_{date}", "CLIP0001_prores-422_2026-10-11.mov"));
    }

    void jobsUseTheNamePattern() {
        if (!hasTool("ffmpeg") || !hasTool("ffprobe"))
            QSKIP("ffmpeg is not installed");
        QTemporaryDir dir;
        const QString source = dir.filePath("bars.mp4");
        QProcess make;
        make.start("ffmpeg", {"-nostdin", "-loglevel", "error", "-f", "lavfi", "-i", "testsrc2=size=1920x1080:rate=25:duration=1",
                              "-c:v", "libx264", "-pix_fmt", "yuv420p", source});
        QVERIFY(make.waitForFinished(60000));
        QCOMPARE(make.exitCode(), 0);
        Job job(1, source);
        job.setPreset(builtinPresets()[2]);    // share: up to 1080p
        job.setOutputFolder(dir.path());
        job.probe();
        QTRY_VERIFY_WITH_TIMEOUT(job.state() == "ready", 15000);
        QCOMPARE(job.outputPath(), dir.filePath("bars_share.mp4"));    // no pattern: the default
        QSignalSpy changed(&job, &Job::presetChanged);
        job.setNamePattern("{name}_{height}p_{n}");
        QCOMPARE(changed.count(), 1);
        QCOMPARE(job.outputPath(), dir.filePath("bars_1080p_001.mp4"));
        job.setCropMargins(0, 140, 0, 140);    // the height follows the crop
        QCOMPARE(job.outputPath(), dir.filePath("bars_800p_001.mp4"));
    }

    void rewrapIsSuggestedWhenItFits() {
        auto probe = [](const QString &video, const QStringList &audio, const QString &subtitle = {}) {
            QString streams = QString(R"({"index": 0, "codec_name": "%1", "codec_type": "video", "width": 1920,
                "height": 1080, "pix_fmt": "yuv420p", "r_frame_rate": "25/1", "avg_frame_rate": "25/1"})").arg(video);
            int i = 1;
            for (const QString &a : audio)
                streams += QString(R"(, {"index": %1, "codec_name": "%2", "codec_type": "audio", "sample_rate": "48000",
                    "channels": 2})").arg(i++).arg(a);
            if (!subtitle.isEmpty())
                streams += QString(R"(, {"index": %1, "codec_name": "%2", "codec_type": "subtitle"})").arg(i).arg(subtitle);
            return QString(R"({"streams": [%1], "format": {"format_name": "matroska,webm", "duration": "60",
                "size": "1000000", "bit_rate": "133333"}})").arg(streams).toUtf8();
        };
        auto reason = [&](const QByteArray &json, const QString &path) { return rewrapReason(parseProbe(json, path), path); };

        const QString mkv = reason(probe("h264", {"aac"}), "/x/film.mkv");
        QVERIFY2(mkv.startsWith("The H.264 video and AAC audio already fit an MP4."), qPrintable(mkv));
        QVERIFY(reason(probe("hevc", {"aac", "aac"}), "/x/clip.MOV").startsWith("The HEVC video and AAC audio"));
        QVERIFY(reason(probe("h264", {}), "/x/silent.mkv").startsWith("The H.264 video already fits an MP4. Rewrapping moves it"));
        QVERIFY(reason(probe("h264", {"aac"}, "hdmv_pgs_subtitle"), "/x/film.mkv").contains("Picture subtitles can't go along"));
        // Already an MP4, or something that needs converting: no tip.
        QVERIFY(reason(probe("h264", {"aac"}), "/x/film.mp4").isEmpty());
        QVERIFY(reason(probe("prores", {"aac"}), "/x/film.mov").isEmpty());
        QVERIFY(reason(probe("h264", {"ac3"}), "/x/film.mkv").isEmpty());
        QVERIFY(reason(probe("h264", {"aac", "dts"}), "/x/film.mkv").isEmpty());
        QVERIFY(reason(probe("vp9", {"opus"}), "/x/film.webm").isEmpty());
    }

    void rewrapTipForARealFile() {
        if (!hasTool("ffmpeg") || !hasTool("ffprobe"))
            QSKIP("ffmpeg is not installed");
        QTemporaryDir dir;
        const QString source = dir.filePath("talk.mkv");
        QProcess make;
        make.start("ffmpeg", {"-nostdin", "-loglevel", "error", "-f", "lavfi", "-i", "testsrc2=size=640x360:rate=25:duration=2",
                              "-f", "lavfi", "-i", "sine=frequency=440:duration=2", "-c:v", "libx264", "-pix_fmt", "yuv420p",
                              "-c:a", "aac", source});
        QVERIFY(make.waitForFinished(60000));
        QCOMPARE(make.exitCode(), 0);
        Job job(1, source);
        job.setPreset(builtinPresets().first());    // review: re-encodes
        job.probe();
        QTRY_VERIFY_WITH_TIMEOUT(job.state() == "ready", 15000);
        QCOMPARE(job.suggestion().value("presetId").toString(), QString("rewrap-mp4"));
        QCOMPARE(job.suggestion().value("title").toString(), QString("This only needs a rewrap"));

        // A crop needs the picture re-encoded: no tip while it's set.
        job.setCropMargins(0, 20, 0, 20);
        QVERIFY(job.suggestion().isEmpty());
        job.setCropMode("off");
        QVERIFY(!job.suggestion().isEmpty());

        // Taking the tip: nothing more to suggest.
        for (const Preset &p : builtinPresets())
            if (p.id == "rewrap-mp4")
                job.setPreset(p);
        QVERIFY(job.suggestion().isEmpty());
    }

    void pictureSubtitlesBurnIn() {
        if (!hasTool("ffmpeg") || !hasTool("ffprobe"))
            QSKIP("ffmpeg is not installed");
        QTemporaryDir dir;
        const QString source = dir.filePath("film.mkv");
        QVERIFY(makePgsSource(dir.path(), source));
        QCOMPARE(boxBrightness(source, 2), 49);    // the source's picture, without subtitles

        Job job(1, source);
        job.setPreset(builtinPresets().first());    // review: H.264 MP4
        job.setOutputFolder(dir.filePath("out"));
        job.probe();
        QTRY_VERIFY_WITH_TIMEOUT(job.state() == "ready", 15000);
        QCOMPARE(job.info().subtitles.first().width, 1280);
        bool listed = false;
        for (const QVariant &c : job.subtitleChoices())
            listed |= c.toMap().value("text").toString() == "Track 1 · Blu-ray (PGS) pictures";
        QVERIFY(listed);

        job.setSubtitleBurn("track:0");
        QVERIFY(job.runPreset().burnSubtitlePicture);
        const QString line = job.commandLine();
        QVERIFY2(line.contains("null[sbv];[sbv][0:s:0]overlay=eof_action=pass:format=yuv420"), qPrintable(line));
        QVERIFY(!line.contains("subtitles=filename"));
        QVERIFY(job.notes().contains("Subtitles burned in from subtitle track 1"));

        // The still in the middle of the subtitle: the preroll catches it.
        job.showOutputAt(2.5);
        QTRY_VERIFY_WITH_TIMEOUT(job.outputStillStamp() == 1 && !job.outputStillBusy(), 30000);
        QVERIFY2(job.outputStillError().isEmpty(), qPrintable(job.outputStillError()));
        QVERIFY(boxBrightness(job.outputStill(), 0) > 200);

        QSignalSpy finished(&job, &Job::finished);
        QVERIFY(job.start());
        QVERIFY(finished.wait(120000));
        QVERIFY2(finished.first().first().toBool(), qPrintable(job.message()));
        const QString out = job.outputPath();
        QVERIFY(boxBrightness(out, 2) > 200);      // burned in while it's on screen
        QVERIFY(boxBrightness(out, 0.5) < 80);     // and not before
        QVERIFY(boxBrightness(out, 5) < 80);       // or after
    }

    void pictureSubtitlesScaleToTheVideo() {
        MediaInfo info = parseProbe(kKiPro, "/x/in.mov");    // 1280×720
        SubtitleTrack dvd;
        dvd.codec = "dvd_subtitle";
        dvd.width = 720;
        dvd.height = 480;
        info.subtitles = {dvd};
        Preset p = builtinPresets()[2];
        p.burnSubtitles = "/c/src.mov";
        p.burnSubtitleTrack = 0;
        p.burnSubtitlePicture = true;
        p.burnSubtitleName = "subtitle track 1";
        p.cropTop = 40;
        p.cropBottom = 40;
        const Command cmd = buildCommand(p, info, "/x/in.mov", "/y/out.mp4");
        const QString joined = cmd.arguments.join(' ');
        // Drawn on the picture before the crop, from a canvas stretched to it.
        QVERIFY2(joined.contains("null[sbv];[0:s:0]scale=1280:720[sbs];[sbv][sbs]overlay=eof_action=pass:format=yuv422p10,crop=1280:640:0:40"),
                 qPrintable(joined));
        QVERIFY(cmd.notes.contains("Picture subtitles go on before the crop: any drawn in the cropped-off part are cut"));
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

        // Outputs named by another pattern are recognised too.
        writeFile(dir.filePath("2026-10-11_clip_proxy.mov.part"), "x");    // not a whole name
        browser.setOutputPatterns({"{date}_{name}{suffix}", naming::kDefault});
        QCOMPARE(at(2, Browser::ConvertedNameRole).toString(), QString("clip_review.mp4"));    // the default still counts
        QFile::remove(dir.filePath("clip_review.mp4"));
        writeFile(dir.filePath("2026-10-11_clip_proxy.mov"), "x");
        browser.refresh();
        int clip = -1;
        for (int r = 0; r < browser.rowCount(); ++r)
            if (at(r, Browser::NameRole).toString() == "clip.mov")
                clip = r;
        QCOMPARE(at(clip, Browser::ConvertedNameRole).toString(), QString("2026-10-11_clip_proxy.mov"));
        QFile::remove(dir.filePath("2026-10-11_clip_proxy.mov"));
        QFile::remove(dir.filePath("2026-10-11_clip_proxy.mov.part"));
        writeFile(dir.filePath("clip_review.mp4"), "not really a video");
        browser.refresh();

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
        QVERIFY2(joined.contains("-c:v h264_nvenc -preset p4 -tune hq -rc vbr -cq 24 -b:v 0 -profile:v high"), qPrintable(joined));
        QVERIFY(joined.contains("-maxrate 16000k"));
        QVERIFY(joined.contains("format=yuv420p[v]"));
        QVERIFY(!joined.contains("libx264"));

        p.hardware = "qsv";
        joined = buildCommand(p, info, "/x/in.mov", "/y/out.mp4").arguments.join(' ');
        QVERIFY2(joined.contains("-c:v h264_qsv -preset fast -global_quality 18"), qPrintable(joined));
        // A cap needs a target under it, or Quick Sync ignores the quality (QVBR).
        QVERIFY2(joined.contains("-b:v 15000k -maxrate 16000k -bufsize 32000k"), qPrintable(joined));
        QVERIFY(joined.contains("format=nv12[v]"));

        // VA-API: the device comes first, and only the encoder's branch is
        // uploaded to the GPU; the preview frames stay ordinary pictures.
        p.hardware = "vaapi";
        p.hardwareDevice = "/dev/dri/renderD129";
        const Command cmd = buildCommand(p, info, "/x/in.mov", "/y/out.mp4", -1, -1, "/c/out.jpg", "/c/src.jpg");
        joined = cmd.arguments.join(' ');
        QVERIFY2(joined.indexOf("-vaapi_device /dev/dri/renderD129") < joined.indexOf("-i /x/in.mov"), qPrintable(joined));
        QVERIFY(joined.contains("format=nv12,split=2[vsw][pv];[vsw]hwupload[v]"));
        QVERIFY(joined.contains("-c:v h264_vaapi -rc_mode CQP -qp 24 -profile:v high"));    // crf 18 + 6
        QVERIFY(!joined.contains("-maxrate"));    // no rate cap in constant-QP mode
        QVERIFY(cmd.notes.contains("Encoded on the GPU (VA-API)"));

        // Intel's driver keeps a cap with QVBR, at the same quality number,
        // and gets its best H.264 mode; uncapped it stays on constant QP.
        p.hardwareVendor = "intel";
        joined = buildCommand(p, info, "/x/in.mov", "/y/out.mp4").arguments.join(' ');
        QVERIFY2(joined.contains("-c:v h264_vaapi -rc_mode QVBR -global_quality 24 -quality 1 -profile:v high"), qPrintable(joined));
        QVERIFY2(joined.contains("-b:v 15000k -maxrate 16000k -bufsize 32000k"), qPrintable(joined));
        p.maxrateKbps = 0;
        joined = buildCommand(p, info, "/x/in.mov", "/y/out.mp4").arguments.join(' ');
        QVERIFY2(joined.contains("-c:v h264_vaapi -rc_mode CQP -qp 24 -quality 1 -profile:v high"), qPrintable(joined));
        QVERIFY(!joined.contains("-maxrate"));
        // AMD's driver refuses QVBR: a cap is left out rather than failing.
        p.maxrateKbps = 16000;
        p.hardwareVendor = "amd";
        joined = buildCommand(p, info, "/x/in.mov", "/y/out.mp4").arguments.join(' ');
        QVERIFY2(joined.contains("-c:v h264_vaapi -rc_mode CQP -qp 24 -profile:v high"), qPrintable(joined));
        QVERIFY(!joined.contains("-maxrate") && !joined.contains("-quality"));

        // HEVC keeps 10-bit and the Apple-friendly tag on the GPU too.
        Preset h = builtinPresets()[3];
        QCOMPARE(h.video, QString("hevc"));
        h.hardware = "nvenc";
        joined = buildCommand(h, info, "/x/in.mov", "/y/out.mp4").arguments.join(' ');
        QVERIFY2(joined.contains("-c:v hevc_nvenc -preset p5 -tune hq -rc vbr -cq 28 -b:v 0"), qPrintable(joined));    // crf 22 + 6
        QVERIFY2(joined.contains("format=p010le[v]") && joined.contains("-c:v hevc_nvenc") && joined.contains("-profile:v main10")
                 && joined.contains("-tag:v hvc1"), qPrintable(joined));
        h.hardware = "vaapi";
        joined = buildCommand(h, info, "/x/in.mov", "/y/out.mp4").arguments.join(' ');
        QVERIFY2(joined.contains("-c:v hevc_vaapi -rc_mode CQP -qp 26 -profile:v main10"), qPrintable(joined));    // crf 22 + 4
        h.hardwareVendor = "intel";    // no -quality for HEVC: it gained nothing
        joined = buildCommand(h, info, "/x/in.mov", "/y/out.mp4").arguments.join(' ');
        QVERIFY2(joined.contains("-c:v hevc_vaapi -rc_mode CQP -qp 26 -profile:v main10"), qPrintable(joined));
        h.maxrateKbps = 8000;
        joined = buildCommand(h, info, "/x/in.mov", "/y/out.mp4").arguments.join(' ');
        QVERIFY2(joined.contains("-c:v hevc_vaapi -rc_mode QVBR -global_quality 26 -profile:v main10 -b:v 7500k -maxrate 8000k"), qPrintable(joined));
        h.maxrateKbps = 0;
        h.hardware = "qsv";    // no cap: plain constant quality (ICQ), no bitrate
        joined = buildCommand(h, info, "/x/in.mov", "/y/out.mp4").arguments.join(' ');
        QVERIFY2(joined.contains("-c:v hevc_qsv -preset medium -global_quality 22") && !joined.contains("-b:v"), qPrintable(joined));

        // ProRes never goes to the GPU, whatever the setting.
        Preset prores = presetNamed("prores-proxy");
        prores.hardware = "nvenc";
        QVERIFY(buildCommand(prores, info, "/x/in.mov", "/y/out.mov").arguments.contains("prores_ks"));
    }

    void vaapiVendorFromRenderNode() {
        QTemporaryDir sys;
        QDir(sys.path()).mkpath("renderD128/device");
        QDir(sys.path()).mkpath("renderD129/device");
        writeFile(sys.filePath("renderD128/device/vendor"), "0x8086\n");
        writeFile(sys.filePath("renderD129/device/vendor"), "0x1002\n");
        QCOMPARE(Hardware::vendorOf("/dev/dri/renderD128", sys.path()), QString("intel"));
        QCOMPARE(Hardware::vendorOf("/dev/dri/renderD129", sys.path()), QString("amd"));
        QVERIFY(Hardware::vendorOf("/dev/dri/renderD130", sys.path()).isEmpty());
        QVERIFY(Hardware::vendorOf({}, sys.path()).isEmpty());
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

    void gpuDetectionExplainsAnOldNvidiaDriver() {
        const QByteArray err =
            "[h264_nvenc @ 0x1] Driver does not support the required nvenc API version. Required: 13.1 Found: 13.0\n"
            "[h264_nvenc @ 0x1] The minimum required Nvidia driver for nvenc is 610.00 or newer\n";
        QCOMPARE(Hardware::explainFailure("nvenc", err),
                 QString("NVIDIA GPU found, but this ffmpeg's NVENC needs NVIDIA driver 610 or newer. Update the "
                         "driver, or use a distribution ffmpeg built for the one installed."));
        QVERIFY(Hardware::explainFailure("nvenc", "Cannot load libcuda.so.1\n").isEmpty());    // no NVIDIA GPU: nothing to say
        QVERIFY(Hardware::explainFailure("vaapi", err).isEmpty());
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

    // On a machine with a GPU encoder that works: detection finds it, and a
    // real conversion runs on it end to end (no quiet fallback to the CPU),
    // for each codec it passed; codecs it can't do go to the CPU.
    void gpuConvertsForReal() {
        if (!hasTool("ffmpeg") || !hasTool("ffprobe"))
            QSKIP("ffmpeg is not installed");
        Hardware hardware;
        hardware.detect();
        QTRY_VERIFY_WITH_TIMEOUT(hardware.ready(), 90000);
        for (const QString &problem : hardware.problems())
            qInfo() << "GPU problem:" << problem;
        if (hardware.encoders().isEmpty())
            QSKIP("no GPU encoder works on this machine");
        for (const Hardware::Encoder &e : hardware.encoders())
            qInfo() << "GPU found:" << e.name << e.device << e.codecs;

        QTemporaryDir dir, config;
        const QString source = dir.filePath("clip.mov");
        QProcess make;
        make.start("ffmpeg", {"-nostdin", "-loglevel", "error", "-f", "lavfi", "-i", "testsrc2=size=1280x720:rate=25:duration=3",
                              "-c:v", "mpeg4", "-q:v", "2", source});
        QVERIFY(make.waitForFinished(30000) && make.exitCode() == 0);
        Backend backend(nullptr, config.path());
        backend.setGpuEnabled(true);
        backend.setHardware(&hardware);

        // What "automatic" picks, and a codec no GPU passed goes to the CPU.
        for (const QString &codec : {QStringLiteral("h264"), QStringLiteral("hevc")}) {
            const QString engine = backend.engineFor(presetNamed(codec == "h264" ? "review" : "hevc")).first;
            if (hardware.best(codec).isEmpty()) {
                QCOMPARE(engine, QString("cpu"));
                qInfo() << codec << "goes to the CPU: no GPU encoder passed for it";
            } else {
                QCOMPARE(engine, hardware.best(codec));
            }
        }
        // Every family and codec that passed, chosen by hand ("Runs on").
        int n = 0;
        for (const Hardware::Encoder &e : hardware.encoders()) {
            for (const QString &codec : e.codecs) {
                Preset p = presetNamed(codec == "h264" ? "review" : "hevc");
                p.hardware = e.id;
                QCOMPARE(backend.engineFor(p), qMakePair(e.id, e.device));
                Job job(++n, source);
                job.setPreset(p);
                job.setOutputFolder(dir.filePath(QString("out-%1-%2").arg(e.id, codec)));
                job.probe();
                QTRY_VERIFY_WITH_TIMEOUT(job.state() == "ready", 15000);
                job.setEngine(e.id, e.device);
                QSignalSpy finished(&job, &Job::finished);
                QVERIFY(job.start());
                QVERIFY(finished.wait(90000));
                QVERIFY2(finished.first().first().toBool(), qPrintable(job.message()));
                QVERIFY2(job.engine() == e.id, qPrintable(job.notes().join(" | ")));    // no fallback
                const QStringList notes = job.notes();    // one list: begin and end must come from the same one
                QVERIFY(std::any_of(notes.cbegin(), notes.cend(),
                                    [](const QString &note) { return note.startsWith("Encoded on the GPU"); }));
                QProcess probe;
                probe.start("ffprobe", {"-v", "error", "-select_streams", "v:0", "-show_entries", "stream=codec_name,width,height",
                                        "-of", "csv=p=0", job.outputPath()});
                QVERIFY(probe.waitForFinished(20000));
                QCOMPARE(QString(probe.readAllStandardOutput()).trimmed(), codec + ",1280,720");
                qInfo() << codec << "converted on" << e.name << QFileInfo(job.outputPath()).size() << "bytes";
            }
        }
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

    void loudnessCommands() {
        const QByteArray out = "[Parsed_loudnorm_0 @ 0x1] \n{\n\t\"input_i\" : \"-31.20\",\n\t\"input_tp\" : \"-9.80\",\n"
                               "\t\"input_lra\" : \"4.30\",\n\t\"input_thresh\" : \"-41.50\",\n\t\"target_offset\" : \"0.10\"\n}\n";
        const Loudness m = parseLoudnorm(out);
        QVERIFY(m.valid());
        QCOMPARE(m.input, -31.2);
        QCOMPARE(m.range, 4.3);
        QVERIFY(!parseLoudnorm("{\"input_i\" : \"-inf\", \"input_tp\" : \"-inf\"}").valid());   // a silent track

        const MediaInfo info = parseProbe(kKiPro, "/x/in.mov");    // two mono tracks, joined
        Preset p = builtinPresets().first();
        p.loudness = "ebu";
        QCOMPARE(audioStreamCount(p, info), 1);
        const QString measure = loudnessArguments(p, info, "/x/in.mov", 0, 5, 10).join(' ');
        QVERIFY2(measure.contains("-ss 5.000 -i /x/in.mov -t 10.000"), qPrintable(measure));
        QVERIFY(measure.contains("[0:a:0][0:a:1]join=inputs=2:channel_layout=stereo,loudnorm=I=-23:TP=-1:LRA=11:print_format=json[m]"));

        const Command cmd = buildCommand(p, info, "/x/in.mov", "/y/out.mp4", -1, -1, {}, {}, {m});
        const QString joined = cmd.arguments.join(' ');
        QVERIFY2(joined.contains("join=inputs=2:channel_layout=stereo,loudnorm=I=-23:TP=-1:LRA=7:measured_I=-31.20"
                                 ":measured_TP=-9.80:measured_LRA=4.30:measured_thresh=-41.50:offset=0.10:linear=true,"
                                 "aresample=48000[a0]"), qPrintable(joined));
        QVERIFY(cmd.notes.contains("Loudness set to -23 LUFS (EBU R128), true peak -1 dBTP or lower"));

        Preset copied = p;
        copied.audio = "copy";
        QVERIFY(!presetProblem(copied).isEmpty());
    }

    void loudnessIsMeasuredThenSet() {
        if (!hasTool("ffmpeg") || !hasTool("ffprobe"))
            QSKIP("ffmpeg is not installed");
        QTemporaryDir dir;
        const QString source = dir.filePath("quiet.wav");
        QProcess make;    // a quiet tone, well under -23 LUFS
        make.start("ffmpeg", {"-nostdin", "-loglevel", "error", "-f", "lavfi", "-i", "sine=frequency=1000:duration=8",
                              "-af", "volume=-30dB", "-ac", "2", "-c:a", "pcm_s24le", source});
        QVERIFY(make.waitForFinished(60000));
        QCOMPARE(make.exitCode(), 0);

        Job job(1, source);
        Preset p;
        for (const Preset &b : builtinPresets())
            if (b.container == "wav")
                p = b;
        p.loudness = "ebu";
        job.setPreset(p);
        job.setOutputFolder(dir.filePath("out"));
        job.probe();
        QTRY_VERIFY_WITH_TIMEOUT(job.state() == "ready", 15000);
        QSignalSpy finished(&job, &Job::finished);
        QVERIFY(job.start());
        QVERIFY(finished.wait(60000));
        QVERIFY2(finished.first().first().toBool(), qPrintable(job.message()));

        QProcess check;    // measure the result independently
        check.setProcessChannelMode(QProcess::MergedChannels);
        check.start("ffmpeg", {"-nostdin", "-hide_banner", "-nostats", "-i", dir.filePath("out/quiet.wav"),
                               "-af", "loudnorm=print_format=json", "-f", "null", "-"});
        QVERIFY(check.waitForFinished(30000));
        const Loudness result = parseLoudnorm(check.readAll());
        QVERIFY2(qAbs(result.input + 23) < 0.6, qPrintable(QString::number(result.input)));
        QVERIFY(result.truePeak <= -0.9);
    }

    void analysisReadsTheDetectors() {
        const QByteArray log =
            "[Parsed_freezedetect_2 @ 0x1] lavfi.freezedetect.freeze_start: 0\n"
            "[Parsed_blackdetect_1 @ 0x1] black_start:0 black_end:2 black_duration:2\n"
            "[Parsed_freezedetect_2 @ 0x1] lavfi.freezedetect.freeze_duration: 2\n"
            "[Parsed_freezedetect_2 @ 0x1] lavfi.freezedetect.freeze_end: 2\n"
            "[Parsed_scdet_3 @ 0x1] lavfi.scd.score: 42.608, lavfi.scd.time: 2\n"
            "[Parsed_silencedetect_4 @ 0x1] silence_start: 4.017052\n"
            "[Parsed_scdet_3 @ 0x1] lavfi.scd.score: 28.095, lavfi.scd.time: 5\n"
            "[Parsed_freezedetect_2 @ 0x1] lavfi.freezedetect.freeze_start: 5\n"
            "[Parsed_silencedetect_4 @ 0x1] silence_end: 6.501451 | silence_duration: 2.484399\n";
        const QList<Moment> m = parseAnalysis(log, 8.0);
        QStringList kinds;
        for (const Moment &x : m)
            kinds << x.kind;
        // The freeze inside the black is dropped; the last one runs to the end.
        QCOMPARE(kinds, (QStringList{"black", "cut", "silence", "cut", "freeze"}));
        QCOMPARE(m.last().end, 8.0);
        QVERIFY(qAbs(m[2].end - 6.501451) < 1e-6);

        QStringList titles;
        for (const Issue &i : analysisIssues(m, 8.0))
            titles << i.level + ":" + i.title;
        QVERIFY(titles.contains("warn:Silence 1 time in the middle"));
        QVERIFY(titles.contains("warn:The picture freezes 1 time"));
        QVERIFY(titles.contains("info:Black at the start or end"));
        QVERIFY(titles.contains("info:2 cuts between shots"));

        MediaInfo info;
        info.path = "/x/Reel 1.mov";
        info.fps = 25;
        info.duration = 8;
        info.timecode = "10:00:00:00";
        const QString edl = edlFromCuts(m, info, "Reel 1");
        QVERIFY(edl.startsWith("TITLE: Reel 1\nFCM: NON-DROP FRAME\n"));
        QVERIFY2(edl.contains("001  AX       V     C        10:00:00:00 10:00:02:00 01:00:00:00 01:00:02:00"), qPrintable(edl));
        QVERIFY(edl.contains("002  AX       V     C        10:00:02:00 10:00:05:00 01:00:02:00 01:00:05:00"));
        QVERIFY(edl.contains("003  AX       V     C        10:00:05:00 10:00:08:00 01:00:05:00 01:00:08:00"));
        QVERIFY(edl.contains("* FROM CLIP NAME: Reel 1.mov"));
    }

    void analysesARealClip() {
        if (!hasTool("ffmpeg") || !hasTool("ffprobe"))
            QSKIP("ffmpeg is not installed");
        QTemporaryDir dir;
        const QString source = dir.filePath("problems.mp4");
        QProcess make;    // 2 s black, 3 s moving, 3 s still bars; silent from 4 to 6.5 s
        make.start("ffmpeg", {"-nostdin", "-loglevel", "error", "-f", "lavfi", "-i", "color=black:size=320x180:rate=25:duration=2",
                              "-f", "lavfi", "-i", "testsrc2=size=320x180:rate=25:duration=3",
                              "-f", "lavfi", "-i", "smptebars=size=320x180:rate=25:duration=3",
                              "-f", "lavfi", "-i", "sine=frequency=440:duration=8",
                              "-filter_complex", "[0:v][1:v][2:v]concat=n=3:v=1:a=0,format=yuv420p[v];"
                              "[3:a]volume=enable='between(t,4,6.5)':volume=0[a]",
                              "-map", "[v]", "-map", "[a]", "-c:v", "libx264", "-c:a", "aac", source});
        QVERIFY(make.waitForFinished(60000));
        QCOMPARE(make.exitCode(), 0);
        Job job(1, source);
        job.probe();
        QTRY_VERIFY_WITH_TIMEOUT(job.state() == "ready", 15000);
        job.analyse();
        QVERIFY(job.analysing());
        QTRY_VERIFY_WITH_TIMEOUT(job.analysed(), 30000);
        QCOMPARE(job.cutCount(), 2);
        QStringList kinds;
        for (const QVariant &v : job.moments())
            kinds << v.toMap().value("kind").toString();
        QVERIFY2(kinds.contains("black") && kinds.contains("freeze") && kinds.contains("silence"), qPrintable(kinds.join(',')));
        job.setOutputFolder(dir.filePath("out"));
        const QString edl = job.saveEdl();
        QVERIFY(edl.endsWith("problems_cuts.edl"));
        QVERIFY(QFileInfo(edl).size() > 100);
    }

    void cropFindsBarsAndAspects() {
        MediaInfo info;
        info.width = 1920;
        info.height = 1080;
        const QByteArray log =
            "[Parsed_cropdetect_0 @ 0x1] x1:0 x2:1919 y1:140 y2:939 w:1920 h:800 x:0 y:140 crop=1920:800:0:140\n"
            "[Parsed_cropdetect_1 @ 0x1] x1:0 x2:1919 y1:150 y2:931 w:1920 h:780 x:0 y:150 crop=1920:780:0:150\n"   // a darker scene
            "[Parsed_cropdetect_2 @ 0x1] x1:1919 x2:0 y1:1079 y2:0 w:0 h:0 x:0 y:0 crop=0:0:0:0\n";             // all black: ignored
        QCOMPARE(parseBars(log, info), QMargins(0, 140, 0, 140));    // the largest picture wins
        QCOMPARE(parseBars("[Parsed_cropdetect_0 @ 0x1] x1:0 x2:1919 y1:2 y2:1077 crop=1920:1076:0:2\n", info), QMargins());

        QCOMPARE(Job::centredCrop(1920, 1080, 9, 16), QMargins(656, 0, 658, 0));    // 606×1080: even sizes can be 2 px off centre
        QCOMPARE(Job::centredCrop(1920, 1080, 1, 1), QMargins(420, 0, 420, 0));
        QCOMPARE(Job::centredCrop(1080, 1920, 16, 9), QMargins(0, 656, 0, 658));

        const MediaInfo source = parseProbe(kKiPro, "/x/in.mov");    // 1280×720
        Preset p = builtinPresets()[2];   // share copy: up to 1080p
        p.cropTop = 40;
        p.cropBottom = 40;
        const Command cmd = buildCommand(p, source, "/x/in.mov", "/y/out.mp4");
        const QString joined = cmd.arguments.join(' ');
        QVERIFY2(joined.contains("crop=1280:640:0:40"), qPrintable(joined));
        QVERIFY(cmd.notes.contains("Cropped to 1280×640"));
        Preset copy = p;
        copy.video = "copy";
        QVERIFY(sourceProblem(copy, source).contains("Cropping changes the picture"));
    }

    void padsToAnAspect() {
        // The frame's short side is the picture's; the picture fits inside.
        Padding pad = padding(1920, 1080, 9, 16);
        QCOMPARE(pad.width, 1080);
        QCOMPARE(pad.height, 1920);
        QCOMPARE(pad.pictureWidth, 1080);
        QCOMPARE(pad.pictureHeight, 608);
        QCOMPARE(pad.x, 0);
        QCOMPARE(pad.y, 656);
        pad = padding(1440, 1080, 16, 9);    // 4:3 pillarboxed, not scaled
        QCOMPARE(pad.width, 1920);
        QCOMPARE(pad.height, 1080);
        QCOMPARE(pad.pictureWidth, 1440);
        QCOMPARE(pad.pictureHeight, 1080);
        QCOMPARE(pad.x, 240);
        QCOMPARE(pad.y, 0);
        pad = padding(1920, 1080, 9, 16, 720);    // the size limit holds the frame's short side
        QCOMPARE(pad.width, 720);
        QCOMPARE(pad.height, 1280);
        QCOMPARE(pad.pictureWidth, 720);
        QCOMPARE(pad.pictureHeight, 404);
        QVERIFY(padding(1920, 1080, 16, 9).isNull());     // already that shape
        QVERIFY(padding(1920, 1082, 16, 9).isNull());     // a pixel or two off is rounding
        QVERIFY(padding(1920, 1080, 0, 0).isNull());

        const MediaInfo source = parseProbe(kKiPro, "/x/in.mov");    // 1280×720
        Preset p = builtinPresets()[2];   // share copy: up to 1080p
        p.padAcross = 9;
        p.padDown = 16;
        Command cmd = buildCommand(p, source, "/x/in.mov", "/y/out.mp4");
        QString joined = cmd.arguments.join(' ');
        QVERIFY2(joined.contains("scale=720:404:flags=lanczos,pad=720:1280:0:438:color=black,setsar=1"), qPrintable(joined));
        QVERIFY2(cmd.notes.contains("Padded to 9:16 (720×1280): black bars above and below"), qPrintable(cmd.notes.join('|')));
        QCOMPARE(outputHeight(p, source), 1280);

        // After the crop: a cropped 4:3 picture padded back out to 16:9.
        p.padAcross = 16;
        p.padDown = 9;
        p.cropLeft = p.cropRight = 160;    // 960×720
        p.padFill = "blur";
        cmd = buildCommand(p, source, "/x/in.mov", "/y/out.mp4");
        joined = cmd.arguments.join(' ');
        QVERIFY2(joined.contains("crop=960:720:160:0,split[pbg][pfg];[pbg]scale=320:180:force_original_aspect_ratio=increase,"
                                 "crop=320:180,boxblur=11:2,scale=1280:720,eq=brightness=-0.08[pbb];[pfg]null[pff];"
                                 "[pbb][pff]overlay=160:0:format=yuv422p10,setsar=1"), qPrintable(joined));
        QVERIFY(cmd.notes.contains("Padded to 16:9 (1280×720): blurred picture at the sides"));

        p.cropLeft = p.cropRight = 0;    // already 16:9: nothing added
        cmd = buildCommand(p, source, "/x/in.mov", "/y/out.mp4");
        QVERIFY(!cmd.arguments.join(' ').contains("pad="));
        QVERIFY(!cmd.arguments.join(' ').contains("split[pbg]"));
        QVERIFY(cmd.notes.contains("Already 16:9, so no bars are added"));

        Preset copy = p;
        copy.video = "copy";
        QVERIFY(sourceProblem(copy, source).contains("Adding bars changes the picture"));

        // The job keeps the choice, like the crop, and drops the rewrap tip.
        Job job(1, "/x/in.mov");
        job.setPadMode("9:16");
        job.setPadFill("blur");
        QCOMPARE(job.runPreset().padAcross, 9);
        QCOMPARE(job.runPreset().padDown, 16);
        QCOMPARE(job.runPreset().padFill, QString("blur"));
        QVERIFY(job.suggestion().isEmpty());
        job.setPadMode("7:3");    // not offered: ignored
        QCOMPARE(job.padMode(), QString("9:16"));
        job.setPadMode("off");
        QVERIFY(!job.runPreset().pads());
    }

    void qualityParsesAndPlans() {
        // libvmaf's log: every 2nd frame scored at 25 fps; one weak second.
        QByteArray json = "{\"frames\":[";
        for (int n = 0; n < 250; n += 2)
            json += QByteArray(n ? "," : "") + "{\"frameNum\":" + QByteArray::number(n) + ",\"metrics\":{\"vmaf\":"
                    + (n >= 100 && n < 125 ? "70.0" : "95.0") + "}}";
        json += "],\"pooled_metrics\":{}}";
        quality::Result r = quality::parseVmafLog(json, 25);
        QCOMPARE(r.scores.size(), 125);
        QCOMPARE(r.subsample, 2);
        QCOMPARE(r.times[1], 0.08);
        QCOMPARE(r.min, 70.0);
        QCOMPARE(r.low, 70.0);
        QVERIFY(r.mean > 91 && r.mean < 93);
        QCOMPARE(r.worst.first().time, 4.0);    // frames 100-124 are second 4
        QCOMPARE(r.worst.first().score, 70.0);
        for (int i = 1; i < r.worst.size(); ++i)
            QVERIFY(std::abs(r.worst[i].time - 4.0) >= 3);
        QVERIFY(quality::verdict(r).startsWith("Very good"));    // read on VMAF's scale, not SSIM's
        for (const quality::Moment &m : r.worst)
            QVERIFY(m.score < r.mean);                            // only seconds below the average
        QVERIFY(quality::verdict(r).contains("Some moments dip"));

        const quality::Result ssim = quality::parseSsimLog(
            "n:1 Y:0.991 U:0.995 V:0.996 All:0.993 (21.5)\nn:2 Y:0.981 U:0.99 V:0.99 All:0.985 (18.2)\n", 50);
        QCOMPARE(ssim.scores, (QList<double>{99.3, 98.5}));
        QCOMPARE(ssim.times, (QList<double>{0, 0.02}));
        QVERIFY(quality::verdict(ssim).startsWith("Very good"));

        QCOMPARE(quality::subsampleFor(1000), 1);
        QCOMPARE(quality::subsampleFor(172800), 9);    // two hours at 24 fps
        QCOMPARE(quality::parseStreamHashes("0,a,MD5=abc\n1,a,MD5=def\n"), (QStringList{"abc", "def"}));

        // The reference is the source through the same processing as the copy.
        const MediaInfo source = parseProbe(kKiPro, "/x/in.mov");    // 1280×720
        Preset p = presetNamed("share");
        p.cropTop = p.cropBottom = 40;
        MediaInfo out;
        out.width = 1280;
        out.height = 640;
        out.fps = 25;
        out.fpsRational = "25/1";
        out.pixelFormat = "yuv420p";
        QString joined = quality::measureArguments(quality::Tool::LibVmaf, p, source, "/x/in.mov", 10, 20, "/y/out.mp4", out,
                                                   "/c/q.json", 8, 2).join(' ');
        QVERIFY2(joined.contains("-ss 10.000 -i /x/in.mov -t 20.000 -i /y/out.mp4"), qPrintable(joined));
        QVERIFY2(joined.contains("[0:v:0]crop=1280:640:0:40[ref];[ref]setpts=PTS-STARTPTS,fps=25/1,scale=1280:640:flags=bicubic,"
                                 "format=yuv420p[r];[1:v:0]setpts=PTS-STARTPTS,fps=25/1,format=yuv420p[d];"
                                 "[d][r]libvmaf=log_fmt=json:log_path=/c/q.json:n_threads=8:n_subsample=2"), qPrintable(joined));
        QVERIFY(joined.endsWith("-map [q] -f null -"));
        if (!quality::modelPath(640).isEmpty())
            QVERIFY(joined.contains(":model='path=" + quality::modelPath(640) + "'"));
        joined = quality::measureArguments(quality::Tool::Ssim, p, source, "/x/in.mov", -1, -1, "/y/out.mp4", out,
                                           "/c/q.log", 8, 1).join(' ');
        QVERIFY(joined.contains("[d][r]ssim=stats_file=/c/q.log[q]"));
        const QString tool = quality::vmafToolArguments("/c/r.y4m", "/c/d.y4m", "/c/q.json", 2160, 4, 3).join(' ');
        QVERIFY2(tool.startsWith("--reference /c/r.y4m --distorted /c/d.y4m --threads 4 --subsample 3 --json --output /c/q.json"),
                 qPrintable(tool));
        QVERIFY(tool.contains("vmaf_4k_v0.6.1"));
        out.pixelFormat = "yuv420p10le";
        QCOMPARE(quality::comparisonFormat(out), QString("yuv420p10le"));

        // Audio that should be bit-exact, and why the rest isn't compared.
        QVERIFY(quality::planAudio(presetNamed("wav"), source, false)[0].reason.contains("Two mono tracks"));
        MediaInfo stereo = source;
        stereo.audio = {AudioTrack{0, "pcm_s24le", 48000, 2, "stereo", {}, 24}};
        Preset wav = presetNamed("wav");
        QList<quality::AudioPlan> plans = quality::planAudio(wav, stereo, false);    // 24-bit PCM to 24-bit WAV
        QCOMPARE(plans.size(), 1);
        QVERIFY2(plans[0].compare, qPrintable(plans[0].reason));
        QVERIFY(!quality::planAudio(wav, stereo, true)[0].compare);
        Preset short16 = wav;
        short16.audio = "pcm16";
        QCOMPARE(quality::planAudio(short16, stereo, false)[0].reason, QString("Stored at 16-bit from a 24-bit source."));
        Preset review = presetNamed("review");
        plans = quality::planAudio(review, stereo, false);
        QVERIFY(!plans.isEmpty() && !plans[0].compare && plans[0].reason.contains("lossy by design"));
        Preset loud = wav;
        loud.loudness = "ebu";
        QCOMPARE(quality::planAudio(loud, stereo, false)[0].reason, QString("Loudness changed on purpose."));
        QVERIFY(quality::planAudio(presetNamed("copy"), stereo, false)[0].compare);

        const QString text = quality::report({{"measured", true}, {"metric", "VMAF"}, {"tool", "the vmaf tool"}, {"mean", 94.5},
                                              {"low", 90.25}, {"min", 88.0}, {"subsample", 1}, {"verdict", "Excellent."},
                                              {"worst", QVariantList{QVariantMap{{"time", 3725.0}, {"score", 88.5}}}},
                                              {"audio", QVariantList{QVariantMap{{"track", 0}, {"text", "Bit-exact."}}}}},
                                             {"Source:    /x/in.mov"});
        QVERIFY2(text.contains("Average   94.50") && text.contains("Worst 1%  90.25 and below") && text.contains("1:02:05  88.50")
                 && text.contains("Track 1: Bit-exact.") && text.contains("Source:    /x/in.mov"), qPrintable(text));
    }

    // A real conversion, cropped (so the reference has to be processed the
    // same way to score well), checked with each tool this machine has.
    void qualityCheckForReal() {
        if (!hasTool("ffmpeg") || !hasTool("ffprobe"))
            QSKIP("ffmpeg is not installed");
        QTemporaryDir dir;
        QProcess make;
        make.start("ffmpeg", {"-nostdin", "-loglevel", "error", "-f", "lavfi", "-i", "testsrc2=size=320x180:rate=25:duration=3",
                              "-f", "lavfi", "-i", "sine=frequency=440:duration=3:sample_rate=48000", "-c:v", "libx264",
                              "-crf", "0", "-pix_fmt", "yuv420p", "-c:a", "pcm_s24le", "-shortest", dir.filePath("src.mov")});
        QVERIFY(make.waitForFinished(30000));
        QCOMPARE(make.exitCode(), 0);

        Job job(1, dir.filePath("src.mov"));
        Preset p = presetNamed("review");
        p.container = "mkv";
        p.audio = "flac";    // lossless from 24-bit PCM: should be bit-exact
        p.speed = "ultrafast";
        job.setPreset(p);
        job.setOutputFolder(dir.filePath("out"));
        job.probe();
        QTRY_VERIFY_WITH_TIMEOUT(job.state() == "ready", 15000);
        job.setCropMargins(0, 20, 0, 20);
        QSignalSpy finished(&job, &Job::finished);
        QVERIFY(job.start());
        QVERIFY(finished.wait(60000));
        QVERIFY2(finished.first().first().toBool(), qPrintable(job.message()));

        QStringList tools{"ssim"};
        QProcess filters;
        filters.start("ffmpeg", {"-hide_banner", "-filters"});
        filters.waitForFinished(10000);
        if (filters.readAllStandardOutput().contains(" libvmaf "))
            tools << "libvmaf";
        if (hasTool("vmaf") && !quality::modelPath(140).isEmpty())
            tools << "vmaf";
        for (const QString &tool : tools) {
            qputenv("ENCODR_QUALITY_TOOL", tool.toUtf8());
            job.checkQuality();
            QCOMPARE(job.qualityState(), QString("checking"));
            QTRY_VERIFY_WITH_TIMEOUT(job.qualityState() != "checking", 60000);
            QVERIFY2(job.qualityState() == "done", qPrintable(tool + ": " + job.qualityStep()));
            const QVariantMap q = job.quality();
            QCOMPARE(q.value("metric").toString(), QString(tool == "ssim" ? "SSIM" : "VMAF"));
            // High: the copy is measured against the cropped source.
            QVERIFY2(q.value("mean").toDouble() > 90, qPrintable(tool + " " + q.value("mean").toString()));
            QVERIFY(!q.value("curve").toList().isEmpty());
            const QVariantMap audio = q.value("audio").toList().value(0).toMap();
            QCOMPARE(audio.value("state").toString(), QString("exact"));
        }
        qunsetenv("ENCODR_QUALITY_TOOL");

        const QString report = job.saveQualityReport();
        QVERIFY(report.endsWith("src_review.quality.txt"));
        QFile f(report);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QString text = QString::fromUtf8(f.readAll());
        QVERIFY2(text.contains("Bit-exact") && text.contains("Preset:    Review copy"), qPrintable(text));

        // A frame of the finished file for the wipe.
        QSignalSpy still(&job, &Job::finishedStillChanged);
        job.showFinishedAt(1.0);
        QVERIFY(still.wait(20000));
        QCOMPARE(QImage(job.finishedStill()).size(), QSize(320, 140));
    }

    void av1AndFfv1() {
        const MediaInfo info = parseProbe(kKiPro, "/x/in.mov");    // 1280×720 ProRes 422 HQ, 10-bit 4:2:2
        Preset av1 = presetNamed("av1");
        QCOMPARE(av1.video, QString("av1"));
        QVERIFY(presetProblem(av1).isEmpty());
        Command cmd = buildCommand(av1, info, "/x/in.mov", "/y/out.mp4");
        QString joined = cmd.arguments.join(' ');
        // The quality slider + 15 on SVT-AV1's scale; 10-bit, the effort mapped.
        QVERIFY2(joined.contains("format=yuv420p10le[v]"), qPrintable(joined));
        QVERIFY2(joined.contains("-c:v libsvtav1 -preset 8 -crf 33"), qPrintable(joined));
        QVERIFY(!joined.contains("-maxrate"));
        av1.speed = "slow";
        av1.maxrateKbps = 8000;
        joined = buildCommand(av1, info, "/x/in.mov", "/y/out.mp4").arguments.join(' ');
        QVERIFY2(joined.contains("-c:v libsvtav1 -preset 6 -crf 33 -maxrate 8000k -bufsize 16000k"), qPrintable(joined));
        av1.container = "mov";
        QVERIFY(presetProblem(av1).contains("MOV can't hold AV1"));
        av1.container = "webm";
        av1.audio = "opus";
        QVERIFY(presetProblem(av1).isEmpty());    // WebM takes AV1 as well as VP9

        // Without SVT-AV1 in this ffmpeg, AV1 is refused up front, saying why.
        QTemporaryDir config;
        Backend backend(nullptr, config.path());
        Hardware hardware;
        backend.setHardware(&hardware);
        QCOMPARE(backend.engineFor(presetNamed("av1")).first, QString("cpu"));    // not listed yet: no guessing
        hardware.setBuiltEncoders({"libx264", "libx265"});
        QCOMPARE(backend.engineFor(presetNamed("av1")).first, QString("missing"));
        Job job(1, "/x/in.mov");
        job.setPreset(presetNamed("av1"));
        job.setEngine("missing");
        QVERIFY(job.presetProblemText().contains("built without SVT-AV1"));
        if (hasTool("ffmpeg")) {
            // Start with only such files waiting says why instead of nothing.
            QTemporaryDir media;
            QProcess make;
            make.start("ffmpeg", {"-nostdin", "-loglevel", "error", "-f", "lavfi", "-i", "testsrc2=size=160x90:rate=25:duration=1",
                                  "-c:v", "libx264", media.filePath("a.mp4")});
            QVERIFY(make.waitForFinished(30000) && make.exitCode() == 0);
            backend.addPaths({media.filePath("a.mp4")});
            QTRY_VERIFY_WITH_TIMEOUT(backend.jobAt(0) && backend.jobAt(0)->isWaiting(), 15000);
            hardware.setBuiltEncoders({"libx264"});
            backend.setJobPreset(0, "av1");
            backend.startQueue();
            QVERIFY(!backend.running());
            QVERIFY2(backend.notice().startsWith("A waiting file can't start: This ffmpeg can't make AV1"), qPrintable(backend.notice()));
            hardware.setBuiltEncoders({"libx264", "libsvtav1"});
        }
        hardware.setBuiltEncoders({"libx264", "libsvtav1"});
        QCOMPARE(backend.engineFor(presetNamed("av1")).first, QString("cpu"));

        // FFV1 keeps the source's own pixel format, all intra with checksums.
        const Preset ffv1 = presetNamed("ffv1");
        cmd = buildCommand(ffv1, info, "/x/in.mov", "/y/out.mkv");
        joined = cmd.arguments.join(' ');
        QVERIFY2(joined.contains("format=" + info.pixelFormat + "[v]"), qPrintable(joined));
        QVERIFY(joined.contains("-c:v ffv1 -level 3 -g 1 -slicecrc 1 -slices 16"));
        QVERIFY(joined.contains("-c:a flac"));
        QVERIFY(!joined.contains("bwdif"));
        QVERIFY(cmd.notes.contains("Lossless (FFV1), " + info.pixelFormat));
        Preset mp4 = ffv1;
        mp4.container = "mp4";
        QCOMPARE(presetProblem(mp4), QString("FFV1 goes in MKV (Matroska) only."));
        Preset same = ffv1;
        same.container = "source";    // a .mov source
        QVERIFY(sourceProblem(same, info).contains("FFV1 goes in MKV (Matroska) only, and this file is MOV"));
        MediaInfo jpeg = info;
        jpeg.pixelFormat = "yuvj420p";    // phones' full-range JPEG layout: stored as plain 4:2:0
        QVERIFY(buildCommand(ffv1, jpeg, "/x/in.mov", "/y/out.mkv").arguments.join(' ').contains("format=yuv420p[v]"));
    }

    // Lossless means lossless: the FFV1 file decodes to exactly the source's
    // frames. AV1 runs only where this ffmpeg has SVT-AV1.
    void av1AndFfv1ForReal() {
        if (!hasTool("ffmpeg") || !hasTool("ffprobe"))
            QSKIP("ffmpeg is not installed");
        QTemporaryDir dir;
        QProcess make;
        make.start("ffmpeg", {"-nostdin", "-loglevel", "error", "-f", "lavfi", "-i", "testsrc2=size=320x180:rate=25:duration=2",
                              "-f", "lavfi", "-i", "sine=frequency=440:duration=2", "-c:v", "libx264", "-pix_fmt", "yuv422p10le",
                              "-c:a", "pcm_s24le", "-shortest", dir.filePath("src.mov")});
        QVERIFY(make.waitForFinished(30000));
        QCOMPARE(make.exitCode(), 0);
        auto convert = [&](const QString &presetId) {
            Job job(1, dir.filePath("src.mov"));
            job.setPreset(presetNamed(presetId));
            job.setOutputFolder(dir.filePath("out"));
            job.probe();
            if (!QTest::qWaitFor([&] { return job.state() == "ready"; }, 15000))
                return QString();
            QSignalSpy finished(&job, &Job::finished);
            if (!job.start() || !finished.wait(120000) || !finished.first().first().toBool())
                return QString();
            return job.outputPath();
        };
        auto hash = [](const QString &file, const QString &stream) {
            QProcess p;
            p.start("ffmpeg", {"-nostdin", "-loglevel", "error", "-i", file, "-map", stream, "-f", "md5", "-"});
            p.waitForFinished(30000);
            return QString(p.readAllStandardOutput()).trimmed();
        };
        const QString archive = convert("ffv1");
        QVERIFY(!archive.isEmpty());
        QVERIFY(archive.endsWith(".mkv"));
        QCOMPARE(hash(archive, "0:v:0"), hash(dir.filePath("src.mov"), "0:v:0"));    // every pixel
        QCOMPARE(hash(archive, "0:a:0"), hash(dir.filePath("src.mov"), "0:a:0"));    // every sample

        // A full-range source (phones, screen recordings) stays full range.
        make.start("ffmpeg", {"-nostdin", "-loglevel", "error", "-f", "lavfi", "-i", "testsrc2=size=320x180:rate=25:duration=1",
                              "-c:v", "libx264", "-pix_fmt", "yuvj420p", "-color_range", "pc", dir.filePath("phone.mp4")});
        QVERIFY(make.waitForFinished(30000) && make.exitCode() == 0);
        {
            Job job(2, dir.filePath("phone.mp4"));
            job.setPreset(presetNamed("ffv1"));
            job.setOutputFolder(dir.filePath("out"));
            job.probe();
            QTRY_VERIFY_WITH_TIMEOUT(job.state() == "ready", 15000);
            QVERIFY(job.info().fullRange());
            QSignalSpy finished(&job, &Job::finished);
            QVERIFY(job.start());
            QVERIFY(finished.wait(60000));
            QVERIFY2(finished.first().first().toBool(), qPrintable(job.message()));
            QCOMPARE(hash(job.outputPath(), "0:v:0"), hash(dir.filePath("phone.mp4"), "0:v:0"));
        }

        QProcess list;
        list.start("ffmpeg", {"-hide_banner", "-encoders"});
        list.waitForFinished(10000);
        if (!Hardware::parseEncoderList(list.readAllStandardOutput()).contains("libsvtav1"))
            QSKIP("this ffmpeg has no SVT-AV1");
        const QString small = convert("av1");
        QVERIFY(!small.isEmpty());
        QProcess probe;
        probe.start("ffprobe", {"-v", "error", "-select_streams", "v:0", "-show_entries", "stream=codec_name,pix_fmt",
                                "-of", "csv=p=0", small});
        QVERIFY(probe.waitForFinished(20000));
        QCOMPARE(QString(probe.readAllStandardOutput()).trimmed(), QString("av1,yuv420p10le"));
    }

    void joinsClips() {
        MediaInfo a;
        a.valid = true;
        a.hasVideo = true;
        a.videoCodec = "h264";
        a.width = 1920;
        a.height = 1080;
        a.fps = 25;
        a.fpsRational = "25/1";
        a.pixelFormat = "yuv420p";
        a.duration = 10;
        a.audio = {AudioTrack{0, "aac", 48000, 2, "stereo"}};
        a.path = "/c/a.mp4";
        MediaInfo b = a;
        b.path = "/c/b.mp4";
        QCOMPARE(clipDifference(a, b), QString());
        b.width = 1280;
        b.height = 720;
        QCOMPARE(clipDifference(a, b), QString("1920×1080 and 1280×720"));
        b = a;
        b.fps = 29.97;
        QCOMPARE(clipDifference(a, b), QString("25 and 29.97 fps"));
        b = a;
        b.videoCodec = "hevc";
        QCOMPARE(clipDifference(a, b), QString("H.264 and HEVC video"));
        b = a;
        b.audio.clear();
        QCOMPARE(clipDifference(a, b), QString("1 and 0 audio tracks"));
        QCOMPARE(concatList({"/c/a.mp4", "/c/it's.mp4"}),
                 QString("ffconcat version 1.0\nfile '/c/a.mp4'\nfile '/c/it'\\''s.mp4'\n"));

        // Clips that match: one concat-list input, everything else as usual.
        Preset p = builtinPresets().first();
        p.joinCount = 2;
        p.joinList = "/c/join.txt";
        MediaInfo joined = a;
        joined.duration = 20;
        Command cmd = buildCommand(p, joined, "/c/a.mp4", "/y/out.mp4");
        QString joinedArgs = cmd.arguments.join(' ');
        QVERIFY2(joinedArgs.contains("-f concat -safe 0 -i /c/join.txt"), qPrintable(joinedArgs));
        QVERIFY(!joinedArgs.contains("-i /c/a.mp4"));
        QVERIFY(joinedArgs.contains("[0:v:0]"));
        QVERIFY(cmd.notes.contains("2 clips joined; they match, so they're read as one file"));
        Preset copy = p;
        copy.video = copy.audio = "copy";
        QVERIFY(sourceProblem(copy, joined).isEmpty());    // matching clips join without re-encoding

        // Clips that differ: each an input, fitted to the first, then joined.
        b = a;
        b.width = 1280;
        b.height = 720;
        b.path = "/c/b.mp4";
        MediaInfo c = a;
        c.audio.clear();
        c.duration = 4;
        c.path = "/c/c.mp4";
        p.joinCount = 3;
        p.joinList.clear();
        p.joinClips = {a, b, c};
        p.joinDifference = clipDifference(a, b);
        joined.duration = 24;
        cmd = buildCommand(p, joined, "/c/a.mp4", "/y/out.mp4");
        joinedArgs = cmd.arguments.join(' ');
        QVERIFY2(joinedArgs.contains("-i /c/a.mp4 -i /c/b.mp4 -i /c/c.mp4"), qPrintable(joinedArgs));
        QVERIFY2(joinedArgs.contains("[1:v:0]scale=1920:1080:force_original_aspect_ratio=decrease,pad=1920:1080:(ow-iw)/2:(oh-ih)/2"
                                     ":color=black,setsar=1,fps=25/1,format=yuv420p[jv1]"), qPrintable(joinedArgs));
        QVERIFY2(joinedArgs.contains("anullsrc=r=48000:cl=stereo,atrim=duration=4.000[ja2]"), qPrintable(joinedArgs));
        QVERIFY(joinedArgs.contains("[jv0][ja0][jv1][ja1][jv2][ja2]concat=n=3:v=1:a=1[jv][ja]"));
        QVERIFY(joinedArgs.contains("[jv]"));
        QVERIFY(joinedArgs.contains("[ja]anull"));
        QVERIFY(cmd.notes.contains("3 clips joined: they differ (1920×1080 and 1280×720), so each is fitted to the first"));
        copy = p;
        copy.video = "copy";
        QVERIFY(sourceProblem(copy, joined).contains("These clips differ (1920×1080 and 1280×720)"));

        // Loudness is measured on the joined sound.
        p.loudness = "web";
        const QString measure = loudnessArguments(p, joined, "/c/a.mp4", 0).join(' ');
        QVERIFY2(measure.contains("concat=n=3:v=0:a=1[ja];[ja]loudnorm=I=-16"), qPrintable(measure));
    }

    void joinsClipsForReal() {
        if (!hasTool("ffmpeg") || !hasTool("ffprobe"))
            QSKIP("ffmpeg is not installed");
        QTemporaryDir dir;
        auto make = [&](const QString &name, const QString &size, int rate, bool sound) {
            QStringList a{"-nostdin", "-loglevel", "error", "-f", "lavfi", "-i",
                          QString("testsrc2=size=%1:rate=%2:duration=2").arg(size).arg(rate)};
            if (sound)
                a << "-f" << "lavfi" << "-i" << "sine=frequency=440:duration=2" << "-c:a" << "aac";
            a << "-c:v" << "libx264" << "-pix_fmt" << "yuv420p" << "-shortest" << dir.filePath(name);
            QProcess p;
            p.start("ffmpeg", a);
            return p.waitForFinished(30000) && p.exitCode() == 0;
        };
        QVERIFY(make("c1.mp4", "320x180", 25, true));
        QVERIFY(make("c2.mp4", "320x180", 25, true));
        QVERIFY(make("odd.mp4", "240x240", 30, false));
        auto probe = [](const QString &file, const QString &entries) {
            QProcess p;
            p.start("ffprobe", {"-v", "error", "-show_entries", entries, "-of", "csv=p=0", file});
            p.waitForFinished(20000);
            return QString(p.readAllStandardOutput()).trimmed();
        };

        QTemporaryDir config;
        Backend backend(nullptr, config.path());
        backend.addPaths({dir.filePath("c1.mp4"), dir.filePath("c2.mp4")});
        QTRY_COMPARE_WITH_TIMEOUT(backend.joinableCount(), 2, 15000);
        backend.joinWaiting();
        QCOMPARE(backend.jobs().size(), 1);
        auto *job = backend.jobAt(0);
        QCOMPARE(job->clipCount(), 2);
        QCOMPARE(backend.joinableCount(), 0);    // nothing left to join it with
        QCOMPARE(job->totalSize(), QFileInfo(dir.filePath("c1.mp4")).size() + QFileInfo(dir.filePath("c2.mp4")).size());
        QVERIFY(job->joinDifference().isEmpty());
        QCOMPARE(job->suggestion().value("presetId").toString(), QString("copy"));
        QVERIFY(qAbs(job->length() - 4.0) < 0.2);

        // Matching clips copied end to end: no re-encoding.
        backend.setJobPreset(0, "copy");
        job->setOutputFolder(dir.filePath("out"));
        QSignalSpy copied(job, &Job::finished);
        QVERIFY(job->start());
        QVERIFY(copied.wait(60000));
        QVERIFY2(copied.first().first().toBool(), qPrintable(job->message()));
        const double copiedLength = probe(job->outputPath(), "format=duration").toDouble();
        QVERIFY2(qAbs(copiedLength - 4.0) < 0.2, qPrintable(QString::number(copiedLength)));

        // Split puts them back as they were.
        backend.splitJob(0);
        QCOMPARE(backend.jobs().size(), 2);
        QCOMPARE(backend.jobAt(1)->inputPath(), dir.filePath("c2.mp4"));
        QCOMPARE(backend.jobAt(0)->clipCount(), 1);

        // A clip that differs (square, 30 fps, silent): re-encoded to fit the first.
        backend.addPaths({dir.filePath("odd.mp4")});
        QTRY_VERIFY_WITH_TIMEOUT(backend.jobAt(2) && backend.jobAt(2)->isWaiting(), 15000);
        backend.removeJob(0);    // the finished one
        QCOMPARE(backend.joinableCount(), 2);
        backend.joinWaiting();
        job = backend.jobAt(0);
        QCOMPARE(job->clipCount(), 2);
        QCOMPARE(job->joinDifference(), QString("320×180 and 240×240"));
        backend.setJobPreset(0, "review");
        Preset p = job->runPreset();
        p.speed = "ultrafast";
        p.loudness = "web";    // measured on the joined sound, silence included
        job->setPreset(p);
        job->setOutputFolder(dir.filePath("out"));
        QSignalSpy converted(job, &Job::finished);
        QVERIFY(job->start());
        QVERIFY(converted.wait(90000));
        QVERIFY2(converted.first().first().toBool(), qPrintable(job->message()));
        const QString out = job->outputPath();
        QCOMPARE(probe(out, "stream=codec_type,width,height").split('\n').first(), QString("video,320,180"));
        QVERIFY(probe(out, "stream=codec_type").contains("audio"));
        const double length = probe(out, "format=duration").toDouble();
        QVERIFY2(qAbs(length - 4.0) < 0.2, qPrintable(QString::number(length)));
    }

    void padsForReal() {
        if (!hasTool("ffmpeg") || !hasTool("ffprobe"))
            QSKIP("ffmpeg is not installed");
        QTemporaryDir dir;
        QProcess make;
        make.start("ffmpeg", {"-nostdin", "-loglevel", "error", "-f", "lavfi", "-i", "color=white:size=320x180:rate=25:duration=2",
                              "-c:v", "libx264", "-pix_fmt", "yuv420p", dir.filePath("white.mp4")});
        QVERIFY(make.waitForFinished(30000));
        QCOMPARE(make.exitCode(), 0);

        Job job(1, dir.filePath("white.mp4"));
        Preset p = builtinPresets().first();
        p.speed = "ultrafast";
        p.burnTimecode = false;
        job.setPreset(p);
        job.setOutputFolder(dir.filePath("out"));
        job.probe();
        QTRY_VERIFY_WITH_TIMEOUT(job.state() == "ready", 15000);
        job.setPadMode("9:16");
        QSignalSpy finished(&job, &Job::finished);
        QVERIFY(job.start());
        QVERIFY(finished.wait(60000));
        QVERIFY2(finished.first().first().toBool(), qPrintable(job.message()));

        const QString out = dir.filePath("out/white_review.mp4");
        QProcess probe;
        probe.start("ffprobe", {"-v", "error", "-select_streams", "v:0", "-show_entries", "stream=width,height,sample_aspect_ratio",
                                "-of", "csv=p=0", out});
        QVERIFY(probe.waitForFinished(20000));
        QCOMPARE(QString(probe.readAllStandardOutput()).trimmed(), QString("180,320,1:1"));
        auto brightness = [&](int x, int y) {
            QProcess f;
            f.start("ffmpeg", {"-nostdin", "-loglevel", "error", "-ss", "1", "-i", out, "-frames:v", "1",
                               "-vf", QString("crop=1:1:%1:%2,format=gray").arg(x).arg(y), "-f", "rawvideo", "-"});
            f.waitForFinished(20000);
            const QByteArray b = f.readAllStandardOutput();
            return b.isEmpty() ? -1 : int(uchar(b[0]));
        };
        QVERIFY2(brightness(90, 10) < 30, "a black bar above");
        QVERIFY2(brightness(90, 310) < 30, "and below");
        QVERIFY2(brightness(90, 160) > 220, "the white picture in the middle");
    }

    void detectsBarsForReal() {
        if (!hasTool("ffmpeg") || !hasTool("ffprobe"))
            QSKIP("ffmpeg is not installed");
        QTemporaryDir dir;
        const QString source = dir.filePath("scope.mp4");
        QProcess make;    // a 1920×800 picture letterboxed into 1080
        make.start("ffmpeg", {"-nostdin", "-loglevel", "error", "-f", "lavfi", "-i", "testsrc2=size=1920x800:rate=25:duration=6",
                              "-vf", "pad=1920:1080:0:140:black", "-c:v", "libx264", "-pix_fmt", "yuv420p", source});
        QVERIFY(make.waitForFinished(60000));
        QCOMPARE(make.exitCode(), 0);
        Job job(1, source);
        job.probe();
        QTRY_VERIFY_WITH_TIMEOUT(job.state() == "ready", 15000);
        job.setCropMode("bars");
        QVERIFY(job.detectingBars());
        QTRY_VERIFY_WITH_TIMEOUT(job.barsChecked(), 30000);
        QCOMPARE(job.cropTop(), 140);
        QCOMPARE(job.cropBottom(), 140);
        QCOMPARE(job.cropLeft(), 0);
        QVERIFY(job.notes().contains("Cropped to 1920×800"));
        job.setCropMode("off");
        QVERIFY(!job.runPreset().crops());
    }

    void outputStillCommands() {
        const MediaInfo info = parseProbe(kKiPro, "/x/in.mov");
        Preset p = builtinPresets()[1];    // review with burned-in timecode
        p.cropTop = 40;
        p.cropBottom = 40;
        p.lut = "/c/warm.cube";
        const QStringList a = outputStillArguments(p, info, "/x/in.mov", 10.0, "/c/still.jpg", 720);
        const QString joined = a.join(' ');
        // The conversion's own chain, from the frame, with the timecode read
        // on the source's clock there (as a run trimmed to 10 s would).
        QVERIFY2(joined.contains("-ss 10.000 -i /x/in.mov"), qPrintable(joined));
        QVERIFY(joined.contains("[0:v:0]lut3d=file=/c/warm.cube:interp=tetrahedral,scale=out_range=tv,format=yuv422p10le,crop=1280:640:0:40"));
        QVERIFY(joined.contains("timecode='01\\:02\\:25\\:12'"));
        QVERIFY(joined.contains("scale=-2:'min(ih,720)'"));
        QVERIFY(joined.contains("prores_metadata"));    // the colour fix, as in the encode
        QVERIFY(joined.endsWith("-frames:v 1 -q:v 3 -update 1 /c/still.jpg"));
        QVERIFY(!joined.contains("libx264"));

        Preset copy = builtinPresets()[1];
        copy.video = "copy";
        QVERIFY(outputStillArguments(copy, info, "/x/in.mov", 10.0, "/c/still.jpg").isEmpty());
        // One list: begin and end from two calls would be two lists, and the
        // search would run off the end of the first (the intermittent crash).
        const QList<Preset> all = builtinPresets();
        const auto wav = std::find_if(all.cbegin(), all.cend(), [](const Preset &x) { return x.audioOnly(); });
        QVERIFY(wav != all.cend());
        QVERIFY(outputStillArguments(*wav, info, "/x/in.mov", 10.0, "/c/still.jpg").isEmpty());
    }

    void outputStillForReal() {
        if (!hasTool("ffmpeg") || !hasTool("ffprobe"))
            QSKIP("ffmpeg is not installed");
        QTemporaryDir dir;
        const QString source = dir.filePath("bars.mp4");
        QProcess make;
        make.start("ffmpeg", {"-nostdin", "-loglevel", "error", "-f", "lavfi", "-i", "testsrc2=size=1920x800:rate=25:duration=4",
                              "-vf", "pad=1920:1080:0:140:black", "-c:v", "libx264", "-pix_fmt", "yuv420p", source});
        QVERIFY(make.waitForFinished(60000));
        QCOMPARE(make.exitCode(), 0);
        Job job(1, source);
        job.setPreset(builtinPresets()[2]);    // share: up to 1080p
        job.probe();
        QTRY_VERIFY_WITH_TIMEOUT(job.state() == "ready", 15000);
        QVERIFY(job.canShowOutput());
        job.setCropMargins(0, 140, 0, 140);
        job.showOutputAt(2.0);
        QTRY_VERIFY_WITH_TIMEOUT(job.outputStillStamp() == 1 && !job.outputStillBusy(), 20000);
        QCOMPARE(job.outputStillTime(), 2.0);
        QVERIFY(job.outputStillError().isEmpty());
        QCOMPARE(QImage(job.outputStill()).size(), QSize(1920, 800));    // the bars are gone, as in the output

        // A change to the processing redraws the same frame.
        job.setCropMargins(0, 0, 0, 0);
        QTRY_VERIFY_WITH_TIMEOUT(job.outputStillStamp() == 2 && !job.outputStillBusy(), 20000);
        QCOMPARE(QImage(job.outputStill()).size(), QSize(1920, 1080));

        // A setting changed while the next frame is still being made (an
        // out point set at the playhead) ends on that frame, not the last
        // one shown.
        // (Whether the change lands while ffmpeg is still busy depends on
        // the machine's speed; either way the result must be the new frame,
        // processed the new way.)
        job.showOutputAt(3.0);
        QTest::qWait(250);    // past the 120 ms gathering: ffmpeg is making it
        job.setCropMargins(0, 140, 0, 140);
        QTRY_VERIFY_WITH_TIMEOUT(!job.outputStillBusy() && job.outputStillTime() == 3.0
                                     && QImage(job.outputStill()).size() == QSize(1920, 800), 20000);
    }

    void subtitleBurnCommands() {
        const MediaInfo info = parseProbe(kKiPro, "/x/in.mov");
        Preset p = builtinPresets()[1];    // review with burned-in timecode
        p.burnSubtitles = "/c/sub-1.srt";
        p.burnSubtitleName = "film.srt";
        Command cmd = buildCommand(p, info, "/x/in.mov", "/y/out.mp4");
        QString joined = cmd.arguments.join(' ');
        QVERIFY2(joined.contains("subtitles=filename=/c/sub-1.srt,drawtext"), qPrintable(joined));
        QVERIFY(joined.contains("x=(w-tw)/2:y=h/24"));    // timecode up top, out of the way
        QVERIFY(cmd.notes.contains("Subtitles burned in from film.srt"));

        // A trimmed run moves the picture onto the subtitles' clock and back.
        p.burnSubtitles = "/c/src.mkv";
        p.burnSubtitleTrack = 1;
        joined = buildCommand(p, info, "/x/in.mov", "/y/out.mp4", 12.5, 10).arguments.join(' ');
        QVERIFY2(joined.contains("setpts=PTS+12.500/TB,subtitles=filename=/c/src.mkv:si=1,setpts=PTS-STARTPTS"), qPrintable(joined));

        Preset copy = p;
        copy.video = "copy";
        QVERIFY(sourceProblem(copy, info).contains("Burning in subtitles"));
    }

    void subtitleChoicesFindFilesBeside() {
        QTemporaryDir dir;
        const QString source = dir.filePath("film.mkv");
        writeFile(source, "x");
        writeFile(dir.filePath("film.srt"), "1\n");
        writeFile(dir.filePath("film.en.ass"), "x");
        writeFile(dir.filePath("other.srt"), "1\n");
        Job job(1, source);
        QStringList texts;
        for (const QVariant &v : job.subtitleChoices())
            texts << v.toMap().value("text").toString();
        QCOMPARE(texts, (QStringList{"None", "film.en.ass", "film.srt", "Choose a file…"}));
        job.setSubtitleBurn(dir.filePath("film.srt"));
        QCOMPARE(job.subtitleBurn(), dir.filePath("film.srt"));
        QVERIFY(job.runPreset().burnSubtitles.endsWith("-subtitles.srt"));
        job.setSubtitleBurn(dir.filePath("nope.txt"));    // not a subtitle file: ignored
        QCOMPARE(job.subtitleBurn(), dir.filePath("film.srt"));
    }

    void burnsSubtitlesForReal() {
        if (!hasTool("ffmpeg") || !hasTool("ffprobe"))
            QSKIP("ffmpeg is not installed");
        QTemporaryDir dir;
        const QString source = dir.filePath("talk: one, two.mp4");    // a name filters choke on
        QProcess make;
        make.start("ffmpeg", {"-nostdin", "-loglevel", "error", "-f", "lavfi", "-i", "color=gray:size=320x180:rate=25:duration=10",
                              "-c:v", "libx264", "-pix_fmt", "yuv420p", source});
        QVERIFY(make.waitForFinished(60000));
        QCOMPARE(make.exitCode(), 0);
        writeFile(dir.filePath("talk: one, two.srt"), "1\n00:00:06,000 --> 00:00:08,000\nHELLO THERE\n");

        Job job(1, source);
        Preset p = builtinPresets().first();
        p.speed = "ultrafast";
        job.setPreset(p);
        job.setOutputFolder(dir.filePath("out"));
        job.probe();
        QTRY_VERIFY_WITH_TIMEOUT(job.state() == "ready", 15000);
        job.setSubtitleBurn(dir.filePath("talk: one, two.srt"));
        job.setTrim(5, 9);    // the line lands 1 s to 3 s into the copy
        QSignalSpy finished(&job, &Job::finished);
        QVERIFY(job.start());
        QVERIFY(finished.wait(60000));
        QVERIFY2(finished.first().first().toBool(), qPrintable(job.message()));

        auto frameMd5 = [&](const QString &at) {
            QProcess f;
            f.start("ffmpeg", {"-nostdin", "-loglevel", "error", "-ss", at, "-i", dir.filePath("out/talk: one, two_review.mp4"),
                               "-frames:v", "1", "-f", "md5", "-"});
            f.waitForFinished(20000);
            return f.readAllStandardOutput().trimmed();
        };
        const QByteArray plain = frameMd5("0.5"), withLine = frameMd5("2.0");
        QVERIFY(!plain.isEmpty());
        QVERIFY2(plain != withLine, "the subtitle line should change the frame at 2 s");
    }

    void lutAndLogoCommands() {
        QTemporaryDir dir;
        writeFile(dir.filePath("slog3.cube"), "LUT_3D_SIZE 2\n");
        writeFile(dir.filePath("brand.png"), "x");
        const MediaInfo info = parseProbe(kKiPro, "/x/in.mov");    // 1280×720
        Preset p = builtinPresets()[2];                            // share copy, up to 1080p
        p.lut = dir.filePath("slog3.cube");
        p.lutFile = "/c/j-lut.cube";
        p.logo = dir.filePath("brand.png");
        p.logoFile = "/c/j-logo.png";
        p.logoPosition = "bl";
        p.logoSize = 10;
        p.logoOpacity = 60;
        p.cropTop = p.cropBottom = 40;                             // kept picture: 640 high
        const Command cmd = buildCommand(p, info, "/x/in.mov", "/y/out.mp4");
        const QString joined = cmd.arguments.join(' ');
        // The LUT comes before the crop; the logo sizes against what's kept.
        QVERIFY2(joined.indexOf("lut3d=file=/c/j-lut.cube:interp=tetrahedral") < joined.indexOf("crop=1280:640:0:40"), qPrintable(joined));
        QVERIFY(joined.contains("null[lv];movie=filename=/c/j-logo.png,scale=-1:64,format=rgba,colorchannelmixer=aa=0.60[lg];"
                                "[lv][lg]overlay=x=26:y=H-h-26:format=yuv422p10"));
        QVERIFY(joined.contains("-color_primaries bt709 -color_trc bt709 -colorspace bt709"));
        QVERIFY(cmd.notes.contains("LUT applied: slog3.cube"));
        QVERIFY(cmd.notes.contains("Logo brand.png at 10% of the height"));

        const Preset back = Preset::fromVariant(p.toVariant());    // saved with the preset
        QCOMPARE(back.lut, p.lut);
        QCOMPARE(back.logoPosition, QString("bl"));
        QCOMPARE(back.logoOpacity, 60);
        QVERIFY(back.lutFile.isEmpty());                           // links are per run

        Preset copy = p;
        copy.video = "copy";
        copy.cropTop = copy.cropBottom = 0;
        QVERIFY(!presetProblem(copy).isEmpty());
        Preset gone = p;
        gone.lut = dir.filePath("missing.cube");
        QVERIFY(sourceProblem(gone, info).contains("isn't there any more"));
    }

    void watermarkMakesRoomForTheLogo() {
        const MediaInfo info = parseProbe(kKiPro, "/x/in.mov");
        Preset p = presetNamed("review");
        p.watermark = "DRAFT";
        QVERIFY(buildCommand(p, info, "/x/in.mov", "/y/out.mp4").arguments.join(' ').contains(":x=w-tw-h/24:y=h/24"));
        p.logo = "/c/logo.png";    // top right by default: the watermark moves to the top left
        QVERIFY(buildCommand(p, info, "/x/in.mov", "/y/out.mp4").arguments.join(' ').contains("text='DRAFT':fontsize=h/12:fontcolor=white@0.35:x=h/24:y=h/24"));
        p.logoPosition = "bl";
        QVERIFY(buildCommand(p, info, "/x/in.mov", "/y/out.mp4").arguments.join(' ').contains(":x=w-tw-h/24:y=h/24"));
    }

    void lutAndLogoForReal() {
        if (!hasTool("ffmpeg") || !hasTool("ffprobe"))
            QSKIP("ffmpeg is not installed");
        QTemporaryDir dir;
        // Inverts every colour: impossible to miss.
        QByteArray cube = "LUT_3D_SIZE 2\n";
        for (int b : {0, 1})
            for (int g : {0, 1})
                for (int r : {0, 1})
                    cube += QByteArray::number(1 - r) + " " + QByteArray::number(1 - g) + " " + QByteArray::number(1 - b) + "\n";
        writeFile(dir.filePath("invert, v2.cube"), cube);
        QProcess make;
        make.start("ffmpeg", {"-nostdin", "-loglevel", "error", "-f", "lavfi", "-i", "color=red:size=64x64", "-frames:v", "1",
                              dir.filePath("logo (final).png")});
        QVERIFY(make.waitForFinished(30000));
        make.start("ffmpeg", {"-nostdin", "-loglevel", "error", "-f", "lavfi", "-i", "color=white:size=320x180:rate=25:duration=2",
                              "-c:v", "libx264", "-pix_fmt", "yuv420p", dir.filePath("white.mp4")});
        QVERIFY(make.waitForFinished(30000));
        QCOMPARE(make.exitCode(), 0);

        Job job(1, dir.filePath("white.mp4"));
        Preset p = builtinPresets().first();
        p.speed = "ultrafast";
        p.lut = dir.filePath("invert, v2.cube");
        p.logo = dir.filePath("logo (final).png");
        p.logoPosition = "tl";
        p.logoSize = 20;
        p.logoOpacity = 100;
        job.setPreset(p);
        job.setOutputFolder(dir.filePath("out"));
        job.probe();
        QTRY_VERIFY_WITH_TIMEOUT(job.state() == "ready", 15000);
        QSignalSpy finished(&job, &Job::finished);
        QVERIFY(job.start());
        QVERIFY(finished.wait(60000));
        QVERIFY2(finished.first().first().toBool(), qPrintable(job.message()));

        // One pixel from the logo's corner and one from the middle, as RGB.
        auto pixel = [&](int x, int y) {
            QProcess f;
            f.start("ffmpeg", {"-nostdin", "-loglevel", "error", "-ss", "1", "-i", dir.filePath("out/white_review.mp4"),
                               "-frames:v", "1", "-vf", QString("crop=1:1:%1:%2,format=rgb24").arg(x).arg(y),
                               "-f", "rawvideo", "-"});
            f.waitForFinished(20000);
            return f.readAllStandardOutput();
        };
        // An RGB logo leaves the picture's range alone: still limited.
        QProcess range;
        range.start("ffprobe", {"-v", "error", "-select_streams", "v:0", "-show_entries", "stream=color_range,pix_fmt",
                                "-of", "csv=p=0", dir.filePath("out/white_review.mp4")});
        QVERIFY(range.waitForFinished(20000));
        QCOMPARE(QString(range.readAllStandardOutput()).trimmed(), QString("yuv420p,tv"));
        const QByteArray logo = pixel(14, 14), middle = pixel(160, 120);
        QCOMPARE(logo.size(), 3);
        QVERIFY2(uchar(logo[0]) > 180 && uchar(logo[1]) < 80, "the red logo, top left");
        QVERIFY2(uchar(middle[0]) < 60 && uchar(middle[1]) < 60 && uchar(middle[2]) < 60, "white inverted to black by the LUT");
    }

    void waveformPeaks() {
        // 4 slices over 8 samples; the pieces split a sample in half.
        WaveformScan scan(8, 4);
        QByteArray pcm;
        for (qint16 v : {qint16(100), qint16(-400), qint16(0), qint16(0), qint16(-32768), qint16(25), qint16(1600), qint16(-1600)})
            pcm.append(char(quint16(v) & 0xff)).append(char(quint16(v) >> 8));
        scan.add(pcm.left(3));
        scan.add(pcm.mid(3));
        const QVariantList peaks = scan.peaks();
        QCOMPARE(peaks.size(), 4);
        QCOMPARE(peaks[1].toDouble(), 0.0);                                  // silence
        QCOMPARE(peaks[2].toDouble(), 1.0);                                  // the loudest, -32768 included
        QVERIFY(qAbs(peaks[0].toDouble() - std::sqrt(400.0 / 32767)) < 1e-9);    // the slice's loudest, either sign
        QVERIFY(qAbs(peaks[3].toDouble() - std::sqrt(1600.0 / 32767)) < 1e-9);
        // Samples past the stated length go to the last slice.
        scan.add(pcm.mid(8, 2));    // -32768 again
        QCOMPARE(scan.peaks()[3].toDouble(), 1.0);
        QVERIFY(WaveformScan(10, 3).peaks() == QVariantList({0.0, 0.0, 0.0}));
        QVERIFY(waveformArguments("/x/in.mov").join(' ').endsWith("-map 0:a:0 -vn -sn -dn -ac 1 -ar 4000 -f s16le -"));
    }

    void fitsAFileSize() {
        MediaInfo info = parseProbe(kKiPro, "/x/in.mov");
        info.duration = 100;
        Preset p = presetNamed("share");    // H.264, AAC 192
        p.targetSizeMB = 25;
        p.hardware = "qsv";    // a size target stays on the CPU, whatever the setting
        QVERIFY(p.fitsSize() && p.twoPass());
        const int audio = audioStreamCount(p, info) * 192;
        const int kbps = int(std::floor(25 * 8e6 * 0.98 / 100 / 1000 - audio));
        QCOMPARE(fitVideoKbps(p, info, 100), kbps);
        QVERIFY(fitVideoKbps(p, info, 50) > 2 * kbps);    // trimmed to half: twice the bits a second

        p.passLog = "/c/log";
        p.pass = 1;    // reads the picture only: no audio, no subtitles, nothing written
        Command cmd = buildCommand(p, info, "/x/in.mov", "/y/out.mp4");
        QString joined = cmd.arguments.join(' ');
        QVERIFY2(joined.contains(QString("-c:v libx264 -preset medium -b:v %1k -pass 1 -passlogfile /c/log -profile:v high").arg(kbps)),
                 qPrintable(joined));
        QVERIFY2(joined.contains("-an") && !joined.contains("-c:a") && joined.endsWith("-f null -"), qPrintable(joined));
        QVERIFY(!joined.contains("-maxrate") && !joined.contains("-crf"));
        p.pass = 2;
        cmd = buildCommand(p, info, "/x/in.mov", "/y/out.mp4");
        joined = cmd.arguments.join(' ');
        QVERIFY2(joined.contains(QString("-b:v %1k -pass 2 -passlogfile /c/log").arg(kbps)), qPrintable(joined));
        QVERIFY(joined.contains("-c:a aac -b:a 192k") && joined.endsWith("/y/out.mp4"));
        QVERIFY2(cmd.notes.contains(QString("Fitted to 25 MB: %1 kbps for the picture, in two passes").arg(kbps)),
                 qPrintable(cmd.notes.join(" | ")));

        // A size is a ceiling: a source that's smaller keeps its own rate.
        MediaInfo small = info;
        small.videoBitrate = 1000000;
        QCOMPARE(fitVideoKbps(p, small, 100), 1000);
        cmd = buildCommand(p, small, "/x/in.mov", "/y/out.mp4");
        QVERIFY(cmd.arguments.join(' ').contains("-b:v 1000k -pass 2"));
        QVERIFY2(cmd.notes.contains("Under 25 MB already at the source's own bitrate: 1000 kbps for the picture, in two passes"),
                 qPrintable(cmd.notes.join(" | ")));
        small.videoBitrate = 0;    // MKV: the file's rate, less its audio
        small.bitrate = 3000000;
        QCOMPARE(sourceVideoKbps(small), 3000 - int(std::lround(info.audio[0].bitrate / 1000.0 + info.audio[1].bitrate / 1000.0)));

        // HEVC passes through x265's own parameters; AV1 has one pass.
        p.video = "hevc";
        p.pass = 1;
        joined = buildCommand(p, info, "/x/in.mov", "/y/out.mp4").arguments.join(' ');
        QVERIFY2(joined.contains(QString("-c:v libx265 -preset medium -b:v %1k").arg(kbps)) && joined.contains(":pass=1:stats=/c/log.x265"),
                 qPrintable(joined));
        p.video = "av1";
        p.pass = 0;
        QVERIFY(!p.twoPass());
        joined = buildCommand(p, info, "/x/in.mov", "/y/out.mp4").arguments.join(' ');
        QVERIFY2(joined.contains(QString("-c:v libsvtav1 -preset 7 -b:v %1k").arg(kbps)) && !joined.contains("-crf"), qPrintable(joined));

        // Too small a size for the length says what would do.
        p.video = "h264";
        p.targetSizeMB = 1;
        const QString tooSmall = fitProblem(p, info, 600);
        QVERIFY2(tooSmall.startsWith("1 MB is too small for 10:00 of video: the audio alone takes it. Choose "), qPrintable(tooSmall));
        QVERIFY(tooSmall.contains("lower the audio bitrate"));
        QVERIFY(fitProblem(p, info, 2).isEmpty());
        // Lossless audio has no known size.
        p.container = "mov";
        p.audio = "pcm24";
        QVERIFY(presetProblem(p).contains("To fit a size"));
        p.targetSizeMB = 0;
        QVERIFY(presetProblem(p).isEmpty());

        p.targetSizeMB = 8;
        QCOMPARE(Preset::fromVariant(p.toVariant()).targetSizeMB, 8);
    }

    void fitsAFileSizeForReal() {
        if (!hasTool("ffmpeg") || !hasTool("ffprobe"))
            QSKIP("ffmpeg is not installed");
        QTemporaryDir dir;
        const QString source = dir.filePath("noisy.mov");
        QProcess make;    // noise, so the picture would take far more than the size allows
        make.start("ffmpeg", {"-nostdin", "-loglevel", "error", "-f", "lavfi", "-i", "testsrc2=size=640x360:rate=30:duration=8",
                              "-f", "lavfi", "-i", "sine=frequency=440:duration=8", "-vf", "noise=alls=30:allf=t",
                              "-c:v", "libx264", "-crf", "10", "-preset", "ultrafast", "-c:a", "pcm_s16le", source});
        QVERIFY(make.waitForFinished(60000));
        QCOMPARE(make.exitCode(), 0);

        Job job(1, source);
        Preset p = presetNamed("share");
        p.speed = "ultrafast";
        p.audioKbps = 96;
        p.targetSizeMB = 1;
        job.setPreset(p);
        job.setOutputFolder(dir.filePath("out"));
        job.probe();
        QTRY_VERIFY_WITH_TIMEOUT(job.state() == "ready", 15000);
        QVERIFY2(job.commandLine().contains("-pass 1") && job.commandLine().contains(" && ffmpeg "), qPrintable(job.commandLine()));

        QSignalSpy finished(&job, &Job::finished);
        QVERIFY(job.start());
        QTRY_VERIFY_WITH_TIMEOUT(job.message().startsWith("Pass 1 of 2"), 10000);
        QVERIFY(finished.wait(120000));
        QVERIFY2(finished.first().first().toBool(), qPrintable(job.message()));
        const QFileInfo out(dir.filePath("out/noisy_share.mp4"));
        QVERIFY(out.exists());
        qInfo() << "1 MB asked, got" << out.size();
        // At most the size asked for, and not far under it.
        QVERIFY2(out.size() <= 1000000 && out.size() > 900000, qPrintable(QString::number(out.size())));
        // The passes' notes are gone.
        const QFileInfo log(QString(job.previewOut()).replace("-out.jpg", "-pass"));
        QVERIFY(log.dir().entryList({log.fileName() + "*"}, QDir::Files).isEmpty());
    }

    void contactSheetForReal() {
        if (!hasTool("ffmpeg") || !hasTool("ffprobe"))
            QSKIP("ffmpeg is not installed");
        QTemporaryDir dir;
        const QString source = dir.filePath("it's: 100% \"odd\".mov");    // a name drawtext would choke on
        QProcess make;
        make.start("ffmpeg", {"-nostdin", "-loglevel", "error", "-f", "lavfi", "-i", "testsrc2=size=640x360:rate=25:duration=8",
                              "-c:v", "libx264", "-preset", "ultrafast", "-timecode", "10:00:00:00", source});
        QVERIFY(make.waitForFinished(60000));
        QCOMPARE(make.exitCode(), 0);
        Job job(1, source);
        job.setOutputFolder(dir.filePath("out"));
        job.probe();
        QTRY_VERIFY_WITH_TIMEOUT(job.state() == "ready", 15000);
        const QStringList args = contactSheetArguments(job.info(), source, "/c/header.txt", "/c/sheet.jpg");
        QCOMPARE(args.count("-ss"), 20);
        QVERIFY2(args.join(' ').contains("text='10\\:00\\:00\\:05'"), qPrintable(args.join(' ')));    // 0.2 s in: frame 5

        QSignalSpy changed(&job, &Job::sheetChanged);
        job.makeContactSheet();
        QCOMPARE(job.sheetState(), QString("making"));
        QTRY_VERIFY_WITH_TIMEOUT(job.sheetState() != "making", 60000);
        const QString sheet = job.sheetState();
        QCOMPARE(sheet, dir.filePath("out/it's: 100% \"odd\"_contact_sheet.jpg"));
        const QImage image(sheet);
        QCOMPARE(image.width(), 5 * (384 + 4));
        QCOMPARE(image.height(), 4 * (216 + 4) + 64);
        QVERIFY(!QFileInfo::exists(QString(job.previewOut()).replace("-out.jpg", "-sheet.txt")));
        // Upright video: more, narrower columns.
        MediaInfo upright = job.info();
        upright.width = 1080;
        upright.height = 1920;
        QCOMPARE(contactSheetLayout(upright).columns, 8);
        QCOMPARE(contactSheetLayout(upright).tileWidth, 240);
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
        QTRY_COMPARE_WITH_TIMEOUT(job.waveform().size(), 600, 15000);
        QVERIFY(job.waveform().first().toDouble() > 0.5);    // a steady tone: every slice near the loudest
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
        QVERIFY2(job.percent() < 99, qPrintable(QString::number(job.percent())));    // where it stopped, not 100%
        QVERIFY(!QFileInfo::exists(dir.filePath("long_review.mp4")));
        QVERIFY(!QFileInfo::exists(Job::partialPathFor(dir.filePath("long_review.mp4"))));
    }
};

QTEST_MAIN(EncodrTests)
#include "encodr_tests.moc"
