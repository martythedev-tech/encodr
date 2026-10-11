#include "mediainfo.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace {
double rational(const QString &text) {
    const auto parts = text.split('/');
    if (parts.size() == 2) {
        const double den = parts[1].toDouble();
        return den > 0 ? parts[0].toDouble() / den : 0;
    }
    return text.toDouble();
}

bool brokenTag(const QString &value) {
    return value == "reserved" || value == "reserved0";
}

QString fpsText(double fps) {
    if (fps <= 0)
        return {};
    const double rounded = std::round(fps);
    return std::abs(fps - rounded) < 0.005 ? QString::number(int(rounded)) : QString::number(fps, 'f', 2);
}
}

bool MediaInfo::interlaced() const {
    return hasVideo && !fieldOrder.isEmpty() && fieldOrder != "progressive" && fieldOrder != "unknown";
}

bool MediaInfo::colourTagsBroken() const {
    return hasVideo && (brokenTag(colorPrimaries) || brokenTag(colorTransfer) || brokenTag(colorSpace));
}

bool MediaInfo::splitMono() const {
    if (audio.size() < 2)
        return false;
    for (const auto &track : audio)
        if (track.channels != 1)
            return false;
    return true;
}

bool SubtitleTrack::isText() const {
    static const QStringList text{"subrip", "srt", "ass", "ssa", "webvtt", "mov_text", "text"};
    return text.contains(codec);
}

qint64 MediaInfo::estimatedFrames() const {
    return hasVideo && fps > 0 ? qint64(std::llround(duration * fps)) : 0;
}

QString MediaInfo::summary() const {
    QStringList parts;
    if (hasVideo)
        parts << QString("%1 %2×%3 %4%5").arg(videoCodec.toUpper()).arg(width).arg(height)
                     .arg(fpsText(fps)).arg(interlaced() ? "i" : "p");
    if (!audio.isEmpty())
        parts << (splitMono() ? QString("%1 mono tracks").arg(audio.size())
                              : QString("%1 audio track%2").arg(audio.size()).arg(audio.size() == 1 ? "" : "s"));
    if (!subtitles.isEmpty())
        parts << QString("%1 subtitle track%2").arg(subtitles.size()).arg(subtitles.size() == 1 ? "" : "s");
    return parts.join(" · ");
}

QVariantMap MediaInfo::toVariant() const {
    QVariantList tracks;
    for (const auto &t : audio) {
        tracks.append(QVariantMap{
            {"index", t.index}, {"codec", t.codec}, {"sampleRate", t.sampleRate},
            {"channels", t.channels}, {"layout", t.layout}, {"bits", t.bits}, {"language", t.language},
            {"bitrate", t.bitrate},
            {"meanDb", std::isnan(t.meanDb) ? QVariant() : QVariant(t.meanDb)},
            {"peakDb", std::isnan(t.peakDb) ? QVariant() : QVariant(t.peakDb)},
        });
    }
    QVariantList subs;
    for (const auto &t : subtitles)
        subs.append(QVariantMap{{"index", t.index}, {"codec", t.codec}, {"language", t.language},
                                {"title", t.title}, {"text", t.isText()}});
    return {
        {"subtitles", subs},
        {"valid", valid}, {"error", error}, {"path", path}, {"container", container},
        {"size", size}, {"duration", duration}, {"bitrate", bitrate}, {"timecode", timecode},
        {"creationTime", creationTime}, {"hasVideo", hasVideo}, {"videoCodec", videoCodec},
        {"videoCodecLong", videoCodecLong}, {"videoProfile", videoProfile},
        {"pixelFormat", pixelFormat}, {"width", width}, {"height", height}, {"fps", fps},
        {"fpsText", fpsText(fps)}, {"fieldOrder", fieldOrder}, {"interlaced", interlaced()},
        {"bitDepth", bitDepth}, {"videoBitrate", videoBitrate},
        {"colorPrimaries", colorPrimaries}, {"colorTransfer", colorTransfer},
        {"colorSpace", colorSpace}, {"frames", estimatedFrames()}, {"audio", tracks},
        {"summary", summary()},
    };
}

