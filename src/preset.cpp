#include "preset.h"

#include <QFileInfo>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <algorithm>
#include <cmath>

QVariantMap Preset::toVariant() const {
    return {
        {"id", id}, {"name", name}, {"description", description}, {"suffix", suffix}, {"builtin", builtin},
        {"container", container}, {"video", video}, {"crf", crf}, {"maxrateKbps", maxrateKbps},
        {"speed", speed}, {"maxHeight", maxHeight}, {"audio", audio}, {"audioKbps", audioKbps}, {"loudness", loudness},
        {"deinterlace", deinterlace}, {"burnTimecode", burnTimecode}, {"watermark", watermark},
        {"lut", lut}, {"logo", logo}, {"logoPosition", logoPosition}, {"logoSize", logoSize}, {"logoOpacity", logoOpacity},
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
    p.loudness = m.value("loudness", p.loudness).toString();
    p.deinterlace = m.value("deinterlace", p.deinterlace).toBool();
    p.burnTimecode = m.value("burnTimecode", p.burnTimecode).toBool();
    p.watermark = m.value("watermark").toString();
    p.lut = m.value("lut").toString();
    p.logo = m.value("logo").toString();
    p.logoPosition = m.value("logoPosition", p.logoPosition).toString();
    p.logoSize = std::clamp(m.value("logoSize", p.logoSize).toInt(), 2, 50);
    p.logoOpacity = std::clamp(m.value("logoOpacity", p.logoOpacity).toInt(), 5, 100);
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

    // SVT-AV1 runs at the quality slider + 15: on a 1080p60 clip, its CRF 33
    // came within half a point of x264 CRF 18's VMAF (91.9 against 92.3) at
    // 45% of the size, in about the same time at its preset 8 ("fast").
    Preset av1;
    av1.id = "av1";
    av1.suffix = "_av1";
    av1.name = "Smallest files (AV1 MP4)";
    av1.description = "About half the size of H.264 at the same quality. Plays in browsers, on Android, "
                      "and on recent Apple devices and TVs.";
    av1.video = "av1";
    av1.crf = 18;
    av1.maxrateKbps = 0;
    av1.audioKbps = 192;
    add(av1);

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

    // DNxHR: what Resolve on Linux reads where it can't read H.264 or AAC.
    Preset dnxProxy = proxy;
    dnxProxy.id = "dnxhr-lb";
    dnxProxy.suffix = "_dnxlb";
    dnxProxy.name = "Edit proxy (DNxHR LB)";
    dnxProxy.description = "Light editing proxy for Resolve (including on Linux), Avid and Premiere. "
                           "Uncompressed 24-bit audio.";
    dnxProxy.video = "dnxhr_lb";
    add(dnxProxy);

    Preset dnxMaster = dnxProxy;
    dnxMaster.id = "dnxhr-hq";
    dnxMaster.suffix = "_dnxhq";
    dnxMaster.name = "Edit master (DNxHR HQ)";
    dnxMaster.description = "High-quality 8-bit intermediate for finishing in Resolve, Avid or Premiere. "
                            "Large files. Uncompressed 24-bit audio.";
    dnxMaster.video = "dnxhr_hq";
    add(dnxMaster);

    // Lossless and checkable: every frame a keyframe, every slice with a
    // checksum (FFV1 level 3, as archives use it), the source's own pixel
    // format, FLAC audio, in MKV.
    Preset ffv1;
    ffv1.id = "ffv1";
    ffv1.suffix = "_ffv1";
    ffv1.name = "Archive master (FFV1 MKV, lossless)";
    ffv1.description = "Mathematically lossless video and audio for keeping: often a third to half the size "
                       "of the uncompressed picture. Big files, for archives, not playback.";
    ffv1.container = "mkv";
    ffv1.video = "ffv1";
    ffv1.deinterlace = false;    // kept as it was shot
    ffv1.maxrateKbps = 0;
    ffv1.audio = "flac";
    add(ffv1);

    // No re-encoding: seconds instead of minutes, and not a bit of quality
    // lost. With in and out points it's a lossless cut.
    Preset copy;
    copy.id = "copy";
    copy.suffix = "_copy";
    copy.name = "Cut or copy, no re-encoding (same file type)";
    copy.description = "Keeps every stream exactly as it is. Set in and out points for a lossless cut; "
                       "cuts start on the nearest keyframe.";
    copy.container = "source";
    copy.video = "copy";
    copy.audio = "copy";
    add(copy);

    Preset rewrap = copy;
    rewrap.id = "rewrap-mp4";
    rewrap.suffix = "_rewrap";
    rewrap.name = "Rewrap to MP4, no re-encoding";
    rewrap.description = "Moves H.264 or HEVC video and AAC audio into an MP4 that plays anywhere, in seconds, "
                         "without touching the picture or sound.";
    rewrap.container = "mp4";
    add(rewrap);

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
    const bool prores = p.video.startsWith("prores") || p.video.startsWith("dnxhr");
    const bool pcm = p.audio.startsWith("pcm");
    if (p.container == "wav") {
        if (!p.audioOnly() || !(pcm || p.copiesAudio()))
            return "WAV holds uncompressed audio only.";
    } else if (p.container == "flac") {
        if (!p.audioOnly() || (p.audio != "flac" && !p.copiesAudio()))
            return "A .flac file holds FLAC audio only.";
    } else if (p.container == "m4a") {
        if (!p.audioOnly() || (p.audio != "aac" && p.audio != "alac" && !p.copiesAudio()))
            return "An .m4a file holds AAC or ALAC audio only.";
    } else if (p.video == "ffv1" && p.container != "mkv" && p.container != "source") {
        return "FFV1 goes in MKV (Matroska) only.";
    } else if (p.container == "mp4") {
        if (prores || p.video == "vp9")
            return "MP4 can't hold this video codec; use MOV or MKV.";
        if (pcm)
            return "MP4 can't hold uncompressed audio; use AAC, ALAC or FLAC, or the MOV container.";
    } else if (p.container == "mov") {
        if (p.video == "vp9" || p.video == "av1")
            return QString("MOV can't hold %1; use %2 or MKV.").arg(p.video == "vp9" ? "VP9" : "AV1", p.video == "vp9" ? "WebM" : "MP4");
        if (p.audio == "opus" || p.audio == "flac" || p.audio == "mp3")
            return "Use AAC, ALAC or uncompressed audio in MOV.";
    } else if (p.container == "webm") {
        if (p.video != "vp9" && p.video != "av1" && p.video != "none" && !p.copiesVideo())
            return "WebM holds VP9 or AV1 video only.";
        if (p.audio != "opus" && p.audio != "none" && !p.copiesAudio())
            return "WebM holds Opus audio only.";
    }
    if (p.audioOnly() && p.audio == "none")
        return "This preset has neither video nor audio.";
    if (p.copiesVideo() && (!p.lut.isEmpty() || !p.logo.isEmpty()))
        return QString("A %1 changes the picture, so the video can't be copied as it is. Choose a video codec, "
                       "or remove it.").arg(p.lut.isEmpty() ? "logo" : "LUT");
    if (p.copiesAudio() && loudnessTarget(p.loudness).lufs < 0)
        return "Setting the loudness changes the audio, so it can't be copied as it is. Choose an audio codec, "
               "or leave the loudness as recorded.";
    return {};
}

QString containerFor(const Preset &p, const MediaInfo &info) {
    if (p.container != "source")
        return p.container;
    const QString ext = QFileInfo(info.path).suffix().toLower();
    return ext.isEmpty() ? QString("mkv") : ext;
}

namespace {
// What each container takes as it is. MKV takes anything; the source's own
// container ("source") takes what it already holds.
const QHash<QString, QStringList> kCopyVideo{
    {"mp4", {"h264", "hevc", "av1", "vp9", "mpeg4", "mpeg2video", "mpeg1video"}},
    {"mov", {"h264", "hevc", "prores", "dnxhd", "mjpeg", "mpeg2video", "mpeg4", "png", "qtrle"}},
    {"webm", {"vp8", "vp9", "av1"}},
};
const QHash<QString, QStringList> kCopyAudio{
    {"mp4", {"aac", "mp3", "ac3", "eac3", "opus", "flac", "alac"}},
    {"mov", {"aac", "mp3", "ac3", "eac3", "alac"}},
    {"m4a", {"aac", "alac"}},
    {"webm", {"opus", "vorbis"}},
    {"flac", {"flac"}},
    {"wav", {}},
};
}

QString codecName(const QString &codec) {
    static const QHash<QString, QString> names{
        {"h264", "H.264"}, {"hevc", "HEVC"}, {"prores", "ProRes"}, {"dnxhd", "DNxHD/DNxHR"}, {"vp9", "VP9"},
        {"vp8", "VP8"}, {"av1", "AV1"}, {"ffv1", "FFV1"}, {"mpeg2video", "MPEG-2"}, {"mjpeg", "Motion JPEG"}, {"aac", "AAC"},
        {"ac3", "AC-3"}, {"eac3", "E-AC-3"}, {"truehd", "Dolby TrueHD"}, {"dts", "DTS"}, {"opus", "Opus"},
        {"vorbis", "Vorbis"}, {"flac", "FLAC"}, {"alac", "ALAC"}, {"mp3", "MP3"},
        {"hdmv_pgs_subtitle", "Blu-ray (PGS)"}, {"dvd_subtitle", "DVD"}, {"dvb_subtitle", "DVB"}};
    if (codec.startsWith("pcm_"))
        return "uncompressed (PCM)";
    return names.value(codec, codec);
}

QString sourceProblem(const Preset &p, const MediaInfo &info) {
    if (!info.valid)
        return {};
    if (!p.lut.isEmpty() && !QFileInfo::exists(p.lut))
        return QString("The LUT %1 isn't there any more.").arg(QFileInfo(p.lut).fileName());
    if (!p.logo.isEmpty() && !QFileInfo::exists(p.logo))
        return QString("The logo %1 isn't there any more.").arg(QFileInfo(p.logo).fileName());
    if (p.burnsSubtitles() && p.copiesVideo())
        return "Burning in subtitles changes the picture, so the video can't be copied as it is. Choose a video "
               "codec, or turn the subtitles off.";
    if (p.crops() && p.copiesVideo())
        return "Cropping changes the picture, so the video can't be copied as it is. Choose a video codec, "
               "or turn the crop off.";
    if (p.pads() && p.copiesVideo())
        return "Adding bars changes the picture, so the video can't be copied as it is. Choose a video codec, "
               "or turn the bars off.";
    if (p.joinsByFilter() && (p.copiesVideo() || p.copiesAudio()))
        return QString("These clips differ (%1), so they can only be joined by converting them. Choose a preset "
                       "that converts the video and audio, or split them.").arg(p.joinDifference);
    if (p.crops() && (info.width - p.cropLeft - p.cropRight < 16 || info.height - p.cropTop - p.cropBottom < 16))
        return "The crop leaves almost nothing of the picture.";
    if (p.video == "ffv1" && containerFor(p, info) != "mkv")
        return QString("FFV1 goes in MKV (Matroska) only, and this file is %1. Choose MKV as the file type.")
            .arg(containerFor(p, info).toUpper());
    if (p.container == "source" || p.container == "mkv")
        return {};
    const QString container = p.container.toUpper();
    if (p.copiesVideo() && info.hasVideo && kCopyVideo.contains(p.container)
        && !kCopyVideo.value(p.container).contains(info.videoCodec))
        return QString("This file's %1 video can't go into %2 without converting it. Copy it into MKV or "
                       "the same file type instead, or convert the video.")
            .arg(codecName(info.videoCodec), container);
    if (p.copiesAudio()) {
        for (const AudioTrack &t : info.audio) {
            if (p.audioOnly() && t.index > 0)
                break;    // audio files take track 1 only
            const bool fits = p.container == "wav" ? t.codec.startsWith("pcm_")
                              : p.container == "mov" ? (t.codec.startsWith("pcm_") || kCopyAudio.value("mov").contains(t.codec))
                                                     : kCopyAudio.value(p.container).contains(t.codec);
            if (kCopyAudio.contains(p.container) && !fits)
                return QString("Audio track %1 is %2, which %3 can't hold without converting it. Copy it into MKV "
                               "or the same file type instead, or convert the audio.")
                    .arg(t.index + 1).arg(codecName(t.codec), container);
        }
    }
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
    if (p.video.startsWith("prores") || p.video == "dnxhr_hqx")
        return "yuv422p10le";
    if (p.video.startsWith("dnxhr"))
        return "yuv422p";
    // GPU encoders take their own layouts: NV12 for 8-bit, P010 for 10-bit.
    if (onGpu(p) && p.hardware != "nvenc")
        return p.video == "hevc" ? "p010le" : "nv12";
    if (p.video == "hevc")
        return onGpu(p) ? "p010le" : "yuv420p10le";
    // 10-bit AV1 is the main profile every decoder plays, and bands less.
    if (p.video == "av1")
        return "yuv420p10le";
    return "yuv420p";
}

// FFV1 keeps the source's own layout, so nothing is lost on the way in;
// the few it can't store go to the nearest one that holds all of it.
QString ffv1PixelFormat(const MediaInfo &info) {
    static const QHash<QString, QString> nearest{
        {"yuvj420p", "yuv420p"}, {"yuvj422p", "yuv422p"}, {"yuvj444p", "yuv444p"}, {"nv12", "yuv420p"},
        {"p010le", "yuv420p10le"}, {"rgb24", "gbrp"}, {"bgr24", "gbrp"}, {"rgba", "bgra"}, {"argb", "bgra"},
        {"abgr", "bgra"}, {"rgb48be", "gbrp16le"}};
    static const QStringList stored{
        "yuv420p", "yuv422p", "yuv444p", "yuv440p", "yuv411p", "yuv410p", "yuva420p", "yuva422p", "yuva444p",
        "yuv420p10le", "yuv422p10le", "yuv444p10le", "yuv420p12le", "yuv422p12le", "yuv444p12le",
        "yuv420p16le", "yuv422p16le", "yuv444p16le", "gray", "gray10le", "gray12le", "gray16le",
        "gbrp", "gbrp10le", "gbrp12le", "gbrp16le", "gbrap10le", "gbrap12le", "gbrap16le", "bgra", "bgr0", "rgb48le"};
    const QString source = info.pixelFormat;
    if (stored.contains(source))
        return source;
    if (nearest.contains(source))
        return nearest.value(source);
    return info.bitDepth > 8 ? QString("yuv444p16le") : QString("yuv444p");
}

// x264's effort names mapped onto SVT-AV1's presets (13 fastest, 0 best);
// 8 for "fast" kept pace with x264 fast.
int svtPreset(const QString &speed) {
    static const QHash<QString, int> map{
        {"ultrafast", 12}, {"superfast", 11}, {"veryfast", 10}, {"faster", 9}, {"fast", 8},
        {"medium", 7}, {"slow", 6}, {"slower", 5}, {"veryslow", 4}};
    return map.value(speed, 8);
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

QStringList stripArguments(const MediaInfo &info, const QString &input, int count, int height, const QString &jpeg) {
    QStringList a{"-nostdin", "-hide_banner", "-y", "-loglevel", "error"};
    QStringList graph, labels;
    const double last = std::max(0.0, info.duration - 1);
    for (int i = 0; i < count; ++i) {
        const double at = std::min(last, info.duration * (i + 0.5) / count);
        a << colourFixArguments(info) << "-ss" << QString::number(at, 'f', 2) << "-i" << input;
        graph << QString("[%1:v:0]scale=-2:%2:flags=bilinear:out_range=full,format=yuvj420p,setsar=1[t%1]").arg(i).arg(height);
        labels << QString("[t%1]").arg(i);
    }
    graph << labels.join("") + QString("hstack=inputs=%1[s]").arg(count);
    a << "-filter_complex" << graph.join(';') << "-map" << "[s]" << "-frames:v" << "1" << "-q:v" << "5"
      << "-update" << "1" << jpeg;
    return a;
}

QStringList posterArguments(const MediaInfo &info, const QString &input, double at, const QString &jpeg, int height) {
    QStringList a{"-nostdin", "-hide_banner", "-y", "-loglevel", "error"};
    a << colourFixArguments(info);
    a << "-ss" << QString::number(std::max(0.0, at), 'f', 2) << "-i" << input << "-frames:v" << "1"
      << "-vf" << QString("scale=-2:%1:flags=bilinear:out_range=full,format=yuvj420p").arg(height) << "-q:v" << "4"
      << "-update" << "1" << jpeg;
    return a;
}

LoudnessTarget loudnessTarget(const QString &id) {
    if (id == "ebu")
        return {-23, -1, "EBU R128"};
    if (id == "atsc")
        return {-24, -2, "ATSC A/85"};
    if (id == "web")
        return {-16, -1, "podcasts and web"};
    if (id == "stream")
        return {-14, -1, "streaming"};
    return {};
}

Loudness parseLoudnorm(const QByteArray &output) {
    Loudness l;
    const qsizetype open = output.lastIndexOf('{'), close = output.lastIndexOf('}');
    if (open < 0 || close < open)
        return l;
    const QJsonObject o = QJsonDocument::fromJson(output.mid(open, close - open + 1)).object();
    auto number = [&o](const char *key) {
        bool ok = false;
        const double v = o.value(key).toString().toDouble(&ok);
        return ok && std::isfinite(v) ? v : NAN;
    };
    l.input = number("input_i");
    l.truePeak = number("input_tp");
    l.range = number("input_lra");
    l.threshold = number("input_thresh");
    const double offset = number("target_offset");
    l.offset = std::isfinite(offset) ? offset : 0;
    return l;
}

namespace {
// One output audio stream: its inputs ("[0:a:1]") and any filter that makes
// it (joining split mono tracks).
struct AudioStream {
    QString inputs;
    QString filter;
    int sampleRate = 48000;
};

QList<AudioStream> audioStreams(const Preset &p, const MediaInfo &info) {
    QList<AudioStream> list;
    if (info.audio.isEmpty() || p.audio == "none")
        return list;
    const int rate = info.audio.first().sampleRate > 0 ? info.audio.first().sampleRate : 48000;
    if (info.splitMono() && !p.copiesAudio()) {
        list.append({"[0:a:0][0:a:1]", "join=inputs=2:channel_layout=stereo", rate});
    } else if (info.hasVideo && !p.audioOnly()) {
        for (const AudioTrack &t : info.audio)
            list.append({QString("[0:a:%1]").arg(t.index), {}, t.sampleRate > 0 ? t.sampleRate : 48000});
    } else {
        list.append({"[0:a:0]", {}, rate});
    }
    return list;
}

QString loudnormFilter(const LoudnessTarget &t, const Loudness &m) {
    QString f = QString("loudnorm=I=%1:TP=%2").arg(t.lufs).arg(t.truePeak);
    if (!m.valid())
        return f + ":LRA=11";
    // A target range at least as wide as the source's keeps loudnorm in
    // linear mode: one gain for the whole track, the dynamics untouched.
    const int lra = std::clamp(int(std::ceil(m.range)) + 1, 7, 20);
    return f + QString(":LRA=%1:measured_I=%2:measured_TP=%3:measured_LRA=%4:measured_thresh=%5:offset=%6:linear=true")
                   .arg(lra).arg(m.input, 0, 'f', 2).arg(m.truePeak, 0, 'f', 2).arg(m.range, 0, 'f', 2)
                   .arg(m.threshold, 0, 'f', 2).arg(m.offset, 0, 'f', 2);
}
}

namespace {
// The inputs: the source, the clips joined after it as one concat-list file
// when they match, or every clip as an input of its own when they don't
// (never trimmed). The colour fix goes before each input it applies to.
QStringList inputArguments(const Preset &p, const MediaInfo &info, const QString &input, double start, double length,
                           bool colourFix) {
    QStringList a;
    if (p.joinsByFilter()) {
        for (const MediaInfo &c : p.joinClips)
            a << (colourFix ? colourFixArguments(c) : QStringList()) << "-i" << c.path;
        return a;
    }
    if (colourFix)
        a << colourFixArguments(info);
    if (start >= 0)
        a << "-ss" << QString::number(start, 'f', 3);
    if (p.joins())
        a << "-f" << "concat" << "-safe" << "0" << "-i" << p.joinList;
    else
        a << "-i" << input;
    if (length > 0)
        a << "-t" << QString::number(length, 'f', 3);
    return a;
}

// Clips that differ, joined by the concat filter: each one's picture fitted
// to the first's size, rate and pixel format (bars where the shape
// differs), and its first audio track to the first's rate and channels.
// A clip without one gets black or silence for its length. Ends in [jv]
// and [ja].
QString joinGraph(const Preset &p, const MediaInfo &info, bool video, bool audio) {
    const int w = info.width, h = info.height;
    const QString rate = info.fpsRational.isEmpty() ? QString::number(info.fps > 0 ? info.fps : 25) : info.fpsRational;
    const QString pixels = info.pixelFormat.isEmpty() ? QString("yuv420p") : info.pixelFormat;
    const AudioTrack first = info.audio.isEmpty() ? AudioTrack() : info.audio.first();
    const int sampleRate = first.sampleRate > 0 ? first.sampleRate : 48000;
    const QString layout = !first.layout.isEmpty() ? first.layout : first.channels == 1 ? QString("mono") : QString("stereo");
    QStringList graph;
    QString labels;
    for (int i = 0; i < p.joinClips.size(); ++i) {
        const MediaInfo &c = p.joinClips[i];
        const QString length = QString::number(std::max(0.04, c.duration), 'f', 3);
        if (video) {
            graph << (c.hasVideo
                          ? QString("[%1:v:0]scale=%2:%3:force_original_aspect_ratio=decrease,pad=%2:%3:(ow-iw)/2:(oh-ih)/2"
                                    ":color=black,setsar=1,fps=%4,format=%5[jv%1]")
                                .arg(i).arg(w).arg(h).arg(rate, pixels)
                          : QString("color=c=black:s=%2x%3:r=%4:d=%5,format=%6,setsar=1[jv%1]")
                                .arg(i).arg(w).arg(h).arg(rate, length, pixels));
            labels += QString("[jv%1]").arg(i);
        }
        if (audio) {
            graph << (!c.audio.isEmpty()
                          ? QString("[%1:a:0]aresample=%2,aformat=sample_rates=%2:channel_layouts=%3[ja%1]")
                                .arg(i).arg(sampleRate).arg(layout)
                          : QString("anullsrc=r=%2:cl=%3,atrim=duration=%4[ja%1]").arg(i).arg(sampleRate).arg(layout, length));
            labels += QString("[ja%1]").arg(i);
        }
    }
    graph << labels + QString("concat=n=%1:v=%2:a=%3%4%5")
                          .arg(p.joinClips.size()).arg(video ? 1 : 0).arg(audio ? 1 : 0)
                          .arg(video ? "[jv]" : "", audio ? "[ja]" : "");
    return graph.join(';');
}
}

QString clipDifference(const MediaInfo &a, const MediaInfo &b) {
    if (a.hasVideo != b.hasVideo)
        return "one has no picture";
    if (a.hasVideo) {
        if (a.videoCodec != b.videoCodec)
            return QString("%1 and %2 video").arg(codecName(a.videoCodec), codecName(b.videoCodec));
        if (a.width != b.width || a.height != b.height)
            return QString("%1×%2 and %3×%4").arg(a.width).arg(a.height).arg(b.width).arg(b.height);
        if (std::abs(a.fps - b.fps) > 0.01)
            return QString("%1 and %2 fps").arg(a.fps, 0, 'g', 4).arg(b.fps, 0, 'g', 4);
        if (a.pixelFormat != b.pixelFormat)
            return QString("%1 and %2 pictures").arg(a.pixelFormat, b.pixelFormat);
    }
    if (a.audio.size() != b.audio.size())
        return QString("%1 and %2 audio tracks").arg(a.audio.size()).arg(b.audio.size());
    for (int i = 0; i < a.audio.size(); ++i) {
        const AudioTrack &x = a.audio[i], &y = b.audio[i];
        if (x.codec != y.codec)
            return QString("%1 and %2 audio").arg(codecName(x.codec), codecName(y.codec));
        if (x.sampleRate != y.sampleRate)
            return QString("audio at %1 and %2 kHz").arg(x.sampleRate / 1000.0).arg(y.sampleRate / 1000.0);
        if (x.channels != y.channels)
            return QString("%1 and %2 audio channels").arg(x.channels).arg(y.channels);
    }
    if (a.subtitles.size() != b.subtitles.size())
        return QString("%1 and %2 subtitle tracks").arg(a.subtitles.size()).arg(b.subtitles.size());
    return {};
}

QString concatList(const QStringList &paths) {
    QString list = "ffconcat version 1.0\n";
    for (QString path : paths)
        list += "file '" + path.replace("'", "'\\''") + "'\n";
    return list;
}

int audioStreamCount(const Preset &p, const MediaInfo &info) {
    return int(audioStreams(p, info).size());
}

QStringList loudnessArguments(const Preset &p, const MediaInfo &info, const QString &input, int stream,
                              double start, double length) {
    const QList<AudioStream> streams = audioStreams(p, info);
    if (stream < 0 || stream >= streams.size())
        return {};
    const AudioStream &s = streams[stream];
    const LoudnessTarget t = loudnessTarget(p.loudness);
    QStringList a{"-nostdin", "-hide_banner", "-nostats"};
    a << inputArguments(p, info, input, start, length, false);
    const QString chain = (s.filter.isEmpty() ? QString() : s.filter + ",")
                          + QString("loudnorm=I=%1:TP=%2:LRA=11:print_format=json").arg(t.lufs).arg(t.truePeak);
    // Clips that differ are measured as the conversion joins them.
    const QString graph = p.joinsByFilter() ? joinGraph(p, info, false, true) + ";[ja]" + chain + "[m]"
                                            : s.inputs + chain + "[m]";
    a << "-filter_complex" << graph << "-map" << "[m]" << "-f" << "null" << "-";
    return a;
}

namespace {
// What an overlay works in: the picture's own chroma layout and depth.
// Left to choose ("auto"), an RGB overlay (a PNG logo, picture subtitles)
// pulls the whole picture into full-range YUV: every pixel shifts a
// little, and the file comes out labelled full range.
QString overlayFormat(const MediaInfo &info) {
    const QString f = info.pixelFormat;
    static const QRegularExpression deep("p(10|12|14|16)");
    const bool high = info.bitDepth > 8 || deep.match(f).hasMatch();
    const QString chroma = f.contains("444") || f.startsWith("gbr") || f.startsWith("rgb") || f.startsWith("bgr") ? "yuv444"
                         : f.contains("422") ? "yuv422" : "yuv420";
    return high ? chroma + "p10" : chroma;
}

// The same as a pixel format: yuv420p, yuv422p10le…
QString yuvLayout(const MediaInfo &info) {
    const QString f = overlayFormat(info);
    return f.endsWith("p10") ? f + "le" : f + "p";
}

// The picture's processing, in order, for the encode and the Output still
// alike: deinterlace, LUT, crop, scale, pad, logo, subtitles, timecode,
// watermark.
// `start` is where the input begins on the source's clock (-1 = the start).
QStringList videoChain(const Preset &p, const MediaInfo &info, double start, QStringList &notes) {
    QStringList chain;
    if (info.interlaced() && p.deinterlace) {
        chain << "bwdif=mode=send_frame";
        notes << "Deinterlaced (bwdif)";
    }
    // The LUT first: it expects the camera's picture as it was shot.
    const QString lut = p.lutFile.isEmpty() ? p.lut : p.lutFile;
    if (!lut.isEmpty()) {
        // The LUT works in RGB; back to ordinary limited-range YUV at once,
        // or an overlay after it (logo, picture subtitles) takes the RGB
        // as full range and so does the whole file.
        chain << "lut3d=file=" + lut + ":interp=tetrahedral,scale=out_range=tv,format=" + yuvLayout(info);
        notes << "LUT applied: " + QFileInfo(p.lut.isEmpty() ? lut : p.lut).fileName();
    }
    // Picture subtitles are drawn for the source's frame, so they go on
    // before the crop and scaling and follow the picture through them (but
    // after the LUT, which would tint them). The source's own subtitle
    // stream joins the line through a labelled detour, as the logo does.
    if (p.burnsSubtitles() && p.burnSubtitlePicture) {
        QString canvas;
        for (const SubtitleTrack &t : info.subtitles)
            if (t.index == p.burnSubtitleTrack && t.width > 0 && t.height > 0
                && (t.width != info.width || t.height != info.height))
                canvas = QString("scale=%1:%2").arg(info.width).arg(info.height);
        chain << (canvas.isEmpty()
                      ? QString("null[sbv];[sbv][0:s:%1]overlay=eof_action=pass:format=%2").arg(p.burnSubtitleTrack).arg(overlayFormat(info))
                      : QString("null[sbv];[0:s:%1]%2[sbs];[sbv][sbs]overlay=eof_action=pass:format=%3")
                            .arg(p.burnSubtitleTrack).arg(canvas, overlayFormat(info)));
        notes << "Subtitles burned in from " + p.burnSubtitleName;
        if (p.crops())
            notes << "Picture subtitles go on before the crop: any drawn in the cropped-off part are cut";
    }
    // Crop before scaling, so a size limit and anything burned in fit the
    // picture that's kept.
    int height = info.height;
    if (p.crops()) {
        const int w = info.width - p.cropLeft - p.cropRight;
        height = info.height - p.cropTop - p.cropBottom;
        chain << QString("crop=%1:%2:%3:%4").arg(w).arg(height).arg(p.cropLeft).arg(p.cropTop);
        notes << QString("Cropped to %1×%2").arg(w).arg(height);
    }
    // Padding sizes the frame itself, the size limit included; the picture
    // is scaled to fit inside it.
    const int width = info.width - (p.crops() ? p.cropLeft + p.cropRight : 0);
    const Padding pad = p.pads() ? padding(width, height, p.padAcross, p.padDown, p.maxHeight) : Padding();
    if (p.pads() && pad.isNull())
        notes << QString("Already %1:%2, so no bars are added").arg(p.padAcross).arg(p.padDown);
    if (!pad.isNull()) {
        const QString fit = pad.pictureWidth == width && pad.pictureHeight == height
                                ? QString()
                                : QString("scale=%1:%2:flags=lanczos").arg(pad.pictureWidth).arg(pad.pictureHeight);
        if (p.padFill == "blur") {
            // The bars are the picture itself, filling the frame, blurred
            // and a little darker; blurred at a quarter size, which is
            // quicker and looks the same.
            const int bw = std::max(2, pad.width / 4 & ~1), bh = std::max(2, pad.height / 4 & ~1);
            chain << QString("split[pbg][pfg];[pbg]scale=%1:%2:force_original_aspect_ratio=increase,crop=%1:%2,"
                             "boxblur=%3:2,scale=%4:%5,eq=brightness=-0.08[pbb];[pfg]%6[pff];"
                             "[pbb][pff]overlay=%7:%8:format=%9,setsar=1")
                         .arg(bw).arg(bh).arg(std::max(1, std::min(bw, bh) / 16)).arg(pad.width).arg(pad.height)
                         .arg(fit.isEmpty() ? QString("null") : fit).arg(pad.x).arg(pad.y).arg(overlayFormat(info));
        } else {
            chain << (fit.isEmpty() ? QString() : fit + ",")
                         + QString("pad=%1:%2:%3:%4:color=black,setsar=1").arg(pad.width).arg(pad.height).arg(pad.x).arg(pad.y);
        }
        const bool sides = pad.x > 0;
        notes << QString("Padded to %1:%2 (%3×%4): %5 %6")
                     .arg(p.padAcross).arg(p.padDown).arg(pad.width).arg(pad.height)
                     .arg(p.padFill == "blur" ? "blurred picture" : "black bars")
                     .arg(sides ? "at the sides" : "above and below");
        height = pad.height;
    } else if (p.maxHeight > 0 && height > p.maxHeight) {
        chain << QString("scale=-2:%1:flags=lanczos").arg(p.maxHeight);
        notes << QString("Scaled down to %1p").arg(p.maxHeight);
        height = p.maxHeight;
    }
    // The logo, sized against the finished picture. The chain is a line,
    // so the second picture joins it through a labelled detour.
    const QString logo = p.logoFile.isEmpty() ? p.logo : p.logoFile;
    if (!logo.isEmpty()) {
        const int size = std::max(8, int(std::lround(height * p.logoSize / 100.0)) & ~1);
        const int m = std::max(4, int(std::lround(height * 0.04)));
        static const QHash<QString, QString> places{
            {"tl", "x=%1:y=%1"}, {"tr", "x=W-w-%1:y=%1"}, {"bl", "x=%1:y=H-h-%1"},
            {"br", "x=W-w-%1:y=H-h-%1"}, {"c", "x=(W-w)/2:y=(H-h)/2"}};
        const QString place = places.value(p.logoPosition, places.value("tr")).arg(m);
        chain << QString("null[lv];movie=filename=%1,scale=-1:%2,format=rgba,colorchannelmixer=aa=%3[lg];"
                         "[lv][lg]overlay=%4:format=%5")
                     .arg(logo).arg(size).arg(p.logoOpacity / 100.0, 0, 'f', 2).arg(place, overlayFormat(info));
        notes << QString("Logo %1 at %2% of the height").arg(QFileInfo(p.logo.isEmpty() ? logo : p.logo).fileName())
                         .arg(p.logoSize);
    }
    if (p.burnsSubtitles() && !p.burnSubtitlePicture) {
        // After crop and scale, so the text is drawn sharp at the output
        // size. The filter reads the subtitles on the source's own clock,
        // while a trimmed input starts at zero: shift the picture onto
        // that clock for the filter, then back.
        QString f = "subtitles=filename=" + p.burnSubtitles;
        if (p.burnSubtitleTrack >= 0)
            f += QString(":si=%1").arg(p.burnSubtitleTrack);
        if (start > 0)
            f = QString("setpts=PTS+%1/TB,%2,setpts=PTS-STARTPTS").arg(start, 0, 'f', 3).arg(f);
        chain << f;
        notes << "Subtitles burned in from " + p.burnSubtitleName;
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
                         ":box=1:boxcolor=black@0.55:boxborderw=10:x=(w-tw)/2:y=%3")
                     .arg(escaped, rate, p.burnsSubtitles() ? QString("h/24") : QString("h-th-h/24"));    // subtitles take the bottom
        notes << QString("Timecode burned in from %1").arg(tc);
    }
    // Only plain characters reach drawtext; its escaping rules for quotes,
    // colons and percent signs are a trap not worth exposing.
    const QString mark = QString(p.watermark).remove(QRegularExpression("[^A-Za-z0-9 .!?&()#@+-]")).trimmed();
    if (!mark.isEmpty()) {
        // Top right, unless the logo is there: then top left.
        const bool logoTopRight = !logo.isEmpty() && (p.logoPosition == "tr" || p.logoPosition.isEmpty());
        chain << QString("drawtext=font=sans:text='%1':fontsize=h/12:fontcolor=white@0.35"
                         ":x=%2:y=h/24").arg(mark, logoTopRight ? QString("h/24") : QString("w-tw-h/24"));
        notes << QString("Watermark \"%1\"").arg(mark);
    }
    return chain;
}
}

Reference referencePicture(const Preset &p, const MediaInfo &info, const QString &input, double start, double length) {
    Reference r;
    r.inputs = inputArguments(p, info, input, start, length, true);
    r.inputCount = p.joinsByFilter() ? int(p.joinClips.size()) : 1;
    QStringList notes;
    const QStringList chain = videoChain(p, info, start, notes);
    r.graph = (p.joinsByFilter() ? joinGraph(p, info, true, false) + ";[jv]" : QString("[0:v:0]"))
              + (chain.isEmpty() ? QString("null") : chain.join(',')) + "[ref]";
    return r;
}

QString rewrapReason(const MediaInfo &info, const QString &path) {
    const QString extension = QFileInfo(path).suffix().toLower();
    if (!info.valid || !info.hasVideo || extension == "mp4" || extension == "m4v")
        return {};
    if (info.videoCodec != "h264" && info.videoCodec != "hevc")
        return {};
    QStringList audio;
    for (const AudioTrack &t : info.audio) {
        if (t.codec != "aac" && t.codec != "mp3")
            return {};
        if (!audio.contains(codecName(t.codec)))
            audio << codecName(t.codec);
    }
    Preset rewrap;
    for (const Preset &p : builtinPresets())
        if (p.id == "rewrap-mp4")
            rewrap = p;
    if (rewrap.id.isEmpty() || !sourceProblem(rewrap, info).isEmpty())
        return {};
    QString reason = QString("The %1 video%2 already fit%3 an MP4. Rewrapping moves %4 into one in seconds, "
                             "without re-encoding: the same picture and sound, nothing lost.")
                         .arg(codecName(info.videoCodec))
                         .arg(audio.isEmpty() ? QString() : " and " + audio.join(" and ") + " audio")
                         .arg(audio.isEmpty() ? "s" : "")
                         .arg(audio.isEmpty() ? "it" : "them");
    int pictures = 0;
    for (const SubtitleTrack &t : info.subtitles)
        pictures += t.isText() ? 0 : 1;
    if (pictures > 0)
        reason += " Picture subtitles can't go along: MP4 has no place for them.";
    return reason;
}

int outputHeight(const Preset &p, const MediaInfo &info) {
    if (!info.hasVideo || p.audioOnly())
        return 0;
    if (p.copiesVideo())
        return info.height;
    int height = info.height - (p.crops() ? p.cropTop + p.cropBottom : 0);
    if (p.pads()) {
        const Padding pad = padding(info.width - (p.crops() ? p.cropLeft + p.cropRight : 0), height,
                                    p.padAcross, p.padDown, p.maxHeight);
        if (!pad.isNull())
            return pad.height;
    }
    if (p.maxHeight > 0 && height > p.maxHeight)
        height = p.maxHeight;
    return height;
}

Padding padding(int width, int height, int across, int down, int maxHeight) {
    if (width <= 0 || height <= 0 || across <= 0 || down <= 0)
        return {};
    int side = std::min(width, height);
    if (maxHeight > 0 && side > maxHeight)
        side = maxHeight;
    side &= ~1;
    Padding pad;
    if (across >= down) {
        pad.height = side;
        pad.width = int(std::lround(double(side) * across / down)) & ~1;
    } else {
        pad.width = side;
        pad.height = int(std::lround(double(side) * down / across)) & ~1;
    }
    // Fit the picture inside, on even pixels.
    if (qint64(width) * pad.height > qint64(height) * pad.width) {
        pad.pictureWidth = pad.width;
        pad.pictureHeight = std::min(pad.height, int(std::lround(double(height) * pad.width / width)) & ~1);
    } else {
        pad.pictureHeight = pad.height;
        pad.pictureWidth = std::min(pad.width, int(std::lround(double(width) * pad.height / height)) & ~1);
    }
    // Bars of a pixel or two each side are rounding, not a different shape.
    if (pad.width - pad.pictureWidth <= 4 && pad.height - pad.pictureHeight <= 4)
        return {};
    pad.x = ((pad.width - pad.pictureWidth) / 2) & ~1;
    pad.y = ((pad.height - pad.pictureHeight) / 2) & ~1;
    return pad;
}

QStringList outputStillArguments(const Preset &p, const MediaInfo &info, const QString &input, double at,
                                 const QString &jpeg, int height) {
    if (!info.hasVideo || p.audioOnly() || p.copiesVideo())
        return {};
    // The input starts at the frame, as a trimmed run's does at its in point,
    // so burned-in subtitles and timecode read the same clock as the encode.
    at = std::max(0.0, at);
    // A picture subtitle is one packet when it appears; seeking straight to
    // the frame would skip the one on screen. Start a little earlier and
    // drop the frames before it after the filters.
    const double preroll = p.burnsSubtitles() && p.burnSubtitlePicture ? std::min(at, 5.0) : 0;
    const double from = at - preroll;
    QStringList notes;
    QStringList chain = videoChain(p, info, from, notes);
    chain << QString("scale=-2:'min(ih,%1)':flags=bicubic:out_range=full,format=yuvj420p").arg(height);
    QStringList a{"-nostdin", "-hide_banner", "-y", "-loglevel", "error"};
    a << colourFixArguments(info) << "-ss" << QString::number(from, 'f', 3) << "-i" << input
      << "-filter_complex" << "[0:v:0]" + chain.join(',') + "[o]" << "-map" << "[o]";
    if (preroll > 0)
        a << "-ss" << QString::number(preroll, 'f', 3);
    a << "-frames:v" << "1" << "-q:v" << "3" << "-update" << "1" << jpeg;
    return a;
}

Command buildCommand(const Preset &p, const MediaInfo &info, const QString &input,
                     const QString &output, double start, double length,
                     const QString &previewOut, const QString &previewSource,
                     const QList<Loudness> &loudness) {
    Command cmd;
    QStringList &a = cmd.arguments;
    a << "-nostdin" << "-hide_banner" << "-y" << "-loglevel" << "error"
      << "-nostats" << "-progress" << "pipe:1" << "-stats_period" << "0.5";

    const bool withVideo = info.hasVideo && !p.audioOnly();
    const bool hd = info.height >= 720;
    const QString defPrim = hd ? "bt709" : "smpte170m";
    // Copied streams pass through untouched: no filters, so no preview frames.
    const bool copyVideo = withVideo && p.copiesVideo();
    const bool copyAudio = p.copiesAudio();
    const QString container = containerFor(p, info);
    const bool previews = withVideo && !copyVideo && !previewOut.isEmpty() && !previewSource.isEmpty();

    if (withVideo && !colourFixArguments(info).isEmpty())
        cmd.notes << QString("Colour labels set to %1").arg(hd ? "Rec. 709" : "Rec. 601");

    const bool gpu = withVideo && onGpu(p);
    if (gpu && p.hardware == "vaapi")
        a << "-vaapi_device" << (p.hardwareDevice.isEmpty() ? QString("/dev/dri/renderD128") : p.hardwareDevice);

    a << inputArguments(p, info, input, start, length, withVideo);

    const bool withAudio = !info.audio.isEmpty() && p.audio != "none";
    QStringList graph;
    // Clips that differ come out of the join as one picture and one sound.
    const QString videoIn = p.joinsByFilter() ? "[jv]" : "[0:v:0]";
    if (p.joins()) {
        if (p.joinsByFilter()) {
            graph << joinGraph(p, info, withVideo, withAudio);
            cmd.notes << QString("%1 clips joined: they differ (%2), so each is fitted to the first")
                             .arg(p.joinCount).arg(p.joinDifference);
            int extraTracks = 0;
            for (const MediaInfo &c : p.joinClips)
                extraTracks = std::max<int>(extraTracks, c.audio.size() - 1);
            if (withAudio && extraTracks > 0)
                cmd.notes << "Only each clip's first audio track is used";
        } else {
            cmd.notes << QString("%1 clips joined; they match, so they're read as one file").arg(p.joinCount);
        }
    }
    if (withVideo && !copyVideo) {
        QStringList chain = videoChain(p, info, start, cmd.notes);
        chain << "format=" + (p.video == "ffv1" ? ffv1PixelFormat(info) : pixelFormatFor(p));
        // VA-API encodes from GPU memory: only the encoder's branch is
        // uploaded, so the preview frames stay ordinary pictures.
        const bool upload = gpu && p.hardware == "vaapi";
        if (previews) {
            graph << videoIn + "split=2[vin][sv]";
            graph << "[vin]" + chain.join(',') + QString(",split=2[%1][pv]").arg(upload ? "vsw" : "v");
            if (upload)
                graph << "[vsw]hwupload[v]";
            graph << QString("[pv]%1[pvo]").arg(kPreviewScale);
            graph << QString("[sv]%1[svo]").arg(kPreviewScale);
        } else {
            graph << videoIn + chain.join(',') + (upload ? ",hwupload" : "") + "[v]";
        }
    }

    QStringList audioMaps;
    if (withAudio) {
        if (info.splitMono() && !copyAudio)
            cmd.notes << "Mono tracks 1 and 2 joined into stereo (left, right)";
        else if (withVideo && info.audio.size() > 1)
            cmd.notes << QString("All %1 audio tracks kept").arg(info.audio.size());   // languages, commentary, mixes
        else if (!withVideo && info.audio.size() > 1)
            cmd.notes << QString("Only audio track 1 of %1 converted: a %2 file holds one track")
                             .arg(info.audio.size()).arg(container.toUpper());

        const LoudnessTarget target = loudnessTarget(p.loudness);
        const bool normalise = target.lufs < 0 && !copyAudio;
        if (normalise)
            cmd.notes << QString("Loudness set to %1 LUFS (%2), true peak %3 dBTP or lower")
                             .arg(target.lufs).arg(target.name).arg(target.truePeak);
        const QList<AudioStream> streams = audioStreams(p, info);
        for (int k = 0; k < streams.size(); ++k) {
            AudioStream s = streams[k];
            QStringList filters;
            if (p.joinsByFilter()) {
                s.inputs = "[ja]";
                s.filter.clear();
                filters << "anull";    // the join's output has to be taken through the graph
            }
            if (!s.filter.isEmpty())
                filters << s.filter;
            if (normalise) {
                const Loudness m = k < loudness.size() ? loudness[k] : Loudness();
                if (loudness.isEmpty() || m.valid()) {
                    filters << loudnormFilter(target, m);
                    // loudnorm works at 192 kHz; come back to the track's own rate.
                    filters << QString("aresample=%1").arg(s.sampleRate);
                    if (m.valid())
                        cmd.notes << QString("Audio %1 measured %2 LUFS, peak %3 dBTP")
                                         .arg(k + 1).arg(m.input, 0, 'f', 1).arg(m.truePeak, 0, 'f', 1);
                } else {
                    cmd.notes << QString("Audio %1 is silent, so its level was left alone").arg(k + 1);
                }
            }
            if (filters.isEmpty()) {
                audioMaps << s.inputs.mid(1, s.inputs.size() - 2);    // "[0:a:1]" → "0:a:1"
            } else {
                graph << s.inputs + filters.join(',') + QString("[a%1]").arg(k);
                audioMaps << QString("[a%1]").arg(k);
            }
        }
    }

    // Subtitles: text becomes the container's own kind; pictures (Blu-ray,
    // DVD) only fit MKV, which takes every kind as it is.
    QStringList subtitleMaps;
    QString subtitleCodec;
    if (withVideo && !info.subtitles.isEmpty()) {
        int pictures = 0;
        if (p.container == "mkv" || p.container == "source") {
            subtitleCodec = "copy";
        } else if (container == "mp4" || container == "mov" || container == "webm") {
            subtitleCodec = container == "webm" ? "webvtt" : "mov_text";
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
                             .arg(pictures).arg(pictures == 1 ? "" : "s").arg(container.toUpper())
                             .arg(pictures == 1 ? "it" : "them");
        else if (subtitleCodec.isEmpty())
            cmd.notes << QString("Subtitles left out: a %1 file has no place for them").arg(container.toUpper());
    }

    if (!graph.isEmpty())
        a << "-filter_complex" << graph.join(';');

    if (copyVideo) {
        a << "-map" << "0:V:0" << "-c:v" << "copy";
        cmd.notes << "Video copied as it is, not re-encoded";
    } else if (withVideo) {
        a << "-map" << "[v]";
        // Through a LUT the picture becomes Rec. 709, whatever the camera's was.
        const bool graded = !p.lut.isEmpty() || !p.lutFile.isEmpty();
        const QString prim = graded ? QString("bt709") : knownTag(info.colorPrimaries) ? info.colorPrimaries : defPrim;
        const QString trc = graded ? QString("bt709") : knownTag(info.colorTransfer) ? info.colorTransfer : defPrim;
        const QString space = graded ? QString("bt709") : knownTag(info.colorSpace) ? info.colorSpace : defPrim;
        if (gpu) {
            const bool hevc = p.video == "hevc";
            const QString encoder = (hevc ? "hevc_" : "h264_") + p.hardware;
            a << "-c:v" << encoder;
            // Constant quality like the CPU path: CRF maps onto each family's
            // own quality scale, offset so the files match x264/x265 on VMAF.
            // NVENC at cq = crf + 1 came out twice the size of x264/x265 at a
            // higher VMAF; at crf + 6 (RTX 3080, 1080p60) H.264 is 8% larger
            // and HEVC 10%, both slightly better. VA-API's constant QP at
            // qp = crf was 1.75-2.8x the size; these offsets match on Intel
            // Iris Xe (H.264 ~25% larger, HEVC the same size).
            // Constant QP can't keep under a cap: on Intel a capped preset
            // came out 1.6x the share copy's size, peaking at 3x the cap.
            // Intel's driver takes QVBR (the same quality, kept under the
            // cap: x264's size there); AMD's doesn't, so AMD stays on CQP.
            // -quality 1 (the driver's slowest, best mode) makes Intel
            // H.264 7% smaller for 12% more time; HEVC gains nothing.
            const bool intelVaapi = p.hardware == "vaapi" && p.hardwareVendor == "intel";
            const bool qvbr = intelVaapi && p.maxrateKbps > 0;
            const QString vaapiQ = QString::number(p.crf + (hevc ? 4 : 6));
            if (p.hardware == "nvenc")
                a << "-preset" << nvencPreset(p.speed) << "-tune" << "hq" << "-rc" << "vbr"
                  << "-cq" << QString::number(std::min(51, p.crf + 6)) << "-b:v" << "0";
            else if (p.hardware == "qsv")
                a << "-preset" << qsvPreset(p.speed) << "-global_quality" << QString::number(p.crf);
            else if (qvbr)
                a << "-rc_mode" << "QVBR" << "-global_quality" << vaapiQ;
            else
                a << "-rc_mode" << "CQP" << "-qp" << vaapiQ;
            if (intelVaapi && !hevc)
                a << "-quality" << "1";
            a << "-profile:v" << (hevc ? "main10" : "high");
            // VA-API in constant-QP mode has no rate cap to honour. Quick Sync
            // given a cap but no bitrate drops global_quality for a fixed QP
            // of its own and overshoots the cap; a target just under the cap
            // selects QVBR, which keeps both (a target equal to it means CBR).
            // Intel's VA-API QVBR wants the same target.
            if (p.maxrateKbps > 0 && (p.hardware != "vaapi" || qvbr)) {
                if (p.hardware == "qsv" || qvbr)
                    a << "-b:v" << QString("%1k").arg(p.maxrateKbps * 15 / 16);
                a << "-maxrate" << QString("%1k").arg(p.maxrateKbps) << "-bufsize" << QString("%1k").arg(p.maxrateKbps * 2);
            }
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
        } else if (p.video.startsWith("dnxhr")) {
            a << "-c:v" << "dnxhd" << "-profile:v" << p.video;
        } else if (p.video == "av1") {
            a << "-c:v" << "libsvtav1" << "-preset" << QString::number(svtPreset(p.speed))
              << "-crf" << QString::number(std::clamp(p.crf + 15, 1, 63));
            if (p.maxrateKbps > 0)
                a << "-maxrate" << QString("%1k").arg(p.maxrateKbps) << "-bufsize" << QString("%1k").arg(p.maxrateKbps * 2);
        } else if (p.video == "ffv1") {
            // Level 3 with slice checksums: damage can be found and kept to
            // one slice; all intra, so any frame can be cut or checked alone.
            a << "-c:v" << "ffv1" << "-level" << "3" << "-g" << "1" << "-slicecrc" << "1" << "-slices" << "16";
            // Full range stays full range: without the label, ffmpeg would
            // squeeze the values into limited range on the way in.
            if (info.fullRange())
                a << "-color_range" << "pc";
            cmd.notes << "Lossless (FFV1), " + ffv1PixelFormat(info);
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
        if (copyAudio)
            a << "-c:a" << "copy";
        else if (p.audio == "aac")
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
        if (!copyAudio && (p.audio == "opus" || (lossy && rate > 48000)))
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
    // A copy cut starts on the keyframe at or before the in point; times are
    // shifted to start at zero so players don't show a gap.
    if ((copyVideo || copyAudio) && start > 0) {
        a << "-avoid_negative_ts" << "make_zero";
        if (copyVideo)
            cmd.notes << "Cut without re-encoding: it starts on the keyframe at or before the in point";
    }
    const bool quicktime = container == "mp4" || container == "mov" || container == "m4a";
    if (quicktime && withVideo && !info.timecode.isEmpty()) {
        // A trimmed copy starts that much later on the source's clock.
        const QString tc = start > 0 ? offsetTimecode(info.timecode, qint64(std::llround(start * info.fps)),
                                                      int(std::lround(info.fps)))
                                     : info.timecode;
        a << "-timecode" << tc;
    }
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
