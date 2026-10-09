#include "preset.h"

#include <QRegularExpression>
#include <cmath>

QVariantMap Preset::toVariant() const {
    return {
        {"id", id}, {"name", name}, {"description", description}, {"suffix", suffix}, {"builtin", builtin},
        {"container", container}, {"video", video}, {"crf", crf}, {"maxrateKbps", maxrateKbps},
        {"speed", speed}, {"maxHeight", maxHeight}, {"audio", audio}, {"audioKbps", audioKbps},
        {"deinterlace", deinterlace}, {"burnTimecode", burnTimecode}, {"watermark", watermark},
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

QString pixelFormatFor(const Preset &p) {
    if (p.video.startsWith("prores"))
        return "yuv422p10le";
    if (p.video == "hevc")
        return "yuv420p10le";
    return "yuv420p";
}
}

Command buildCommand(const Preset &p, const MediaInfo &info, const QString &input,
                     const QString &output, double start, double length) {
    Command cmd;
    QStringList &a = cmd.arguments;
    a << "-nostdin" << "-hide_banner" << "-y" << "-loglevel" << "error"
      << "-nostats" << "-progress" << "pipe:1" << "-stats_period" << "0.5";

    const bool withVideo = info.hasVideo && !p.audioOnly();
    const bool hd = info.height >= 720;
    const QString defPrim = hd ? "bt709" : "smpte170m";

    // Colour labels live inside each compressed frame, so a decoder-side
    // override can't reach them; rewrite them in the bitstream instead.
    if (withVideo && info.colourTagsBroken()) {
        const QString codec = info.videoCodec;
        if (codec == "prores") {
            a << "-bsf:v" << QString("prores_metadata=color_primaries=%1:color_trc=%1:colorspace=%1").arg(defPrim);
            cmd.notes << QString("Colour labels set to %1").arg(hd ? "Rec. 709" : "Rec. 601");
        } else if (codec == "h264" || codec == "hevc") {
            const int code = hd ? 1 : 6;
            a << "-bsf:v" << QString("%1_metadata=colour_primaries=%2:transfer_characteristics=%2:matrix_coefficients=%2")
                                 .arg(codec).arg(code);
            cmd.notes << QString("Colour labels set to %1").arg(hd ? "Rec. 709" : "Rec. 601");
        }
    }

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
        graph << "[0:v:0]" + chain.join(',') + "[v]";
    }

    const bool withAudio = !info.audio.isEmpty() && p.audio != "none";
    QString audioMap;
    if (withAudio) {
        if (info.splitMono()) {
            graph << "[0:a:0][0:a:1]join=inputs=2:channel_layout=stereo[a]";
            audioMap = "[a]";
            cmd.notes << "Mono tracks 1 and 2 joined into stereo (left, right)";
        } else {
            audioMap = "0:a:0";
        }
    }

    if (!graph.isEmpty())
        a << "-filter_complex" << graph.join(';');

    if (withVideo) {
        a << "-map" << "[v]";
        const QString prim = knownTag(info.colorPrimaries) ? info.colorPrimaries : defPrim;
        const QString trc = knownTag(info.colorTransfer) ? info.colorTransfer : defPrim;
        const QString space = knownTag(info.colorSpace) ? info.colorSpace : defPrim;
        if (p.video == "h264") {
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
        a << "-map" << audioMap;
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

    // Data tracks (timecode, captions) are rebuilt rather than copied.
    a << "-dn" << "-sn";
    const bool quicktime = p.container == "mp4" || p.container == "mov" || p.container == "m4a";
    if (quicktime && withVideo && !info.timecode.isEmpty() && start < 0)
        a << "-timecode" << info.timecode;
    if (quicktime)
        a << "-movflags" << "+faststart";
    a << output;
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
