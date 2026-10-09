import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "Format.js" as Format

ScrollView {
    id: root
    property var job: null
    readonly property var info: job ? job.info : ({})
    clip: true
    contentWidth: availableWidth

    component Row2: RowLayout {
        property string label
        property string value
        property color valueColor: theme.foreground
        Layout.fillWidth: true
        spacing: 12
        Text { text: parent.label; color: theme.dim; font.pixelSize: 13; Layout.preferredWidth: 140 }
        Text { text: parent.value || "—"; color: parent.valueColor; font.pixelSize: 13; Layout.fillWidth: true; wrapMode: Text.Wrap }
    }

    component Section: Rectangle {
        default property alias content: inner.data
        property string title
        Layout.fillWidth: true
        color: theme.surface
        radius: 10
        implicitHeight: inner.implicitHeight + 46
        Text { x: 14; y: 12; text: parent.title.toUpperCase(); color: theme.dim; font.pixelSize: 11; font.bold: true; font.letterSpacing: 1 }
        ColumnLayout {
            id: inner
            x: 14
            y: 34
            width: parent.width - 28
            spacing: 6
        }
    }

    ColumnLayout {
        width: root.availableWidth
        spacing: 14

        Text {
            visible: job !== null && job.state === "probing"
            text: "Reading the file…"
            color: theme.dim
            font.pixelSize: 14
        }

        Section {
            title: "What Encodr found"
            visible: job !== null && job.issues.length > 0
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
                        implicitHeight: 20
                        radius: 4
                        color: modelData.level === "fix" ? theme.good : modelData.level === "warn" ? theme.warn : theme.raised
                        Text {
                            id: badge
                            anchors.centerIn: parent
                            text: modelData.level === "fix" ? "FIXED" : modelData.level === "warn" ? "NOTE" : "INFO"
                            color: modelData.level === "info" ? theme.foreground : theme.background
                            font.pixelSize: 10
                            font.bold: true
                        }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 1
                        Text { text: modelData.title; color: theme.foreground; font.pixelSize: 13; font.bold: true }
                        Text { text: modelData.detail; color: theme.dim; font.pixelSize: 12; wrapMode: Text.Wrap; Layout.fillWidth: true }
                    }
                }
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
                        text: "Track " + (modelData.index + 1) + ":  " + modelData.codec + " · " + (modelData.sampleRate / 1000) + " kHz"
                              + (modelData.bits ? " · " + modelData.bits + "-bit" : "") + " · "
                              + (modelData.channels === 1 ? "mono" : modelData.channels === 2 ? "stereo" : modelData.channels + " channels")
                              + (modelData.bitrate ? " · " + Format.bps(modelData.bitrate) : "")
                        color: theme.foreground
                        font.pixelSize: 13
                    }
                    // Level meter: average loudness and loudest peak, -60 to 0 dBFS.
                    RowLayout {
                        visible: modelData.peakDb !== undefined && modelData.peakDb !== null
                        Layout.fillWidth: true
                        spacing: 10
                        Rectangle {
                            Layout.fillWidth: true
                            implicitHeight: 10
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
                            font.pixelSize: 12
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
            title: "Full technical report (mediainfo)"
            visible: job !== null && job.mediainfoReport !== ""
            Button {
                text: reportText.visible ? "Hide" : "Show"
                flat: true
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
                font.family: "monospace"
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
