import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "Format.js" as Format

ScrollView {
    id: root
    property var job: null
    property int jobIndex: -1
    readonly property var p: job ? job.preset : ({})
    readonly property bool locked: job === null || job.state === "running" || job.state === "paused" || job.state === "probing"
    readonly property bool lossyVideo: p.video === "h264" || p.video === "hevc" || p.video === "av1" || p.video === "vp9"
    readonly property bool lossyAudio: p.audio === "aac" || p.audio === "opus" || p.audio === "mp3"
    readonly property bool canFit: p.video === "h264" || p.video === "hevc" || p.video === "av1"
    readonly property bool fits: canFit && (p.targetSizeMB || 0) > 0
    clip: true
    contentWidth: availableWidth

    function engineName(id) {
        var found = backend.hardware ? backend.hardware.encoders : [];
        for (var i = 0; i < found.length; i++)
            if (found[i].id === id)
                return found[i].name + " (GPU)";
        return id;
    }

    function edit(key, value) {
        backend.editJobPreset(jobIndex, key, value);
    }

    component Field: RowLayout {
        property string label
        default property alias control: holder.data
        Layout.fillWidth: true
        spacing: 12
        Text { text: parent.label; color: theme.dim; font.pixelSize: 12; Layout.preferredWidth: 104; Layout.alignment: Qt.AlignTop; Layout.topMargin: 7; wrapMode: Text.Wrap }
        Item {
            id: holder
            Layout.fillWidth: true
            implicitHeight: childrenRect.height
        }
    }

    component Choice: ComboBox {
        property string key
        implicitHeight: 32
        topInset: 0
        bottomInset: 0
        font.pixelSize: 12
        property var options: []     // [{value, text}]
        width: parent ? Math.min(340, parent.width) : 300
        model: options
        textRole: "text"
        valueRole: "value"
        enabled: !root.locked
        currentIndex: indexOfValue(root.p[key])
        onActivated: root.edit(key, currentValue)
    }

    component Section: Rectangle {
        default property alias content: inner.data
        property string title
        Layout.fillWidth: true
        // Flat inside its panel: a small-caps title over the rows.
        color: "transparent"
        Layout.alignment: Qt.AlignTop
        implicitHeight: inner.implicitHeight + 26
        Text { x: 0; y: 0; text: parent.title.toUpperCase(); color: theme.dim; font.pixelSize: 10; font.weight: Font.DemiBold; font.letterSpacing: 0.8 }
        ColumnLayout {
            id: inner
            x: 0
            y: 22
            width: parent.width
            spacing: 8
        }
    }

    ColumnLayout {
        x: 12
        width: root.availableWidth - 24
        spacing: 16

        Item { implicitHeight: 0 }

        Text {
            visible: root.locked && job !== null && job.state !== "probing"
            text: "Settings are locked while this file converts."
            color: theme.warn
            font.pixelSize: 13
        }

        Section {
            title: "Preset"
            ComboBox {
                id: presetBox
                implicitHeight: 32
                topInset: 0
                bottomInset: 0
                font.pixelSize: 12
                Layout.fillWidth: true
                Layout.maximumWidth: 520
                enabled: !root.locked
                model: backend.presets
                textRole: "name"
                valueRole: "id"
                currentIndex: indexOfValue(root.p.id)
                displayText: root.p.name || ""
                onActivated: backend.setJobPreset(root.jobIndex, currentValue)
            }
            Text { text: root.p.description || ""; color: theme.dim; font.pixelSize: 12; wrapMode: Text.Wrap; Layout.fillWidth: true }
            Text {
                visible: job !== null && job.presetProblem !== ""
                text: job ? job.presetProblem : ""
                color: theme.bad
                font.pixelSize: 13
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
        }

        GridLayout {
            Layout.fillWidth: true
            columns: root.availableWidth > 760 ? 2 : 1
            columnSpacing: 28
            rowSpacing: 16

            Section {
                title: "Video"
                Field {
                    label: "File type"
                    Choice { key: "container"; options: [
                        { value: "source", text: "Same as the source" + (job ? " (." + job.inputPath.split(".").pop().toLowerCase() + ")" : "") },
                        { value: "mp4", text: "MP4 (.mp4)" }, { value: "mov", text: "QuickTime (.mov)" },
                        { value: "mkv", text: "Matroska (.mkv)" }, { value: "webm", text: "WebM (.webm)" },
                        { value: "wav", text: "WAV audio (.wav)" }, { value: "flac", text: "FLAC audio (.flac)" },
                        { value: "m4a", text: "AAC/ALAC audio (.m4a)" }] }
                }
                Field {
                    label: "Video codec"
                    Choice { key: "video"; options: [
                        { value: "copy", text: "Copy — no re-encoding" },
                        { value: "h264", text: "H.264 — plays everywhere" }, { value: "hevc", text: "HEVC (H.265) — half the size" },
                        { value: "av1", text: "AV1 — smallest, newer players" },
                        { value: "vp9", text: "VP9 — for the web" }, { value: "prores_proxy", text: "ProRes Proxy" },
                        { value: "prores_lt", text: "ProRes LT" }, { value: "prores_422", text: "ProRes 422" },
                        { value: "prores_hq", text: "ProRes 422 HQ" }, { value: "dnxhr_lb", text: "DNxHR LB — light proxy" },
                        { value: "dnxhr_sq", text: "DNxHR SQ" }, { value: "dnxhr_hq", text: "DNxHR HQ" },
                        { value: "dnxhr_hqx", text: "DNxHR HQX — 10-bit" }, { value: "ffv1", text: "FFV1 — lossless archive (MKV)" },
                        { value: "none", text: "No video (audio only)" }] }
                }
                Field {
                    label: "Quality"
                    visible: root.lossyVideo && !root.fits
                    ColumnLayout {
                        width: Math.min(340, parent.width)
                        spacing: 0
                        Slider {
                            id: crfSlider
                            Layout.fillWidth: true
                            from: 10; to: 35; stepSize: 1
                            enabled: !root.locked
                            value: root.p.crf || 18
                            onMoved: root.edit("crf", Math.round(value))
                        }
                        Text {
                            text: "CRF " + Math.round(crfSlider.value) + (crfSlider.value <= 18 ? " · visually lossless" : crfSlider.value <= 23 ? " · very good" : crfSlider.value <= 28 ? " · good, small" : " · rough, tiny") + "  (lower = better, bigger)"
                                  + (root.p.video === "av1" ? " · SVT-AV1 CRF " + Math.min(63, Math.round(crfSlider.value) + 15) : "")
                            color: theme.dim
                            font.pixelSize: 12
                            wrapMode: Text.Wrap
                            Layout.fillWidth: true
                        }
                    }
                }
                Field {
                    label: "Bitrate cap"
                    visible: root.canFit && !root.fits
                    RowLayout {
                        width: parent.width
                        SpinBox {
                            implicitHeight: 32
                            topInset: 0
                            bottomInset: 0
                            font.pixelSize: 12
                            from: 0; to: 200; editable: true
                            enabled: !root.locked
                            value: Math.round((root.p.maxrateKbps || 0) / 1000)
                            onValueModified: root.edit("maxrateKbps", value * 1000)
                        }
                        Text { text: "Mbps · 0 means no cap"; color: theme.dim; font.pixelSize: 12; wrapMode: Text.Wrap; Layout.fillWidth: true }
                    }
                }
                // A size instead of a quality: the bitrate comes from the length.
                Field {
                    label: "Fit a size"
                    visible: root.canFit
                    RowLayout {
                        width: parent.width
                        SpinBox {
                            implicitHeight: 32
                            topInset: 0
                            bottomInset: 0
                            font.pixelSize: 12
                            from: 0; to: 100000; editable: true
                            enabled: !root.locked
                            value: root.p.targetSizeMB || 0
                            onValueModified: root.edit("targetSizeMB", value)
                        }
                        Text {
                            text: {
                                if (!root.fits)
                                    return "MB · 0 means the quality decides";
                                var kbps = job ? job.fitKbps : 0;
                                var rate = kbps >= 1000 ? (kbps / 1000).toFixed(1) + " Mbps" : kbps + " kbps";
                                if (job && job.fitAtSourceRate)
                                    return "MB · the source is smaller already: it keeps its own " + rate;
                                return "MB with the audio" + (kbps > 0 ? " · " + rate + " for the picture" : "")
                                       + (root.p.video === "av1" ? ", one pass" : ", two passes on the CPU");
                            }
                            color: theme.dim; font.pixelSize: 12; wrapMode: Text.Wrap; Layout.fillWidth: true
                        }
                    }
                }
                Field {
                    label: "Encoder effort"
                    visible: p.video === "h264" || p.video === "hevc" || p.video === "av1"
                    Choice { key: "speed"; options: [
                        { value: "veryfast", text: "Very fast — bigger files" }, { value: "faster", text: "Faster" },
                        { value: "fast", text: "Fast (recommended)" }, { value: "medium", text: "Medium" },
                        { value: "slow", text: "Slow — slightly smaller files" }, { value: "slower", text: "Slower" },
                        { value: "veryslow", text: "Very slow — smallest" }] }
                }
                Field {
                    label: "Runs on"
                    visible: (p.video === "h264" || p.video === "hevc") && backend.gpuEnabled && !root.fits
                    ColumnLayout {
                        width: Math.min(340, parent.width)
                        spacing: 4
                        Choice {
                            key: "hardware"
                            Layout.fillWidth: true
                            options: {
                                var list = [{ value: "auto", text: "Automatic — fastest available" }, { value: "cpu", text: "CPU (x264 / x265)" }];
                                var found = backend.hardware ? backend.hardware.encoders : [];
                                for (var i = 0; i < found.length; i++)
                                    if (found[i].codecs.indexOf(root.p.video) >= 0)
                                        list.push({ value: found[i].id, text: found[i].name + " (GPU)" });
                                return list;
                            }
                        }
                        Text {
                            text: job ? "This file: " + (job.engine === "cpu" ? "CPU" : root.engineName(job.engine)) : ""
                            color: theme.dim
                            font.pixelSize: 11
                        }
                    }
                }
                Field {
                    label: "Picture size"
                    visible: p.video !== "none" && p.video !== "copy"
                    Choice { key: "maxHeight"; options: [
                        { value: 0, text: "Same as the source" + (job && job.info.height ? " (" + job.info.height + "p)" : "") },
                        { value: 2160, text: "Up to 2160p (4K)" }, { value: 1440, text: "Up to 1440p" },
                        { value: 1080, text: "Up to 1080p" }, { value: 720, text: "Up to 720p" },
                        { value: 540, text: "Up to 540p" }, { value: 480, text: "Up to 480p" }] }
                }
                // Crop belongs to the file, not the preset: it lives on the job.
                Field {
                    label: "Crop"
                    visible: p.video !== "none"
                    ColumnLayout {
                        width: Math.min(340, parent.width)
                        spacing: 6
                        ComboBox {
                            id: cropChoice
                            Layout.fillWidth: true
                            implicitHeight: 32
                            topInset: 0
                            bottomInset: 0
                            font.pixelSize: 12
                            enabled: !root.locked && job !== null
                            textRole: "text"
                            valueRole: "value"
                            model: [
                                { value: "off", text: "None" },
                                { value: "bars", text: "Remove black bars (detected)" },
                                { value: "16:9", text: "Widescreen 16:9 (centred)" },
                                { value: "1:1", text: "Square 1:1 (centred)" },
                                { value: "4:5", text: "Portrait 4:5 (centred)" },
                                { value: "9:16", text: "Vertical 9:16 (centred)" },
                                { value: "custom", text: "Custom" }]
                            currentIndex: job ? indexOfValue(job.cropMode) : 0
                            onActivated: job.setCropMode(currentValue)
                        }
                        Text {
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            color: theme.dim
                            font.pixelSize: 11
                            visible: text !== ""
                            text: {
                                if (!job || job.cropMode === "off")
                                    return "";
                                if (job.detectingBars)
                                    return "Looking for black bars…";
                                const w = job.info.width - job.cropLeft - job.cropRight;
                                const h = job.info.height - job.cropTop - job.cropBottom;
                                if (job.cropMode === "bars" && w === job.info.width && h === job.info.height)
                                    return "No black bars found: the picture is kept whole.";
                                var parts = [];
                                if (job.cropTop || job.cropBottom)
                                    parts.push(job.cropTop === job.cropBottom ? job.cropTop + " px top and bottom"
                                                                               : job.cropTop + " px top, " + job.cropBottom + " px bottom");
                                if (job.cropLeft || job.cropRight)
                                    parts.push(job.cropLeft === job.cropRight ? job.cropLeft + " px each side"
                                                                               : job.cropLeft + " px left, " + job.cropRight + " px right");
                                return (parts.length ? "Removes " + parts.join(" and ") + " → " : "") + w + "×" + h;
                            }
                        }
                        GridLayout {
                            visible: job !== null && job.cropMode === "custom"
                            columns: 4
                            columnSpacing: 8
                            rowSpacing: 6
                            Repeater {
                                model: [["Top", "cropTop"], ["Bottom", "cropBottom"], ["Left", "cropLeft"], ["Right", "cropRight"]]
                                RowLayout {
                                    required property var modelData
                                    spacing: 4
                                    Text { text: modelData[0]; color: theme.dim; font.pixelSize: 11 }
                                    SpinBox {
                                        implicitHeight: 28
                                        implicitWidth: 72
                                        topInset: 0
                                        bottomInset: 0
                                        font.pixelSize: 11
                                        from: 0; to: 4000; stepSize: 2; editable: true
                                        enabled: !root.locked
                                        value: job ? job[modelData[1]] : 0
                                        onValueModified: {
                                            const m = { cropLeft: job.cropLeft, cropTop: job.cropTop, cropRight: job.cropRight, cropBottom: job.cropBottom };
                                            m[modelData[1]] = value;
                                            job.setCropMargins(m.cropLeft, m.cropTop, m.cropRight, m.cropBottom);
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                // Bars to reach an aspect: the file's, like the crop.
                Field {
                    label: "Bars"
                    visible: p.video !== "none"
                    ColumnLayout {
                        width: Math.min(340, parent.width)
                        spacing: 6
                        ComboBox {
                            Layout.fillWidth: true
                            implicitHeight: 32
                            topInset: 0
                            bottomInset: 0
                            font.pixelSize: 12
                            enabled: !root.locked && job !== null
                            textRole: "text"
                            valueRole: "value"
                            model: [
                                { value: "off", text: "None" },
                                { value: "16:9", text: "Widescreen 16:9" },
                                { value: "4:3", text: "Classic 4:3" },
                                { value: "1:1", text: "Square 1:1" },
                                { value: "4:5", text: "Portrait 4:5" },
                                { value: "9:16", text: "Vertical 9:16" }]
                            currentIndex: job ? indexOfValue(job.padMode) : 0
                            onActivated: job.setPadMode(currentValue)
                        }
                        Segmented {
                            visible: job !== null && job.padMode !== "off"
                            implicitHeight: 24
                            enabled: !root.locked
                            options: [{ id: "black", text: "Black" }, { id: "blur", text: "Blurred picture" }]
                            current: job ? job.padFill : "black"
                            onPicked: function (id) { job.setPadFill(id); }
                        }
                        Text {
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            color: theme.dim
                            font.pixelSize: 11
                            visible: text !== ""
                            text: {
                                if (!job || job.padMode === "off")
                                    return "";
                                const note = job.notes.find(n => n.startsWith("Padded to") || n.startsWith("Already "));
                                return note ? note.replace(/^Padded to [0-9:]+ /, "Makes a frame ") : "";
                            }
                        }
                    }
                }
                Field {
                    label: "LUT"
                    visible: p.video !== "none"
                    RowLayout {
                        width: Math.min(340, parent.width)
                        spacing: 8
                        Text {
                            Layout.fillWidth: true
                            elide: Text.ElideMiddle
                            text: root.p.lut ? root.p.lut.split("/").pop() : "None (for log footage: a .cube file)"
                            color: root.p.lut ? theme.foreground : theme.dim
                            font.pixelSize: 12
                        }
                        UiButton { text: "Choose…"; implicitHeight: 26; enabled: !root.locked; onClicked: backend.chooseFile(root.jobIndex, "lut") }
                        UiButton { kind: "ghost"; text: "Remove"; implicitHeight: 26; visible: !!root.p.lut; enabled: !root.locked; onClicked: root.edit("lut", "") }
                    }
                }
                Field {
                    label: "Deinterlace"
                    visible: p.video !== "none" && p.video !== "copy"
                    RowLayout {
                        width: parent.width
                        Switch {
                            enabled: !root.locked
                            checked: root.p.deinterlace === true
                            onToggled: root.edit("deinterlace", checked)
                        }
                        Text {
                            text: job && job.info.interlaced ? "This source is interlaced" : "Only applies to interlaced sources"
                            color: theme.dim; font.pixelSize: 12; wrapMode: Text.Wrap; Layout.fillWidth: true
                        }
                    }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                spacing: 16
                Section {
                    title: "Review extras"
                    visible: p.video !== "none" && p.video !== "copy"
                    Field {
                        label: "Burn in timecode"
                        RowLayout {
                            width: parent.width
                            Switch {
                                enabled: !root.locked
                                checked: root.p.burnTimecode === true
                                onToggled: root.edit("burnTimecode", checked)
                            }
                            Text {
                                text: job && job.info.timecode ? "Starts at " + job.info.timecode : "Counts from 00:00:00:00 (no timecode in the source)"
                                color: theme.dim; font.pixelSize: 12; wrapMode: Text.Wrap; Layout.fillWidth: true
                            }
                        }
                    }
                    // Per file: a text track in the source, or a subtitle file.
                    Field {
                        label: "Burn in subtitles"
                        ColumnLayout {
                            width: Math.min(340, parent.width)
                            spacing: 4
                            ComboBox {
                                id: subsChoice
                                Layout.fillWidth: true
                                implicitHeight: 32
                                topInset: 0
                                bottomInset: 0
                                font.pixelSize: 12
                                enabled: !root.locked && job !== null && job.clipCount < 2
                                textRole: "text"
                                valueRole: "value"
                                model: job ? job.subtitleChoices : []
                                currentIndex: job ? Math.max(0, indexOfValue(job.subtitleBurn)) : 0
                                onActivated: {
                                    if (currentValue === "pick") {
                                        backend.chooseFile(root.jobIndex, "subtitles");
                                        currentIndex = Qt.binding(() => job ? Math.max(0, indexOfValue(job.subtitleBurn)) : 0);
                                    } else {
                                        job.setSubtitleBurn(currentValue);
                                    }
                                }
                            }
                            Text {
                                visible: job !== null && job.subtitleBurn !== "off" && root.p.burnTimecode === true
                                Layout.fillWidth: true
                                wrapMode: Text.Wrap
                                text: "The timecode moves to the top, out of the subtitles' way."
                                color: theme.dim
                                font.pixelSize: 11
                            }
                        }
                    }
                    Field {
                        label: "Logo"
                        ColumnLayout {
                            width: Math.min(340, parent.width)
                            spacing: 6
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 8
                                Text {
                                    Layout.fillWidth: true
                                    elide: Text.ElideMiddle
                                    text: root.p.logo ? root.p.logo.split("/").pop() : "None (PNG, JPEG or WebP)"
                                    color: root.p.logo ? theme.foreground : theme.dim
                                    font.pixelSize: 12
                                }
                                UiButton { text: "Choose…"; implicitHeight: 26; enabled: !root.locked; onClicked: backend.chooseFile(root.jobIndex, "image") }
                                UiButton { kind: "ghost"; text: "Remove"; implicitHeight: 26; visible: !!root.p.logo; enabled: !root.locked; onClicked: root.edit("logo", "") }
                            }
                            Segmented {
                                visible: !!root.p.logo
                                implicitHeight: 24
                                enabled: !root.locked
                                options: [{ id: "tl", text: "↖" }, { id: "tr", text: "↗" }, { id: "c", text: "Centre" },
                                          { id: "bl", text: "↙" }, { id: "br", text: "↘" }]
                                current: root.p.logoPosition || "tr"
                                onPicked: function (id) { root.edit("logoPosition", id); }
                            }
                            RowLayout {
                                visible: !!root.p.logo
                                Layout.fillWidth: true
                                spacing: 8
                                Text { text: "Size"; color: theme.dim; font.pixelSize: 11; Layout.preferredWidth: 48 }
                                Slider {
                                    id: logoSizeSlider
                                    Layout.fillWidth: true
                                    from: 4; to: 30; stepSize: 1
                                    enabled: !root.locked
                                    value: root.p.logoSize || 10
                                    onPressedChanged: if (!pressed) root.edit("logoSize", Math.round(value))
                                }
                                Text { text: Math.round(logoSizeSlider.value) + "%"; color: theme.dim; font.pixelSize: 11; Layout.preferredWidth: 32 }
                            }
                            RowLayout {
                                visible: !!root.p.logo
                                Layout.fillWidth: true
                                spacing: 8
                                Text { text: "Opacity"; color: theme.dim; font.pixelSize: 11; Layout.preferredWidth: 48 }
                                Slider {
                                    id: logoOpacitySlider
                                    Layout.fillWidth: true
                                    from: 10; to: 100; stepSize: 5
                                    enabled: !root.locked
                                    value: root.p.logoOpacity || 80
                                    onPressedChanged: if (!pressed) root.edit("logoOpacity", Math.round(value))
                                }
                                Text { text: Math.round(logoOpacitySlider.value) + "%"; color: theme.dim; font.pixelSize: 11; Layout.preferredWidth: 32 }
                            }
                        }
                    }
                    Field {
                        label: "Watermark"
                        TextField {
                            implicitHeight: 32
                            topInset: 0
                            bottomInset: 0
                            topPadding: 0
                            bottomPadding: 0
                            font.pixelSize: 12
                            width: Math.min(340, parent.width)
                            enabled: !root.locked
                            placeholderText: "e.g. DRAFT — letters, numbers and spaces"
                            text: root.p.watermark || ""
                            onEditingFinished: root.edit("watermark", text)
                        }
                    }
                }

                Section {
                    title: "Audio"
                    Field {
                        label: "Audio codec"
                        Choice { key: "audio"; options: [
                            { value: "copy", text: "Copy — no re-encoding" },
                            { value: "aac", text: "AAC — plays everywhere" }, { value: "opus", text: "Opus — best at low bitrates" },
                            { value: "mp3", text: "MP3" }, { value: "alac", text: "ALAC — lossless (Apple)" },
                            { value: "flac", text: "FLAC — lossless" }, { value: "pcm24", text: "Uncompressed 24-bit" },
                            { value: "pcm16", text: "Uncompressed 16-bit" }, { value: "none", text: "No audio" }] }
                    }
                    Field {
                        label: "Audio bitrate"
                        visible: root.lossyAudio
                        Choice { key: "audioKbps"; options: [
                            { value: 128, text: "128 kbps" }, { value: 160, text: "160 kbps" }, { value: 192, text: "192 kbps" },
                            { value: 256, text: "256 kbps" }, { value: 320, text: "320 kbps — highest" }] }
                    }
                    Field {
                        label: "Loudness"
                        visible: p.audio !== "none" && p.audio !== "copy"
                        ColumnLayout {
                            width: Math.min(340, parent.width)
                            spacing: 4
                            Choice { key: "loudness"; Layout.fillWidth: true; options: [
                                { value: "off", text: "As recorded" },
                                { value: "ebu", text: "Broadcast, EBU R128 — −23 LUFS" },
                                { value: "atsc", text: "US broadcast, ATSC A/85 — −24 LUFS" },
                                { value: "web", text: "Podcasts and web — −16 LUFS" },
                                { value: "stream", text: "Streaming (YouTube, Spotify) — −14 LUFS" }] }
                            Text {
                                visible: root.p.loudness !== undefined && root.p.loudness !== "off"
                                Layout.fillWidth: true
                                wrapMode: Text.Wrap
                                text: "Measured first, then turned up or down evenly: the dynamics stay as they are."
                                color: theme.dim
                                font.pixelSize: 11
                            }
                        }
                    }
                }

                Section {
                    title: "Output"
                    Text {
                        text: job ? Format.folder(job.outputPath) : ""
                        color: theme.foreground
                        font.pixelSize: 13
                        elide: Text.ElideMiddle
                        Layout.fillWidth: true
                    }
                    Text {
                        text: backend.outputMode === "folder"
                              ? "Everything goes to the folder you chose."
                              : "Saved next to each original. Read-only sources (camera cards, Mac drives) go to ~/Videos/Encodr instead."
                        color: theme.dim
                        font.pixelSize: 12
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                    }
                    RowLayout {
                        UiButton { text: "Choose folder…"; onClicked: backend.chooseOutputFolder() }
                        UiButton { text: "Next to the originals"; kind: "ghost"; visible: backend.outputMode === "folder"; onClicked: backend.useSourceFolders() }
                    }
                    Field {
                        label: "File name"
                        ColumnLayout {
                            width: parent.width
                            spacing: 4
                            ComboBox {
                                id: nameBox
                                Layout.fillWidth: true
                                Layout.maximumWidth: 340
                                implicitHeight: 32
                                topInset: 0
                                bottomInset: 0
                                font.pixelSize: 12
                                editable: true
                                // For every conversion, not just this file.
                                model: ["{name}{suffix}", "{date}_{name}{suffix}", "{name}{suffix}_{n}",
                                        "{name}_{height}p{suffix}", "{name}_{preset}_{date}"]
                                function sync() { editText = backend.namePattern; }
                                Component.onCompleted: sync()
                                Connections { target: backend; function onSettingsChanged() { nameBox.sync(); } }
                                onActivated: function (index) { backend.namePattern = textAt(index); }
                                Connections {
                                    target: nameBox.contentItem
                                    function onEditingFinished() {
                                        backend.namePattern = nameBox.editText;
                                        nameBox.sync();    // shows the pattern as cleaned up
                                    }
                                }
                            }
                            Text {
                                Layout.fillWidth: true
                                wrapMode: Text.Wrap
                                color: theme.dim
                                font.pixelSize: 11
                                text: "Every file is named this way. Parts: {name} the original's name, {suffix} the preset's (_review), "
                                      + "{preset} its id, {date} " + Qt.formatDateTime(new Date(), "yyyy-MM-dd") + ", {time} "
                                      + Qt.formatDateTime(new Date(), "HHmm") + ", {height} 1080, {n} 001, 002… "
                                      + "(the first number free). {name} is always in it. Without {n}, a name "
                                      + "that's taken gets \" (2)\"."
                            }
                        }
                    }
                }

                // Settings for this machine, not for the file: they apply to every job.
                Section {
                    title: "This computer"
                    Field {
                        label: "Files at once"
                        ComboBox {
                            width: Math.min(340, parent.width)
                            implicitHeight: 32
                            topInset: 0
                            bottomInset: 0
                            font.pixelSize: 12
                            textRole: "text"
                            valueRole: "value"
                            model: [
                                { value: 0, text: "Automatic (" + backend.autoParallel + " on this computer)" },
                                { value: 1, text: "1 — one after another" }, { value: 2, text: "2" },
                                { value: 3, text: "3" }, { value: 4, text: "4" }]
                            currentIndex: indexOfValue(backend.parallel)
                            onActivated: backend.parallel = currentValue
                        }
                    }
                    Field {
                        label: "Quality check"
                        ColumnLayout {
                            width: parent.width
                            spacing: 2
                            Switch {
                                checked: backend.checkAfter
                                onToggled: backend.checkAfter = checked
                            }
                            Text {
                                Layout.fillWidth: true
                                wrapMode: Text.Wrap
                                color: theme.dim
                                font.pixelSize: 11
                                text: "Measure each file as soon as it's converted (VMAF, and bit-exact audio where it should be). "
                                      + "It reads the source and the copy again, so it takes about as long as a fast conversion; "
                                      + "off, the check is a button on each finished file."
                            }
                        }
                    }
                    Field {
                        label: "GPU encoding"
                        ColumnLayout {
                            width: parent.width
                            spacing: 2
                            RowLayout {
                                Switch {
                                    checked: backend.gpuEnabled
                                    onToggled: backend.gpuEnabled = checked
                                }
                                Text {
                                    text: "Experimental"
                                    color: theme.warn
                                    font.pixelSize: 11
                                    font.weight: Font.DemiBold
                                }
                            }
                            Text {
                                Layout.fillWidth: true
                                wrapMode: Text.Wrap
                                color: theme.dim
                                font.pixelSize: 11
                                text: {
                                    var hw = backend.hardware;
                                    if (!hw || !hw.ready)
                                        return "Looking for GPU encoders…";
                                    if (hw.encoders.length === 0)
                                        return hw.problems.length > 0 ? hw.problems.join(" ") + " Until then, everything runs on the CPU."
                                                                      : "No GPU encoder works on this computer, so everything runs on the CPU.";
                                    var names = [];
                                    for (var i = 0; i < hw.encoders.length; i++)
                                        names.push(hw.encoders[i].name + " (" + hw.encoders[i].codecs.join(", ").toUpperCase() + ")");
                                    return "Found " + names.join(", ") + ". Much faster, at about the same quality as the CPU, though H.264 files can come out up to a quarter larger; "
                                           + "if the GPU refuses a file, Encodr redoes it on the CPU.";
                                }
                            }
                        }
                    }
                }

            }
        }

        Section {
            title: "Keep these settings"
            RowLayout {
                Layout.fillWidth: true
                TextField {
                    id: presetName
                    implicitHeight: 32
                    topInset: 0
                    bottomInset: 0
                    topPadding: 0
                    bottomPadding: 0
                    font.pixelSize: 12
                    Layout.fillWidth: true
                    Layout.maximumWidth: 360
                    placeholderText: "Preset name"
                    text: root.p.builtin ? "" : (root.p.name || "").replace(" (edited)", "")
                }
                UiButton {
                    text: "Save as preset"
                    enabled: presetName.text.trim() !== "" && job !== null
                    onClicked: backend.savePreset(root.jobIndex, presetName.text)
                }
            }
            RowLayout {
                UiButton { text: "Use for all waiting files"; kind: "ghost"; onClicked: backend.applyToWaiting(root.jobIndex) }
                UiButton {
                    text: "Delete this preset"
                    kind: "ghost"
                    danger: true
                    visible: root.p.id !== undefined && String(root.p.id).indexOf("user-") === 0
                    onClicked: backend.deletePreset(root.p.id)
                }
            }
        }
        Section {
            title: "Command"
            visible: job !== null && job.commandLine !== ""
            Text {
                text: job ? job.preset.name + "  →  " + Format.folder(job.outputPath) : ""
                color: theme.foreground
                font.pixelSize: 12
                elide: Text.ElideMiddle
                Layout.fillWidth: true
            }
            Repeater {
                model: job ? job.notes : []
                Text {
                    required property string modelData
                    text: "✓  " + modelData
                    color: theme.good
                    font.pixelSize: 12
                }
            }
            Rectangle {
                Layout.fillWidth: true
                color: theme.background
                radius: 4
                border.width: 1
                border.color: theme.border
                implicitHeight: cmd.implicitHeight + 16
                TextEdit {
                    id: cmd
                    anchors.fill: parent
                    anchors.margins: 8
                    text: job ? job.commandLine : ""
                    readOnly: true
                    selectByMouse: true
                    wrapMode: TextEdit.WrapAnywhere
                    color: theme.dim
                    font.family: theme.mono
                    font.pixelSize: 11
                }
            }
            UiButton { text: "Copy ffmpeg command"; onClicked: backend.copyText(job.commandLine) }
        }
        Item { implicitHeight: 6 }
    }
}
