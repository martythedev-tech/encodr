import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "Format.js" as Format

ScrollView {
    id: root
    property var job: null
    property var tel: backend.telemetry
    property int jobIndex: -1
    clip: true
    contentWidth: availableWidth

    readonly property bool live: job !== null && (job.state === "running" || job.state === "paused")
    readonly property bool done: job !== null && job.state === "done"
    readonly property var info: job ? job.info : ({})
    readonly property var hist: job ? job.history : ({})

    ColumnLayout {
        width: root.availableWidth
        spacing: 14

        // Headline: what's happening and when it will be done.
        Rectangle {
            Layout.fillWidth: true
            color: theme.surface
            radius: 10
            implicitHeight: headline.implicitHeight + 28

            ColumnLayout {
                id: headline
                anchors.fill: parent
                anchors.margins: 14
                spacing: 8

                RowLayout {
                    Layout.fillWidth: true
                    ColumnLayout {
                        spacing: 2
                        Layout.fillWidth: true
                        Text {
                            text: job ? Format.stateLabel(job.state).toUpperCase() : ""
                            color: !job ? theme.dim : job.state === "failed" ? theme.bad : job.state === "done" ? theme.good
                                 : job.state === "paused" ? theme.warn : theme.accent
                            font.pixelSize: 12
                            font.bold: true
                            font.letterSpacing: 1
                        }
                        Text {
                            text: job ? job.fileName : ""
                            color: theme.foreground
                            font.pixelSize: 20
                            font.bold: true
                            elide: Text.ElideMiddle
                            Layout.fillWidth: true
                        }
                    }
                    Text {
                        text: job ? Format.number(job.percent, job.percent > 0 && job.percent < 100 ? 1 : 0) + "%" : ""
                        color: theme.foreground
                        font.pixelSize: 40
                        font.bold: true
                    }
                }

                ProgressBar {
                    Layout.fillWidth: true
                    from: 0
                    to: 100
                    value: job ? job.percent : 0
                }

                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    color: job && job.state === "failed" ? theme.bad : theme.dim
                    font.pixelSize: 13
                    text: {
                        if (!job)
                            return "";
                        if (job.state === "running" && job.remaining >= 0)
                            return "Finishes around " + Format.clock(job.finishAt) + " — " + Format.words(job.remaining) + " left at the current pace.";
                        if (job.state === "running")
                            return "Measuring speed…";
                        if (job.state === "done")
                            return job.message + " · took " + Format.words(job.elapsed) + " · " + Format.bytes(job.projectedSize)
                                   + " (" + Math.round(100 * job.projectedSize / Math.max(1, info.size)) + "% of the original)";
                        if (job.state === "ready")
                            return "Waiting in the queue. Press Space to start.";
                        return job.message;
                    }
                }

                RowLayout {
                    spacing: 8
                    Button {
                        visible: root.live
                        text: job && job.state === "paused" ? "Resume  (Space)" : "Pause  (Space)"
                        onClicked: backend.toggleQueue()
                    }
                    Button {
                        visible: root.live
                        text: "Stop  (C)"
                        flat: true
                        onClicked: stopDialog.open()
                    }
                    Button {
                        visible: job && job.state === "ready" && !backend.running
                        text: "Start queue  (Space)"
                        highlighted: true
                        onClicked: backend.startQueue()
                    }
                    Button {
                        visible: root.done
                        text: "Play  (Enter)"
                        highlighted: true
                        onClicked: backend.openOutput(root.jobIndex)
                    }
                    Button {
                        visible: root.done
                        text: "Show in folder  (F)"
                        flat: true
                        onClicked: backend.revealOutput(root.jobIndex)
                    }
                    Button {
                        visible: job && (job.state === "failed" || job.state === "cancelled" || job.state === "done")
                        text: "Convert again  (R)"
                        flat: true
                        onClicked: backend.retryJob(root.jobIndex)
                    }
                }
            }
        }

        // The numbers.
        GridLayout {
            Layout.fillWidth: true
            columns: root.width > 980 ? 4 : root.width > 640 ? 3 : 2
            columnSpacing: 10
            rowSpacing: 10

            StatTile { label: "Elapsed"; value: job ? Format.duration(job.elapsed) : "—"
                       detail: job && job.startedAt && !isNaN(job.startedAt.getTime()) ? "started " + Format.clock(job.startedAt) : "" }
            StatTile { label: "Time left"; value: job && job.state === "running" ? Format.duration(job.remaining) : root.done ? "0:00" : "—"
                       detail: job && job.state === "running" && job.remaining >= 0 ? Format.words(job.remaining) : "" }
            StatTile { label: "Finishes at"; value: job && job.state === "running" ? Format.clock(job.finishAt) : root.done ? Format.clock(job.finishedAt) : "—"
                       valueColor: theme.accent; detail: root.done ? "finished" : job && job.state === "paused" ? "paused" : "" }
            StatTile { label: "Speed"; value: job && root.live ? Format.number(job.speed, 2) + "×" : root.done ? Format.number(job.averageSpeed, 2) + "×" : "—"
                       detail: job && job.averageSpeed > 0 ? "average " + Format.number(job.averageSpeed, 2) + "× real time" : "" }
            StatTile { label: "Frames"; value: job ? Format.thousands(job.frame) + (job.totalFrames > 0 ? " / " + Format.thousands(job.totalFrames) : "") : "—"
                       detail: job && job.fps > 0 ? Format.number(job.fps, 1) + " fps now · " + Format.number(job.averageFps, 1) + " avg" : "" }
            StatTile { label: "Converted"; value: job ? Format.duration(job.outTime) + " / " + Format.duration(info.duration) : "—"
                       detail: "of the source's running time" }
            StatTile { label: "Bitrate"; value: job ? Format.bitrate(root.live ? job.bitrateKbps : job.averageBitrateKbps) : "—"
                       detail: job && job.averageBitrateKbps > 0 ? "average " + Format.bitrate(job.averageBitrateKbps) + " · source " + Format.bps(info.bitrate) : "" }
            StatTile { label: "Size so far"; value: job ? Format.bytes(job.size) : "—"
                       detail: job && job.freeSpace > 0 ? Format.bytes(job.freeSpace) + " free on the output drive" : "" }
            StatTile {
                label: root.done ? "Final size" : "Projected size"
                value: job ? Format.bytes(job.projectedSize) : "—"
                valueColor: job && job.freeSpace > 0 && job.projectedSize - job.size > job.freeSpace ? theme.bad : theme.foreground
                detail: job && job.projectedSize > 0 && info.size > 0
                        ? (job.freeSpace > 0 && job.projectedSize - job.size > job.freeSpace ? "won't fit on the drive!" : Math.round(100 * job.projectedSize / info.size) + "% of the original " + Format.bytes(info.size))
                        : ""
            }
            StatTile { label: "Encoder CPU"; value: root.live ? Format.number(tel.processCpu, 0) + "%" : job && job.peakCpu > 0 ? Format.number(job.peakCpu, 0) + "%" : "—"
                       detail: root.live ? "of all " + tel.coreCount + " cores · peak " + Format.number(job.peakCpu, 0) + "%" : job && job.peakCpu > 0 ? "peak" : "" }
            StatTile { label: "Energy used"; value: job && job.energyWh > 0 ? Format.number(job.energyWh, job.energyWh < 10 ? 2 : 1) + " Wh" : "—"
                       detail: tel.systemWatts > 0 ? "whole computer now " + Format.number(tel.systemWatts, 1) + " W" : "" }
            StatTile { label: "Quantizer"; value: job && job.quantizer >= 0 ? Format.number(job.quantizer, 1) : "—"
                       detail: (job && (job.droppedFrames > 0 || job.duplicatedFrames > 0))
                               ? job.droppedFrames + " dropped · " + job.duplicatedFrames + " duplicated frames"
                               : "lower is finer detail" }
        }

        // The graphs.
        GridLayout {
            Layout.fillWidth: true
            columns: root.width > 760 ? 2 : 1
            columnSpacing: 10
            rowSpacing: 10
            Graph { Layout.fillWidth: true; title: "Speed (× real time)"; values: hist.speed || []; floor: 1
                    current: job && job.speed > 0 ? Format.number(job.speed, 2) + "×" : ""; lineColor: theme.accent }
            Graph { Layout.fillWidth: true; title: "Frames per second"; values: hist.fps || []
                    current: job && job.fps > 0 ? Format.number(job.fps, 1) : ""; lineColor: theme.good }
            Graph { Layout.fillWidth: true; title: "Output bitrate"; values: hist.bitrate || []
                    current: job ? Format.bitrate(job.bitrateKbps) : ""; lineColor: theme.warn }
            Graph { Layout.fillWidth: true; title: "Encoder CPU (% of the machine)"; values: hist.cpu || []; floor: 100
                    current: root.live ? Format.number(tel.processCpu, 0) + "%" : ""; lineColor: theme.bad }
            Graph { Layout.fillWidth: true; Layout.columnSpan: parent.columns; title: "Power draw, whole computer (W)"; values: hist.watts || []
                    current: tel.systemWatts > 0 ? Format.number(tel.systemWatts, 1) + " W" : ""; lineColor: theme.dim
                    visible: tel.systemWatts > 0 }
        }

        MachinePanel { Layout.fillWidth: true; tel: root.tel }

        // What will be (or was) run.
        Rectangle {
            Layout.fillWidth: true
            color: theme.surface
            radius: 10
            implicitHeight: recipe.implicitHeight + 28
            visible: job !== null && job.commandLine !== ""

            ColumnLayout {
                id: recipe
                anchors.fill: parent
                anchors.margins: 14
                spacing: 8

                Text { text: "RECIPE"; color: theme.dim; font.pixelSize: 11; font.bold: true; font.letterSpacing: 1 }
                Text {
                    text: job ? job.preset.name + " → " + Format.folder(job.outputPath) : ""
                    color: theme.foreground
                    font.pixelSize: 13
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
                    radius: 6
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
                        font.family: "monospace"
                        font.pixelSize: 11
                    }
                }
                Button {
                    text: "Copy ffmpeg command"
                    flat: true
                    onClicked: backend.copyText(job.commandLine)
                }
            }
        }

        Item { implicitHeight: 6 }
    }

    Dialog {
        id: stopDialog
        anchors.centerIn: Overlay.overlay
        modal: true
        title: "Stop converting?"
        standardButtons: Dialog.Yes | Dialog.No
        Label { text: "The unfinished file will be deleted. The original isn't touched." }
        onAccepted: backend.cancelJob(root.jobIndex)
    }
    function askStop() {
        if (live)
            stopDialog.open();
    }
}
