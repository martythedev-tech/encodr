#include "preset.h"

#include <QHash>
#include <QRegularExpression>
#include <cmath>

QVariantMap Preset::toVariant() const {
    return {
        {"id", id}, {"name", name}, {"description", description}, {"suffix", suffix}, {"builtin", builtin},
        {"container", container}, {"video", video}, {"crf", crf}, {"maxrateKbps", maxrateKbps},
        {"speed", speed}, {"maxHeight", maxHeight}, {"audio", audio}, {"audioKbps", audioKbps},
        {"deinterlace", deinterlace}, {"burnTimecode", burnTimecode}, {"watermark", watermark},
        {"hardware", hardware},
        {"audioOnly", audioOnly()},
    };
}

Preset Preset::fromVariant(const QVariantMap &m) {
    Preset p;
    p.id = m.value("id").toString();
    p.name = m.value("name").toString();
    p.description = m.value("description").toString();
    p.suffix = m.value("suffix").toString();
    p.builtin = m.value("builtin", false).toBool();
    p.container = m.value("container", p.container).toString();
    p.video = m.value("video", p.video).toString();
    p.crf = m.value("crf", p.crf).toInt();
    p.maxrateKbps = m.value("maxrateKbps", p.maxrateKbps).toInt();
    p.speed = m.value("speed", p.speed).toString();
    p.maxHeight = m.value("maxHeight", p.maxHeight).toInt();
    p.audio = m.value("audio", p.audio).toString();
    p.audioKbps = m.value("audioKbps", p.audioKbps).toInt();
    p.deinterlace = m.value("deinterlace", p.deinterlace).toBool();
    p.burnTimecode = m.value("burnTimecode", p.burnTimecode).toBool();
    p.watermark = m.value("watermark").toString();
    p.hardware = m.value("hardware", p.hardware).toString();
    return p;
}

QList<Preset> builtinPresets() {
    QList<Preset> list;
    auto add = [&list](Preset p) { list.append(p); };

    Preset review;
    review.id = "review";
    review.suffix = "_review";
    review.name = "Review copy (H.264 MP4)";
    review.description = "Plays anywhere and looks like the source. AAC audio at 320 kbps.";
    add(review);

    Preset reviewTc = review;
    reviewTc.id = "review-timecode";
    reviewTc.suffix = "_review_tc";
    reviewTc.name = "Review copy with timecode";
    reviewTc.description = "The review copy with the source timecode burned into the picture, so reviewers can quote exact moments.";
    reviewTc.burnTimecode = true;
    add(reviewTc);

    Preset share;
    share.id = "share";
    share.suffix = "_share";
    share.name = "Small share copy (H.264, up to 1080p)";
    share.description = "For email, chat and the web: smaller files, still clean. AAC 192 kbps.";
    share.crf = 23;
    share.maxrateKbps = 6000;
    share.maxHeight = 1080;
    share.speed = "medium";
    share.audioKbps = 192;
    add(share);

    Preset hevc;
    hevc.id = "hevc";
    hevc.suffix = "_hevc";
    hevc.name = "High efficiency (HEVC MP4)";
    hevc.description = "About half the size of H.264 at the same quality. Plays on Macs, iPhones and modern TVs.";
    hevc.video = "hevc";
    hevc.crf = 22;
    hevc.maxrateKbps = 0;
    hevc.speed = "medium";
    hevc.audioKbps = 256;
    add(hevc);

    Preset proxy;
    proxy.id = "prores-proxy";
    proxy.suffix = "_proxy";
    proxy.name = "Edit proxy (ProRes Proxy)";
    proxy.description = "Light editing proxy for Premiere, Final Cut and Resolve. Uncompressed 24-bit audio.";
    proxy.container = "mov";
    proxy.video = "prores_proxy";
    proxy.maxrateKbps = 0;
    proxy.audio = "pcm24";
    add(proxy);

    Preset master = proxy;
    master.id = "prores-422";
    master.suffix = "_prores";
    master.name = "Edit master (ProRes 422)";
    master.description = "Full-quality intermediate for finishing. Large files. Uncompressed 24-bit audio.";
    master.video = "prores_422";
    add(master);

    Preset wav;
    wav.id = "wav";
    wav.suffix = "";
    wav.name = "Lossless audio (WAV, 24-bit)";
    wav.description = "The audio exactly as recorded. Split mono tracks become one stereo file.";
    wav.container = "wav";
    wav.video = "none";
    wav.maxrateKbps = 0;
    wav.audio = "pcm24";
    add(wav);

    Preset flac = wav;
    flac.id = "flac";
    flac.suffix = "";
    flac.name = "Lossless audio, smaller (FLAC)";
    flac.description = "Bit-for-bit identical audio in about half the space of WAV.";
    flac.container = "flac";
    flac.audio = "flac";
    add(flac);

    Preset m4a = wav;
    m4a.id = "m4a";
    m4a.suffix = "";
    m4a.name = "Audio for listening (AAC M4A)";
    m4a.description = "Small audio-only file at 256 kbps for listening back.";
    m4a.container = "m4a";
    m4a.audio = "aac";
    m4a.audioKbps = 256;
    add(m4a);

    return list;
}