MediaInfo parseProbe(const QByteArray &json, const QString &path) {
    MediaInfo info;
    info.path = path;

    QJsonParseError parseError;
    const auto doc = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        info.error = "ffprobe did not return readable information about this file.";
        return info;
    }

    const auto format = doc.object().value("format").toObject();
    info.container = format.value("format_long_name").toString(format.value("format_name").toString());
    info.size = format.value("size").toString().toLongLong();
    if (info.size == 0)
        info.size = QFileInfo(path).size();
    info.duration = format.value("duration").toString().toDouble();
    info.bitrate = format.value("bit_rate").toString().toLongLong();
    const auto formatTags = format.value("tags").toObject();
    info.timecode = formatTags.value("timecode").toString();
    info.creationTime = formatTags.value("creation_time").toString();

    int audioIndex = 0, subtitleIndex = 0;
    for (const auto &value : doc.object().value("streams").toArray()) {
        const auto s = value.toObject();
        const auto type = s.value("codec_type").toString();
        const auto tags = s.value("tags").toObject();
        if (info.timecode.isEmpty())
            info.timecode = tags.value("timecode").toString();

        // Cover art and thumbnails arrive as single-frame video streams.
        const bool attached = s.value("disposition").toObject().value("attached_pic").toInt() == 1;
        if (type == "video" && !info.hasVideo && !attached) {
            info.hasVideo = true;
            info.videoCodec = s.value("codec_name").toString();
            info.videoCodecLong = s.value("codec_long_name").toString();
            info.videoProfile = s.value("profile").toString();
            info.pixelFormat = s.value("pix_fmt").toString();
            info.width = s.value("width").toInt();
            info.height = s.value("height").toInt();
            info.fpsRational = s.value("avg_frame_rate").toString();
            info.fps = rational(info.fpsRational);
            if (info.fps <= 0) {
                info.fpsRational = s.value("r_frame_rate").toString();
                info.fps = rational(info.fpsRational);
            }
            info.fieldOrder = s.value("field_order").toString();
            info.bitDepth = s.value("bits_per_raw_sample").toString().toInt();
            info.videoBitrate = s.value("bit_rate").toString().toLongLong();
            info.colorPrimaries = s.value("color_primaries").toString();
            info.colorTransfer = s.value("color_transfer").toString();
            info.colorSpace = s.value("color_space").toString();
            if (info.duration <= 0)
                info.duration = s.value("duration").toString().toDouble();
        } else if (type == "audio") {
            AudioTrack t;
            t.index = audioIndex++;
            t.codec = s.value("codec_name").toString();
            t.sampleRate = s.value("sample_rate").toString().toInt();
            t.channels = s.value("channels").toInt();
            t.layout = s.value("channel_layout").toString();
            t.language = tags.value("language").toString();
            t.bits = s.value("bits_per_raw_sample").toString().toInt();
            if (t.bits == 0)
                t.bits = s.value("bits_per_sample").toInt();
            t.bitrate = s.value("bit_rate").toString().toLongLong();
            info.audio.append(t);
        } else if (type == "subtitle") {
            SubtitleTrack t;
            t.index = subtitleIndex++;
            t.codec = s.value("codec_name").toString();
            t.language = tags.value("language").toString();
            t.title = tags.value("title").toString();
            t.width = s.value("width").toInt();
            t.height = s.value("height").toInt();
            info.subtitles.append(t);
        }
    }

    if (!info.hasVideo && info.audio.isEmpty()) {
        info.error = "No video or audio streams were found in this file.";
        return info;
    }
    info.valid = true;
    return info;
}

QList<Issue> inspect(const MediaInfo &info) {
    QList<Issue> issues;
    if (!info.valid)
        return issues;

    if (info.colourTagsBroken()) {
        issues.append({"fix", "Colour information is blank",
                       QString("The recorder left the colour labels unset (\"reserved\"), which stops ffmpeg "
                               "converting the picture. Encodr labels it as %1, the standard for %2.")
                           .arg(info.height >= 720 ? "Rec. 709" : "Rec. 601")
                           .arg(info.height >= 720 ? "HD video" : "SD video")});
    }
    if (info.interlaced()) {
        issues.append({"fix", "Interlaced video",
                       "This video is interlaced, which shows comb lines on computer screens. "
                       "Encodr deinterlaces it (bwdif) unless the preset turns that off."});
    }
    if (info.splitMono()) {
        QString detail = "The audio is split across separate mono tracks. Encodr joins track 1 (left) "
                         "and track 2 (right) into one stereo track.";
        if (info.audio.size() > 2)
            detail += QString(" Tracks 3–%1 are not included.").arg(info.audio.size());
        issues.append({"fix", QString("%1 separate mono audio tracks").arg(info.audio.size()), detail});
    } else if (info.audio.size() > 1) {
        issues.append({"info", QString("%1 audio tracks").arg(info.audio.size()),
                       "Video copies keep every track. Audio-only files (WAV, FLAC, M4A) hold one, so they take track 1."});
    }
    int pictureSubs = 0;
    for (const SubtitleTrack &t : info.subtitles)
        pictureSubs += t.isText() ? 0 : 1;
    if (pictureSubs > 0)
        issues.append({"info", QString("%1 picture subtitle track%2").arg(pictureSubs).arg(pictureSubs == 1 ? "" : "s"),
                       "Blu-ray and DVD subtitles are pictures, which MP4 and MOV can't hold. They're kept in MKV "
                       "copies and can be burned into any copy; text subtitles are kept in every video copy."});
    if (info.hasVideo && info.audio.isEmpty())
        issues.append({"info", "No audio", "This file has no audio tracks."});

    for (const auto &t : info.audio) {
        if (std::isnan(t.peakDb))
            continue;
        if (t.meanDb < -70) {
            issues.append({"warn", QString("Track %1 is silent").arg(t.index + 1),
                           QString("Average level %1 dB in the sampled section.").arg(t.meanDb, 0, 'f', 1)});
        } else if (t.peakDb >= -0.05) {
            issues.append({"info", QString("Track %1 peaks at full scale").arg(t.index + 1),
                           "The loudest moments touch 0 dBFS, so the recording may clip briefly. "
                           "Encodr keeps the level as recorded."});
        }
    }

    return issues;
}

QVariantList issuesToVariant(const QList<Issue> &issues) {
    QVariantList list;
    for (const auto &i : issues)
        list.append(QVariantMap{{"level", i.level}, {"title", i.title}, {"detail", i.detail}});
    return list;
}

namespace {
const QStringList kAudioExtensions = {"wav", "aif", "aiff", "flac", "mp3", "m4a"};
const QStringList kVideoExtensions = {"mov", "mp4", "m4v", "mkv", "mxf", "avi", "mts", "m2ts", "ts", "mpg", "mpeg", "webm", "dv"};
QString extensionOf(const QString &path) {
    const qsizetype dot = path.lastIndexOf('.');
    return dot < 0 ? QString() : path.mid(dot + 1).toLower();
}
}

bool isMediaFile(const QString &path) {
    const QString e = extensionOf(path);
    return kVideoExtensions.contains(e) || kAudioExtensions.contains(e);
}

bool isAudioFile(const QString &path) {
    return kAudioExtensions.contains(extensionOf(path));
}
