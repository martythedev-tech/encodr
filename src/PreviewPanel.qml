import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "Format.js" as Format

// What's being converted right now: the processed picture, the source, or
// both side by side. Before a run it shows a poster frame from the source.
Rectangle {
    id: root
    property var job: null
    property string mode: "output"      // output, source, both
    readonly property bool live: job !== null && job.hasLivePreview
    readonly property bool running: job !== null && job.state === "running"
    readonly property real frameHeight: Math.min(440, (mode === "both" ? (width - 38) / 2 : width - 28) * 9 / 16)

    color: theme.surface
    radius: 6
    border.width: 1
    border.color: theme.border
    implicitHeight: body.implicitHeight + 28
    visible: job !== null && job.info.hasVideo === true

    function cycle() {
        mode = mode === "output" ? "source" : mode === "source" ? "both" : "output";
    }

    ColumnLayout {
        id: body
        anchors.fill: parent
        anchors.margins: 14
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Text { text: "PREVIEW"; color: theme.dim; font.pixelSize: 11; font.weight: Font.DemiBold; font.letterSpacing: 0.8 }
            Row {
                visible: root.running && root.live
                spacing: 5
                Rectangle {
                    id: liveDot
                    anchors.verticalCenter: parent.verticalCenter
                    width: 6; height: 6; radius: 3
                    color: theme.bad
                    // A blink once a second rather than a smooth pulse: an endless
                    // animation makes Qt redraw the whole window every frame, which
                    // took CPU away from ffmpeg for the whole conversion.
                    Timer {
                        interval: 1000
                        repeat: true
                        running: root.running && root.live
                        onTriggered: liveDot.opacity = liveDot.opacity < 1 ? 1 : 0.3
                        onRunningChanged: liveDot.opacity = 1
                    }
                }
                Text { text: "LIVE"; color: theme.bad; font.pixelSize: 10; font.weight: Font.DemiBold; font.letterSpacing: 0.8 }
            }
            Item { Layout.fillWidth: true }
            Segmented {
                options: [{ id: "output", text: "Output" }, { id: "source", text: "Source" }, { id: "both", text: "Side by side" }]
                current: root.mode
                onPicked: function (id) { root.mode = id; }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 10

            Repeater {
                model: root.mode === "both" ? ["source", "output"] : [root.mode]
                ColumnLayout {
                    required property string modelData
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1     // equal shares side by side
                    spacing: 4
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: root.frameHeight
                        color: "black"
                        radius: 4
                        clip: true
                        FrameView {
                            id: frame
                            anchors.fill: parent
                            // Until the run writes processed frames, both views show the source poster.
                            path: !job ? "" : modelData === "output" && root.live ? job.previewOut : job.previewSource
                            stamp: job ? job.previewStamp : 0
                        }
                        Rectangle {
                            anchors.left: parent.left
                            anchors.bottom: parent.bottom
                            anchors.margins: 8
                            visible: frame.ready && job !== null
                            color: Qt.rgba(0, 0, 0, 0.6)
                            radius: 4
                            implicitWidth: where.implicitWidth + 12
                            implicitHeight: where.implicitHeight + 6
                            Text {
                                id: where
                                anchors.centerIn: parent
                                color: "white"
                                font.pixelSize: 11
                                text: !job ? "" : root.live ? Format.duration(job.outTime) + " / " + Format.duration(job.info.duration)
                                                            : "Poster · " + Format.duration(job.info.duration * 0.1) + " in"
                            }
                        }
                        Text {
                            anchors.centerIn: parent
                            visible: !frame.ready
                            text: job && job.state === "probing" ? "Reading the file…" : "No picture yet"
                            color: theme.dim
                            font.pixelSize: 13
                        }
                    }
                    Text {
                        visible: root.mode === "both"
                        text: modelData === "source" ? "Source, as decoded" : "Output, after Encodr's processing"
                        color: theme.dim
                        font.pixelSize: 11
                        Layout.alignment: Qt.AlignHCenter
                    }
                }
            }
        }

        Text {
            visible: !root.live && job !== null && job.state !== "probing"
            text: "A poster frame from the source. The live picture appears as soon as converting starts."
            color: theme.dim
            font.pixelSize: 11
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }
        Text {
            visible: root.live && root.mode !== "source"
            text: "Shows the processed picture (timecode, watermark, scaling, deinterlacing, colour) just before compression. "
                  + "Updates once per second of video."
            color: theme.dim
            font.pixelSize: 11
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }
    }
}