QString presetProblem(const Preset &p) {
    const bool prores = p.video.startsWith("prores");
    const bool pcm = p.audio.startsWith("pcm");
    if (p.container == "wav") {
        if (!p.audioOnly() || !pcm)
            return "WAV holds uncompressed audio only.";
    } else if (p.container == "flac") {
        if (!p.audioOnly() || p.audio != "flac")
            return "A .flac file holds FLAC audio only.";
    } else if (p.container == "m4a") {
        if (!p.audioOnly() || (p.audio != "aac" && p.audio != "alac"))
            return "An .m4a file holds AAC or ALAC audio only.";
    } else if (p.container == "mp4") {
        if (prores || p.video == "vp9")
            return "MP4 can't hold this video codec; use MOV or MKV.";
        if (pcm)
            return "MP4 can't hold uncompressed audio; use AAC, ALAC or FLAC, or the MOV container.";
    } else if (p.container == "mov") {
        if (p.video == "vp9")
            return "MOV can't hold VP9; use WebM or MKV.";
        if (p.audio == "opus" || p.audio == "flac" || p.audio == "mp3")
            return "Use AAC, ALAC or uncompressed audio in MOV.";
    } else if (p.container == "webm") {
        if (p.video != "vp9" && p.video != "none")
            return "WebM holds VP9 video only.";
        if (p.audio != "opus" && p.audio != "none")
            return "WebM holds Opus audio only.";
    }
    if (p.audioOnly() && p.audio == "none")
        return "This preset has neither video nor audio.";
    return {};
}

