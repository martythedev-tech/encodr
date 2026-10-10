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
    readonly property bool lossyVideo: p.video === "h264" || p.video === "hevc" || p.video === "vp9"
    readonly property bool lossyAudio: p.audio === "aac" || p.audio === "opus" || p.audio === "mp3"
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
                        { value: "vp9", text: "VP9 — for the web" }, { value: "prores_proxy", text: "ProRes Proxy" },
                        { value: "prores_lt", text: "ProRes LT" }, { value: "prores_422", text: "ProRes 422" },
                        { value: "prores_hq", text: "ProRes 422 HQ" }, { value: "dnxhr_lb", text: "DNxHR LB — light proxy" },
                        { value: "dnxhr_sq", text: "DNxHR SQ" }, { value: "dnxhr_hq", text: "DNxHR HQ" },
                        { value: "dnxhr_hqx", text: "DNxHR HQX — 10-bit" }, { value: "none", text: "No video (audio only)" }] }
                }
                Field {
                    label: "Quality"
                    visible: root.lossyVideo
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
                            color: theme.dim
                            font.pixelSize: 12
                            wrapMode: Text.Wrap
                            Layout.fillWidth: true
                        }
                    }
                }
                Field {
                    label: "Bitrate cap"
                    visible: p.video === "h264" || p.video === "hevc"
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
                Field {
                    label: "Encoder effort"
                    visible: p.video === "h264" || p.video === "hevc"
                    Choice { key: "speed"; options: [
                        { value: "veryfast", text: "Very fast — bigger files" }, { value: "faster", text: "Faster" },
                        { value: "fast", text: "Fast (recommended)" }, { value: "medium", text: "Medium" },
                        { value: "slow", text: "Slow — slightly smaller files" }, { value: "slower", text: "Slower" },
                        { value: "veryslow", text: "Very slow — smallest" }] }
                }
                Field {
                    label: "Runs on"
                    visible: (p.video === "h264" || p.video === "hevc") && backend.gpuEnabled
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
                                        return "No GPU encoder works on this computer, so everything runs on the CPU.";
                                    var names = [];
                                    for (var i = 0; i < hw.encoders.length; i++)
                                        names.push(hw.encoders[i].name + " (" + hw.encoders[i].codecs.join(", ").toUpperCase() + ")");
                                    return "Found " + names.join(", ") + ". Much faster, at the same quality as the CPU, but files come out larger (often about twice the size); "
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
