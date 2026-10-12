import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "Format.js" as Format

ScrollView {
    id: root
    property var job: null
    readonly property var info: job ? job.info : ({})
    signal seekRequested(real seconds)
    readonly property bool tip: job !== null && job.suggestion.presetId !== undefined
                                && job.state !== "running" && job.state !== "paused" && job.state !== "done"
    property string savedEdl: ""
    property string savedReport: ""
    onJobChanged: { savedEdl = ""; savedReport = ""; }

    function kindColor(kind) {
        return kind === "black" ? theme.dim : kind === "freeze" ? theme.bad : kind === "silence" ? theme.warn : theme.foreground;
    }
    function kindName(kind) {
        return kind === "black" ? "Black" : kind === "freeze" ? "Frozen picture" : kind === "silence" ? "Silence" : "Cut";
    }
    clip: true
    contentWidth: availableWidth
    // Nothing scrolls sideways; a hidden horizontal bar still took clicks
    // along the bottom edge.
    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

    component Row2: RowLayout {
        property string label
        property var value
        property color valueColor: theme.foreground
        Layout.fillWidth: true
        spacing: 12
        Text { text: parent.label; color: theme.dim; font.pixelSize: 12; Layout.preferredWidth: 104 }
        Text { text: parent.value || "—"; color: parent.valueColor; font.pixelSize: 12; Layout.fillWidth: true; wrapMode: Text.Wrap; font.features: { "tnum": 1 } }
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
            spacing: 6
        }
    }

    ColumnLayout {
        x: 12
        width: root.availableWidth - 24
        spacing: 16

        Item { implicitHeight: 0 }

        Text {
            visible: job !== null && job.state === "probing"
            text: "Reading the file…"
            color: theme.dim
            font.pixelSize: 14
        }

        // After converting: how much the picture lost, and whether audio
        // that should be bit-exact is.
        Section {
            title: "Quality"
            visible: job !== null && job.state === "done"
            Text {
                visible: job !== null && job.qualityState === ""
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: theme.dim
                font.pixelSize: 12
                text: "Measure the copy against the source run through the same processing, so only what the encoder lost "
                      + "counts: VMAF for the picture, and a sample-by-sample check of audio that should match exactly."
            }
            RowLayout {
                visible: job !== null && job.qualityState === "checking"
                Layout.fillWidth: true
                spacing: 10
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4
                    Text { text: job ? job.qualityStep : ""; color: theme.foreground; font.pixelSize: 12 }
                    Meter { Layout.fillWidth: true; implicitHeight: 4; value: job ? job.qualityPercent : 0; barColor: theme.accent }
                }
                UiButton { text: "Stop"; kind: "ghost"; onClicked: job.stopQualityCheck() }
            }
            Text {
                visible: job !== null && job.qualityState === "failed"
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: theme.bad
                font.pixelSize: 12
                text: job ? job.qualityStep : ""
            }
            UiButton {
                visible: job !== null && (job.qualityState === "" || job.qualityState === "failed")
                text: job && job.qualityState === "failed" ? "Try again" : "Check quality"
                onClicked: job.checkQuality()
            }

            // The results.
            ColumnLayout {
                id: results
                visible: job !== null && job.qualityState === "done"
                Layout.fillWidth: true
                spacing: 8
                readonly property var q: job ? job.quality : ({})
                RowLayout {
                    visible: results.q.measured === true
                    spacing: 18
                    Repeater {
                        model: [["Average", "mean"], ["Worst 1%", "low"], ["Lowest", "min"]]
                        ColumnLayout {
                            required property var modelData
                            Layout.alignment: Qt.AlignTop
                            spacing: 0
                            Text { text: modelData[0].toUpperCase(); color: theme.dim; font.pixelSize: 10; font.weight: Font.DemiBold; font.letterSpacing: 0.6 }
                            Text {
                                text: job && job.quality[modelData[1]] !== undefined ? Format.number(job.quality[modelData[1]], 1) : "—"
                                color: theme.foreground
                                font.pixelSize: modelData[1] === "mean" ? 22 : 16
                                font.weight: Font.Medium
                                font.features: { "tnum": 1 }
                            }
                        }
                    }
                    Text {
                        Layout.alignment: Qt.AlignBottom
                        Layout.bottomMargin: 3
                        text: results.q.metric + (results.q.subsample > 1 ? " · every " + results.q.subsample + "th frame" : "")
                        color: theme.dim
                        font.pixelSize: 11
                    }
                }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    color: theme.foreground
                    font.pixelSize: 12
                    text: results.q.measured === true ? results.q.verdict : (results.q.videoNote || "")
                }
                Graph {
                    visible: results.q.measured === true
                    Layout.fillWidth: true
                    implicitHeight: 84
                    title: results.q.metric ? results.q.metric + " across the file" : ""
                    values: results.q.curve || []
                    // From a little under the lowest point, so the dips show.
                    floor: 100
                    base: Math.max(0, Math.floor(((results.q.min !== undefined ? results.q.min : 0) - 5) / 10) * 10)
                    maxLabel: "100"
                    current: results.q.measured === true ? "from " + Math.round(base) : ""
                }
                Text {
                    visible: (results.q.worst || []).length > 0
                    text: "Weakest seconds (click to go there; pick Compare above the player to look closely)"
                    color: theme.dim
                    font.pixelSize: 11
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                }
                Repeater {
                    model: results.q.worst || []
                    RowLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        spacing: 12
                        Text {
                            text: Format.precise(modelData.time)
                            color: theme.accent
                            font.pixelSize: 12
                            font.features: { "tnum": 1 }
                            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.seekRequested(modelData.time) }
                        }
                        Text { text: Format.number(modelData.score, 1); color: theme.foreground; font.pixelSize: 12; font.features: { "tnum": 1 } }
                    }
                }
                Repeater {
                    model: results.q.audio || []
                    Row2 {
                        required property var modelData
                        label: "Audio " + (modelData.track + 1)
                        value: modelData.text
                        valueColor: modelData.state === "exact" ? theme.good : modelData.state === "differs" ? theme.bad : theme.dim
                    }
                }
                RowLayout {
                    spacing: 8
                    UiButton {
                        text: "Save report"
                        onClicked: root.savedReport = job.saveQualityReport()
                    }
                    UiButton { text: "Check again"; kind: "ghost"; onClicked: job.checkQuality() }
                }
                Text {
                    visible: root.savedReport !== ""
                    Layout.fillWidth: true
                    elide: Text.ElideMiddle
                    text: "Saved " + root.savedReport
                    color: theme.dim
                    font.pixelSize: 11
                }
            }
        }

        // The clips the queue's Join put together, in the order they play.
        Section {
            title: "Joined clips"
            visible: job !== null && job.clipCount > 1
            Text {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: theme.dim
                font.pixelSize: 12
                text: !job || job.clipCount < 2 ? ""
                      : job.joinDifference === ""
                        ? job.clipCount + " clips, " + Format.duration(job.totalDuration) + " in all. They match, so they're put back to back as they are."
                        : job.clipCount + " clips, " + Format.duration(job.totalDuration) + " in all. They differ (" + job.joinDifference
                          + "), so each is fitted to the first: its picture size and frame rate, and its first audio track. Subtitles are left out."
            }
            Repeater {
                model: job ? job.clips : []
                RowLayout {
                    required property var modelData
                    required property int index
                    Layout.fillWidth: true
                    spacing: 10
                    Text { text: (index + 1) + "."; color: theme.dim; font.pixelSize: 12; font.features: { "tnum": 1 }; Layout.preferredWidth: 18 }
                    Text { text: modelData.name; color: theme.foreground; font.pixelSize: 12; elide: Text.ElideMiddle; Layout.fillWidth: true }
                    Text { text: Format.duration(modelData.duration); color: theme.dim; font.pixelSize: 12; font.features: { "tnum": 1 } }
                }
            }
            Text {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: theme.dim
                font.pixelSize: 11
                text: "The player shows the first clip. Trim and subtitle burn-in are for single files; split to use them."
            }
            UiButton {
                Layout.topMargin: 2
                text: "Split"
                kind: "ghost"
                enabled: job !== null && job.state !== "running" && job.state !== "paused"
                onClicked: backend.splitJob(backend.selectedIndex)
            }
        }

        Section {
            title: "What Encodr found"
            visible: job !== null && (job.issues.length > 0 || root.tip)
            // A better preset for this file, with a switch to it; not once
            // the conversion is under way or done.
            RowLayout {
                visible: root.tip
                Layout.fillWidth: true
                spacing: 10
                Rectangle {
                    Layout.alignment: Qt.AlignTop
                    Layout.topMargin: 2
                    implicitWidth: tipBadge.implicitWidth + 12
                    implicitHeight: 18
                    radius: 3
                    color: "transparent"
                    border.width: 1
                    border.color: theme.accent
                    Text {
                        id: tipBadge
                        anchors.centerIn: parent
                        text: "TIP"
                        color: theme.accent
                        font.pixelSize: 10
                        font.weight: Font.DemiBold
                        font.letterSpacing: 0.5
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 1
                    Text { text: root.tip ? job.suggestion.title : ""; color: theme.foreground; font.pixelSize: 13; font.weight: Font.Medium }
                    Text { text: root.tip ? job.suggestion.detail : ""; color: theme.dim; font.pixelSize: 12; wrapMode: Text.Wrap; Layout.fillWidth: true }
                    UiButton {
                        Layout.topMargin: 5
                        text: root.tip ? job.suggestion.action : ""
                        onClicked: backend.setJobPreset(backend.selectedIndex, job.suggestion.presetId)
                    }
                }
            }
            Repeater {
                model: job ? job.issues : []
                RowLayout {
                    required property var modelData
                    Layout.fillWidth: true
                    spacing: 10
                    Rectangle {
                        Layout.alignment: Qt.AlignTop
                        Layout.topMargin: 2
                        implicitWidth: badge.implicitWidth + 12
                        implicitHeight: 18
                        radius: 3
                        color: "transparent"
                        border.width: 1
                        border.color: modelData.level === "fix" ? theme.good : modelData.level === "warn" ? theme.warn : theme.border
                        Text {
                            id: badge
                            anchors.centerIn: parent
                            text: modelData.level === "fix" ? "FIXED" : modelData.level === "warn" ? "NOTE" : "INFO"
                            color: modelData.level === "fix" ? theme.good : modelData.level === "warn" ? theme.warn : theme.dim
                            font.pixelSize: 10
                            font.weight: Font.DemiBold
                            font.letterSpacing: 0.5
                        }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 1
                        Text { text: modelData.title; color: theme.foreground; font.pixelSize: 13; font.weight: Font.Medium }
                        Text { text: modelData.detail; color: theme.dim; font.pixelSize: 12; wrapMode: Text.Wrap; Layout.fillWidth: true }
                    }
                }
            }
        }

        // A whole-file check, on request: it decodes every frame.
        Section {
            title: "Check the whole file"
            visible: info.valid === true
            RowLayout {
                visible: job !== null && !job.analysing && !job.analysed
                Layout.fillWidth: true
                spacing: 10
                UiButton { text: "Check for black, freezes, silence and cuts"; onClicked: job.analyse() }
            }
            Text {
                visible: job !== null && !job.analysing && !job.analysed
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: "Reads the whole file once, on a small copy of the picture."
                color: theme.dim
                font.pixelSize: 11
            }
            RowLayout {
                visible: job !== null && job.analysing
                spacing: 10
                Text { text: job ? "Checking… " + Format.number(job.analysisPercent, 0) + "%" : ""; color: theme.foreground; font.pixelSize: 12 }
                UiButton { kind: "ghost"; text: "Stop"; implicitHeight: 22; onClicked: job.stopAnalysis() }
            }
            Repeater {
                // The problems; cuts are counted below.
                model: job && job.analysed ? job.moments.filter(m => m.kind !== "cut").slice(0, 50) : []
                RowLayout {
                    required property var modelData
                    Layout.fillWidth: true
                    spacing: 8
                    Rectangle { implicitWidth: 8; implicitHeight: 8; radius: 2; color: root.kindColor(modelData.kind) }
                    Text {
                        Layout.fillWidth: true
                        text: root.kindName(modelData.kind) + "  ·  " + Format.precise(modelData.start) + " – "
                              + Format.precise(modelData.end) + "  (" + Format.number(modelData.end - modelData.start, 1) + " s)"
                        color: hover.containsMouse ? theme.accent : theme.foreground
                        font.pixelSize: 12
                        font.features: { "tnum": 1 }
                        MouseArea { id: hover; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: root.seekRequested(modelData.start) }
                    }
                }
            }
            Text {
                visible: job !== null && job.analysed && job.moments.filter(m => m.kind !== "cut").length > 50
                text: "…and more; the first 50 are listed."
                color: theme.dim
                font.pixelSize: 11
            }
            RowLayout {
                visible: job !== null && job.analysed
                Layout.fillWidth: true
                spacing: 10
                Text { text: job ? job.cutCount + " cut" + (job.cutCount === 1 ? "" : "s") + " between shots" : ""; color: theme.foreground; font.pixelSize: 12 }
                UiButton {
                    visible: job !== null && job.cutCount > 0
                    text: "Save cuts as EDL"
                    implicitHeight: 24
                    onClicked: root.savedEdl = job.saveEdl() || "failed"
                }
                Item { Layout.fillWidth: true }
                UiButton { kind: "ghost"; text: "Check again"; implicitHeight: 22; onClicked: { root.savedEdl = ""; job.analyse(); } }
            }
            Text {
                visible: root.savedEdl !== ""
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: root.savedEdl === "failed" ? "Couldn't write the EDL to the output folder." : "Saved " + Format.folder(root.savedEdl)
                color: root.savedEdl === "failed" ? theme.bad : theme.dim
                font.pixelSize: 11
            }
        }

        Section {
            title: "File"
            visible: info.valid === true
            Row2 { label: "Name"; value: job ? job.fileName : "" }
            Row2 { label: "Folder"; value: job ? Format.folder(job.inputPath.substring(0, job.inputPath.lastIndexOf("/"))) : "" }
            Row2 { label: "Format"; value: info.container }
            Row2 { label: "Size"; value: Format.bytes(info.size) }
            Row2 { label: "Running time"; value: Format.duration(info.duration) + "  (" + Format.words(info.duration) + ")" }
            Row2 { label: "Overall bitrate"; value: Format.bps(info.bitrate) }
            Row2 { label: "Start timecode"; value: info.timecode || "none" }
            Row2 { label: "Recorded"; value: info.creationTime ? new Date(info.creationTime).toLocaleString(Qt.locale(), "ddd d MMM yyyy, h:mm AP") : "" }
        }

        Section {
            title: "Video"
            visible: info.hasVideo === true
            Row2 { label: "Codec"; value: (info.videoCodecLong || info.videoCodec || "") + (info.videoProfile ? " — " + info.videoProfile : "") }
            Row2 { label: "Picture"; value: info.width + " × " + info.height }
            Row2 { label: "Frame rate"; value: info.fpsText + " fps" + (info.frames ? "  ·  about " + Format.thousands(info.frames) + " frames" : "") }
            Row2 { label: "Scan"; value: info.interlaced ? "Interlaced (" + info.fieldOrder + ")" : "Progressive"; valueColor: info.interlaced ? theme.warn : theme.foreground }
            Row2 { label: "Pixels"; value: (info.pixelFormat || "") + (info.bitDepth ? "  ·  " + info.bitDepth + "-bit" : "") }
            Row2 {
                label: "Colour"
                readonly property bool blank: [info.colorPrimaries, info.colorTransfer, info.colorSpace].indexOf("reserved") >= 0
                value: blank ? "blank (\"reserved\") — fixed automatically"
                             : [info.colorPrimaries, info.colorTransfer, info.colorSpace].filter(function (v) { return v; }).join(" / ") || "not labelled"
                valueColor: blank ? theme.warn : theme.foreground
            }
            Row2 { label: "Video bitrate"; value: Format.bps(info.videoBitrate) }
        }

        Section {
            title: "Audio" + (job && !job.levelsScanned && info.audio && info.audio.length ? "  ·  measuring levels…" : "")
            visible: info.audio !== undefined && info.audio.length > 0
            Repeater {
                model: info.audio || []
                ColumnLayout {
                    required property var modelData
                    Layout.fillWidth: true
                    spacing: 4
                    Text {
                        text: "Track " + (modelData.index + 1) + ":  "
                              + (modelData.language && modelData.language !== "und" ? modelData.language + " · " : "")
                              + modelData.codec + " · " + (modelData.sampleRate / 1000) + " kHz"
                              + (modelData.bits ? " · " + modelData.bits + "-bit" : "") + " · "
                              + (modelData.channels === 1 ? "mono" : modelData.channels === 2 ? "stereo" : modelData.channels + " channels")
                              + (modelData.bitrate ? " · " + Format.bps(modelData.bitrate) : "")
                        color: theme.foreground
                        font.pixelSize: 12
                    }
                    // Level meter: average loudness and loudest peak, -60 to 0 dBFS.
                    RowLayout {
                        visible: modelData.peakDb !== undefined && modelData.peakDb !== null
                        Layout.fillWidth: true
                        spacing: 10
                        Rectangle {
                            Layout.fillWidth: true
                            implicitHeight: 6
                            radius: 3
                            color: theme.raised
                            Rectangle {
                                height: parent.height
                                radius: 3
                                width: parent.width * Math.max(0, Math.min(1, (modelData.meanDb + 60) / 60))
                                color: theme.good
                            }
                            Rectangle {
                                x: parent.width * Math.max(0, Math.min(1, (modelData.peakDb + 60) / 60)) - 2
                                width: 3
                                height: parent.height
                                color: modelData.peakDb >= -0.1 ? theme.bad : theme.warn
                            }
                        }
                        Text {
                            text: "avg " + Format.number(modelData.meanDb, 1) + " dB · peak " + Format.number(modelData.peakDb, 1) + " dB"
                            color: theme.dim
                            font.pixelSize: 11
                            font.family: theme.mono
                        }
                    }
                }
            }
            Text {
                visible: job !== null && job.levelsScanned
                text: "Levels measured over 30 seconds in the middle of the file."
                color: theme.dim
                font.pixelSize: 11
            }
        }

        Section {
            title: "Subtitles"
            visible: info.subtitles !== undefined && info.subtitles.length > 0
            Repeater {
                model: info.subtitles || []
                Text {
                    required property var modelData
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    text: "Track " + (modelData.index + 1) + ":  "
                          + (modelData.language && modelData.language !== "und" ? modelData.language + " · " : "")
                          + modelData.codec + (modelData.text ? " (text)" : " (picture)")
                          + (modelData.title ? " · " + modelData.title : "")
                    color: theme.foreground
                    font.pixelSize: 12
                }
            }
        }

        Section {
            title: "Full technical report (mediainfo)"
            visible: job !== null && job.mediainfoReport !== ""
            UiButton {
                text: reportText.visible ? "Hide" : "Show"
                onClicked: reportText.visible = !reportText.visible
            }
            TextEdit {
                id: reportText
                visible: false
                Layout.fillWidth: true
                text: job ? job.mediainfoReport : ""
                readOnly: true
                selectByMouse: true
                wrapMode: TextEdit.Wrap
                color: theme.dim
                font.family: theme.mono
                font.pixelSize: 11
            }
        }

        Text {
            visible: job !== null && job.state === "failed" && info.valid !== true
            text: job ? job.message : ""
            color: theme.bad
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }
        Item { implicitHeight: 6 }
    }
}