namespace {
// Timecode HH:MM:SS:FF plus a number of frames, for non-drop-frame timecode.
QString offsetTimecode(const QString &tc, qint64 frames, int base) {
    const auto parts = tc.split(QRegularExpression("[:;.]"));
    if (parts.size() != 4 || base <= 0 || tc.contains(';'))
        return tc;
    qint64 total = ((parts[0].toLongLong() * 60 + parts[1].toLongLong()) * 60 + parts[2].toLongLong()) * base
                   + parts[3].toLongLong() + frames;
    const qint64 ff = total % base;
    total /= base;
    return QString("%1:%2:%3:%4").arg(total / 3600 % 24, 2, 10, QChar('0')).arg(total / 60 % 60, 2, 10, QChar('0'))
        .arg(total % 60, 2, 10, QChar('0')).arg(ff, 2, 10, QChar('0'));
}

bool knownTag(const QString &value) {
    return !value.isEmpty() && value != "unknown" && value != "reserved" && value != "reserved0";
}

bool onGpu(const Preset &p) {
    return p.usesVideoEncoder() && (p.hardware == "nvenc" || p.hardware == "qsv" || p.hardware == "vaapi");
}

QString pixelFormatFor(const Preset &p) {
    if (p.video.startsWith("prores"))
        return "yuv422p10le";
    // GPU encoders take their own layouts: NV12 for 8-bit, P010 for 10-bit.
    if (onGpu(p) && p.hardware != "nvenc")
        return p.video == "hevc" ? "p010le" : "nv12";
    if (p.video == "hevc")
        return onGpu(p) ? "p010le" : "yuv420p10le";
    return "yuv420p";
}

// The x264 effort names mapped onto NVENC's p1 (fastest) to p7 (best).
QString nvencPreset(const QString &speed) {
    static const QHash<QString, QString> map{
        {"ultrafast", "p1"}, {"superfast", "p1"}, {"veryfast", "p2"}, {"faster", "p3"}, {"fast", "p4"},
        {"medium", "p5"}, {"slow", "p6"}, {"slower", "p7"}, {"veryslow", "p7"}};
    return map.value(speed, "p4");
}

// Quick Sync uses x264's names, but stops at veryfast.
QString qsvPreset(const QString &speed) {
    return speed == "ultrafast" || speed == "superfast" ? "veryfast" : speed;
}

QString gpuName(const QString &id) {
    return id == "nvenc" ? "NVIDIA NVENC" : id == "qsv" ? "Intel Quick Sync" : "VA-API";
}
}

namespace {
const char *kPreviewScale = "fps=1,scale=-2:360:flags=bilinear:out_range=full,format=yuvj420p";

QStringList previewOutput(const QString &label, const QString &path) {
    return {"-map", label, "-c:v", "mjpeg", "-q:v", "5", "-an", "-f", "image2", "-update", "1",
            "-atomic_writing", "1", path};
}
}

QStringList colourFixArguments(const MediaInfo &info) {
    if (!info.hasVideo || !info.colourTagsBroken())
        return {};
    // Colour labels live inside each compressed frame, so a decoder-side
    // override can't reach them; rewrite them in the bitstream instead.
    const bool hd = info.height >= 720;
    if (info.videoCodec == "prores")
        return {"-bsf:v", QString("prores_metadata=color_primaries=%1:color_trc=%1:colorspace=%1").arg(hd ? "bt709" : "smpte170m")};
    if (info.videoCodec == "h264" || info.videoCodec == "hevc")
        return {"-bsf:v", QString("%1_metadata=colour_primaries=%2:transfer_characteristics=%2:matrix_coefficients=%2")
                              .arg(info.videoCodec).arg(hd ? 1 : 6)};
    return {};
}

QStringList posterArguments(const MediaInfo &info, const QString &input, double at, const QString &jpeg, int height) {
    QStringList a{"-nostdin", "-hide_banner", "-y", "-loglevel", "error"};
    a << colourFixArguments(info);
    a << "-ss" << QString::number(std::max(0.0, at), 'f', 2) << "-i" << input << "-frames:v" << "1"
      << "-vf" << QString("scale=-2:%1:flags=bilinear:out_range=full,format=yuvj420p").arg(height) << "-q:v" << "4"
      << "-update" << "1" << jpeg;
    return a;
}

Command buildCommand(const Preset &p, const MediaInfo &info, const QString &input,
                     const QString &output, double start, double length,
                     const QString &previewOut, const QString &previewSource) {
    Command cmd;
    QStringList &a = cmd.arguments;
    a << "-nostdin" << "-hide_banner" << "-y" << "-loglevel" << "error"
      << "-nostats" << "-progress" << "pipe:1" << "-stats_period" << "0.5";

    const bool withVideo = info.hasVideo && !p.audioOnly();
    const bool hd = info.height >= 720;
    const QString defPrim = hd ? "bt709" : "smpte170m";
    const bool previews = withVideo && !previewOut.isEmpty() && !previewSource.isEmpty();

    const QStringList fix = withVideo ? colourFixArguments(info) : QStringList();
    if (!fix.isEmpty()) {
        a << fix;
        cmd.notes << QString("Colour labels set to %1").arg(hd ? "Rec. 709" : "Rec. 601");
    }

    const bool gpu = withVideo && onGpu(p);
    if (gpu && p.hardware == "vaapi")
        a << "-vaapi_device" << (p.hardwareDevice.isEmpty() ? QString("/dev/dri/renderD128") : p.hardwareDevice);

    if (start >= 0)
        a << "-ss" << QString::number(start, 'f', 3);
    a << "-i" << input;
    if (length > 0)
        a << "-t" << QString::number(length, 'f', 3);

    QStringList graph;
    if (withVideo) {
        QStringList chain;
        if (info.interlaced() && p.deinterlace) {
            chain << "bwdif=mode=send_frame";
            cmd.notes << "Deinterlaced (bwdif)";
        }
        if (p.maxHeight > 0 && info.height > p.maxHeight) {
            chain << QString("scale=-2:%1:flags=lanczos").arg(p.maxHeight);
            cmd.notes << QString("Scaled down to %1p").arg(p.maxHeight);
        }
        if (p.burnTimecode) {
            const int base = int(std::lround(info.fps));
            QString tc = info.timecode.isEmpty() ? QString("00:00:00:00") : info.timecode;
            if (start > 0)
                tc = offsetTimecode(tc, qint64(std::llround(start * info.fps)), base);
            QString escaped = tc;
            escaped.replace(":", "\\:").replace(";", "\\;");
            const QString rate = info.fpsRational.isEmpty() ? QString::number(info.fps) : info.fpsRational;
            chain << QString("drawtext=font=monospace:timecode='%1':rate=%2:fontsize=h/16:fontcolor=white"
                             ":box=1:boxcolor=black@0.55:boxborderw=10:x=(w-tw)/2:y=h-th-h/24")
                         .arg(escaped, rate);
            cmd.notes << QString("Timecode burned in from %1").arg(tc);
        }
        // Only plain characters reach drawtext; its escaping rules for quotes,
        // colons and percent signs are a trap not worth exposing.
        const QString mark = QString(p.watermark).remove(QRegularExpression("[^A-Za-z0-9 .!?&()#@+-]")).trimmed();
        if (!mark.isEmpty()) {
            chain << QString("drawtext=font=sans:text='%1':fontsize=h/12:fontcolor=white@0.35"
                             ":x=w-tw-h/24:y=h/24").arg(mark);
            cmd.notes << QString("Watermark \"%1\"").arg(mark);
        }
        chain << "format=" + pixelFormatFor(p);
        // VA-API encodes from GPU memory: only the encoder's branch is
        // uploaded, so the preview frames stay ordinary pictures.
        const bool upload = gpu && p.hardware == "vaapi";
        if (previews) {
            graph << "[0:v:0]split=2[vin][sv]";
            graph << "[vin]" + chain.join(',') + QString(",split=2[%1][pv]").arg(upload ? "vsw" : "v");
            if (upload)
                graph << "[vsw]hwupload[v]";
            graph << QString("[pv]%1[pvo]").arg(kPreviewScale);
            graph << QString("[sv]%1[svo]").arg(kPreviewScale);
        } else {
            graph << "[0:v:0]" + chain.join(',') + (upload ? ",hwupload" : "") + "[v]";
        }
    }

    const bool withAudio = !info.audio.isEmpty() && p.audio != "none";
    QStringList audioMaps;
    if (withAudio) {
        if (info.splitMono()) {
            graph << "[0:a:0][0:a:1]join=inputs=2:channel_layout=stereo[a]";
            audioMaps << "[a]";
            cmd.notes << "Mono tracks 1 and 2 joined into stereo (left, right)";
        } else if (withVideo) {
            // Every track: languages, commentary, separate mixes.
            for (const AudioTrack &t : info.audio)
                audioMaps << QString("0:a:%1").arg(t.index);
            if (audioMaps.size() > 1)
                cmd.notes << QString("All %1 audio tracks kept").arg(audioMaps.size());
        } else {
            // Audio files hold one track.
            audioMaps << "0:a:0";
            if (info.audio.size() > 1)
                cmd.notes << QString("Only audio track 1 of %1 converted: a %2 file holds one track")
                                 .arg(info.audio.size()).arg(p.container.toUpper());
        }
    }

    // Subtitles: text becomes the container's own kind; pictures (Blu-ray,
    // DVD) only fit MKV, which takes every kind as it is.
    QStringList subtitleMaps;
    QString subtitleCodec;
    if (withVideo && !info.subtitles.isEmpty()) {
        int pictures = 0;
        if (p.container == "mkv") {
            subtitleCodec = "copy";
        } else if (p.container == "mp4" || p.container == "mov" || p.container == "webm") {
            subtitleCodec = p.container == "webm" ? "webvtt" : "mov_text";
        }
        for (const SubtitleTrack &t : info.subtitles) {
            if (subtitleCodec.isEmpty())
                continue;
            if (subtitleCodec == "copy" || t.isText())
                subtitleMaps << QString("0:s:%1").arg(t.index);
            else
                ++pictures;
        }
        if (!subtitleMaps.isEmpty())
            cmd.notes << QString("%1 subtitle track%2 kept").arg(subtitleMaps.size()).arg(subtitleMaps.size() == 1 ? "" : "s");
        if (pictures > 0)
            cmd.notes << QString("%1 picture subtitle track%2 left out: %3 can't hold %4; MKV can")
                             .arg(pictures).arg(pictures == 1 ? "" : "s").arg(p.container.toUpper())
                             .arg(pictures == 1 ? "it" : "them");
        else if (subtitleCodec.isEmpty())
            cmd.notes << QString("Subtitles left out: a %1 file has no place for them").arg(p.container.toUpper());
    }

    if (!graph.isEmpty())
        a << "-filter_complex" << graph.join(';');

    if (withVideo) {
        a << "-map" << "[v]";
        const QString prim = knownTag(info.colorPrimaries) ? info.colorPrimaries : defPrim;
        const QString trc = knownTag(info.colorTransfer) ? info.colorTransfer : defPrim;
        const QString space = knownTag(info.colorSpace) ? info.colorSpace : defPrim;
        if (gpu) {
            const bool hevc = p.video == "hevc";
            const QString encoder = (hevc ? "hevc_" : "h264_") + p.hardware;
            a << "-c:v" << encoder;
            // Constant quality like the CPU path: CRF maps onto each family's
            // own quality scale, about one step coarser on NVENC.
            if (p.hardware == "nvenc")
                a << "-preset" << nvencPreset(p.speed) << "-tune" << "hq" << "-rc" << "vbr"
                  << "-cq" << QString::number(p.crf + 1) << "-b:v" << "0";
            else if (p.hardware == "qsv")
                a << "-preset" << qsvPreset(p.speed) << "-global_quality" << QString::number(p.crf);
            else
                a << "-rc_mode" << "CQP" << "-qp" << QString::number(p.crf);
            a << "-profile:v" << (hevc ? "main10" : "high");
            // VA-API in constant-QP mode has no rate cap to honour.
            if (p.maxrateKbps > 0 && p.hardware != "vaapi")
                a << "-maxrate" << QString("%1k").arg(p.maxrateKbps) << "-bufsize" << QString("%1k").arg(p.maxrateKbps * 2);
            if (hevc)
                a << "-tag:v" << "hvc1";
            cmd.notes << QString("Encoded on the GPU (%1)").arg(gpuName(p.hardware));
        } else if (p.video == "h264") {
            a << "-c:v" << "libx264" << "-preset" << p.speed << "-crf" << QString::number(p.crf)
              << "-profile:v" << "high";
            if (p.maxrateKbps > 0)
                a << "-maxrate" << QString("%1k").arg(p.maxrateKbps) << "-bufsize" << QString("%1k").arg(p.maxrateKbps * 2);
        } else if (p.video == "hevc") {
            QString params = "log-level=error";
            if (p.maxrateKbps > 0)
                params += QString(":vbv-maxrate=%1:vbv-bufsize=%2").arg(p.maxrateKbps).arg(p.maxrateKbps * 2);
            a << "-c:v" << "libx265" << "-preset" << p.speed << "-crf" << QString::number(p.crf)
              << "-tag:v" << "hvc1" << "-x265-params" << params;
        } else if (p.video.startsWith("prores")) {
            const QStringList profiles{"prores_proxy", "prores_lt", "prores_422", "prores_hq"};
            a << "-c:v" << "prores_ks" << "-profile:v" << QString::number(std::max<qsizetype>(0, profiles.indexOf(p.video)))
              << "-vendor" << "apl0";
        } else if (p.video == "vp9") {
            a << "-c:v" << "libvpx-vp9" << "-crf" << QString::number(p.crf) << "-b:v" << "0"
              << "-row-mt" << "1" << "-deadline" << "good" << "-cpu-used" << "2";
        }
        a << "-color_primaries" << prim << "-color_trc" << trc << "-colorspace" << space;
    } else {
        a << "-vn";
    }

    if (withAudio) {
        for (const QString &map : audioMaps)
            a << "-map" << map;
        const int rate = info.audio.first().sampleRate;
        const bool lossy = p.audio == "aac" || p.audio == "opus" || p.audio == "mp3";
        if (p.audio == "aac")
            a << "-c:a" << "aac" << "-b:a" << QString("%1k").arg(p.audioKbps);
        else if (p.audio == "opus")
            a << "-c:a" << "libopus" << "-b:a" << QString("%1k").arg(p.audioKbps);
        else if (p.audio == "mp3")
            a << "-c:a" << "libmp3lame" << "-b:a" << QString("%1k").arg(p.audioKbps);
        else if (p.audio == "pcm24")
            a << "-c:a" << "pcm_s24le";
        else if (p.audio == "pcm16")
            a << "-c:a" << "pcm_s16le";
        else if (p.audio == "flac")
            a << "-c:a" << "flac";
        else if (p.audio == "alac")
            a << "-c:a" << "alac";
        if (p.audio == "opus" || (lossy && rate > 48000))
            a << "-ar" << "48000";
    } else {
        a << "-an";
    }

    for (const QString &map : subtitleMaps)
        a << "-map" << map;
    if (!subtitleMaps.isEmpty())
        a << "-c:s" << subtitleCodec;
    else
        a << "-sn";
    // Data tracks (timecode) are rebuilt rather than copied.
    a << "-dn";
    const bool quicktime = p.container == "mp4" || p.container == "mov" || p.container == "m4a";
    if (quicktime && withVideo && !info.timecode.isEmpty() && start < 0)
        a << "-timecode" << info.timecode;
    if (quicktime)
        a << "-movflags" << "+faststart";
    a << output;
    if (previews)
        a << previewOutput("[pvo]", previewOut) << previewOutput("[svo]", previewSource);
    return cmd;
}

QString shellQuote(const QStringList &arguments) {
    static const QRegularExpression safe("^[A-Za-z0-9_./:=+,@%-]+$");
    QStringList out;
    for (const auto &arg : arguments) {
        if (safe.match(arg).hasMatch())
            out << arg;
        else
            out << "'" + QString(arg).replace("'", "'\\''") + "'";
    }
    return out.join(' ');
}
