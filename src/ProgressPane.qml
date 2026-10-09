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
        spacing: 10

        // Headline: what's happening and when it will be done.
        Panel {
            spacing: 10

            RowLayout {
                Layout.fillWidth: true
                spacing: 12
                ColumnLayout {
                    spacing: 3
                    Layout.fillWidth: true
                    RowLayout {
                        spacing: 6
                        Rectangle {
                            implicitWidth: 6; implicitHeight: 6; radius: 3
                            color: stateLabel.color
                        }
                        Text {
                            id: stateLabel
                            text: job ? Format.stateLabel(job.state).toUpperCase() : ""
                            color: !job ? theme.dim : job.state === "failed" ? theme.bad : job.state === "done" ? theme.good
                                 : job.state === "paused" ? theme.warn : job.state === "running" ? theme.accent : theme.dim
                            font.pixelSize: 10
                            font.weight: Font.DemiBold
                            font.letterSpacing: 0.8
                        }
                    }
                    Text {
                        text: job ? job.fileName : ""
                        color: theme.foreground
                        font.pixelSize: 16
                        font.weight: Font.DemiBold
                        elide: Text.ElideMiddle
                        Layout.fillWidth: true
                    }
                }
                Text {
                    text: job ? Format.number(job.percent, job.percent > 0 && job.percent < 100 ? 1 : 0) + "%" : ""
                    color: theme.foreground
                    font.pixelSize: 30
                    font.weight: Font.Medium
                    font.features: { "tnum": 1 }
                    Layout.alignment: Qt.AlignBottom
                }
            }

            Meter {
                Layout.fillWidth: true
                implicitHeight: 4
                value: job ? job.percent : 0
                barColor: stateLabel.color
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 12
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    color: job && job.state === "failed" ? theme.bad : theme.dim
                    font.pixelSize: 12
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
                            return "Waiting in the queue.";
                        return job.message;
                    }
                }
                Flow {
                    spacing: 6
                    layoutDirection: Qt.RightToLeft
                    Layout.maximumWidth: root.availableWidth * 0.6
                    UiButton {
                        visible: job && job.state === "ready" && !backend.running
                        text: "Start queue"; hint: "Space"; kind: "primary"
                        onClicked: backend.startQueue()
                    }
                    UiButton {
                        visible: root.live
                        text: job && job.state === "paused" ? "Resume" : "Pause"; hint: "Space"
                        kind: job && job.state === "paused" ? "primary" : "default"
                        onClicked: backend.toggleQueue()
                    }
                    UiButton {
                        visible: root.live
                        text: "Stop"; hint: "C"; kind: "ghost"; danger: true
                        onClicked: stopDialog.open()
                    }
                    UiButton {
                        visible: root.done
                        text: "Play"; hint: "Enter"; kind: "primary"
                        onClicked: backend.openOutput(root.jobIndex)
                    }
                    UiButton {
                        visible: root.done
                        text: "Show in folder"; hint: "F"
                        onClicked: backend.revealOutput(root.jobIndex)
                    }
                    UiButton {
                        visible: job && (job.state === "failed" || job.state === "cancelled" || job.state === "done")
                        text: "Convert again"; hint: "R"; kind: "ghost"
                        onClicked: backend.retryJob(root.jobIndex)
                    }
                }
            }
        }

        PreviewPanel { id: preview; Layout.fillWidth: true; job: root.job }

        // The numbers.
        GridLayout {
            Layout.fillWidth: true
            columns: root.width > 980 ? 4 : root.width > 640 ? 3 : 2
            columnSpacing: 8
            rowSpacing: 8

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
            StatTile {
                readonly property bool wontFit: job !== null && job.freeSpace > 0 && job.projectedSize - job.size > job.freeSpace
                label: root.done ? "Final size" : "Size"
                value: job ? Format.bytes(root.done ? job.projectedSize : job.size) : "—"
                valueColor: wontFit ? theme.bad : theme.foreground
                detail: {
                    if (!job || job.projectedSize <= 0 || info.size <= 0)
                        return job && job.freeSpace > 0 ? Format.bytes(job.freeSpace) + " free on the output drive" : "";
                    if (wontFit)
                        return "→ " + Format.bytes(job.projectedSize) + " won't fit on the drive!";
                    const share = Math.round(100 * job.projectedSize / info.size) + "% of the original";
                    return root.done ? share + " " + Format.bytes(info.size) : "→ " + Format.bytes(job.projectedSize) + " projected · " + share;
                }
            }
            StatTile { label: "Encoder CPU"; value: root.live ? Format.number(tel.processCpu, 0) + "%" : job && job.peakCpu > 0 ? Format.number(job.peakCpu, 0) + "%" : "—"
                       detail: root.live ? "of all " + tel.coreCount + " cores · peak " + Format.number(job.peakCpu, 0) + "%" : job && job.peakCpu > 0 ? "peak" : "" }
            StatTile { label: "Energy used"; value: job && job.energyWh > 0 ? Format.number(job.energyWh, job.energyWh < 10 ? 2 : 1) + " Wh" : "—"
                       detail: tel.systemWatts > 0 ? "whole computer now " + Format.number(tel.systemWatts, 1) + " W" : "" }
            StatTile {
                label: "CPU clock"
                value: root.live && tel.clocksMeasured ? Math.round(tel.clockShare * 100) + "%"
                       : job && job.lowestClockShare > 0 ? Math.round(job.lowestClockShare * 100) + "%" : "—"
                valueColor: (root.live && tel.throttled) ? theme.bad : theme.foreground
                detail: {
                    if (!job || job.lowestClockShare <= 0)
                        return "of top speed, measured";
                    const heat = job.heatSeconds > 0 ? "slowed by heat for " + Format.words(job.heatSeconds) : "never slowed by heat";
                    return (root.live ? "lowest " + Math.round(job.lowestClockShare * 100) + "% · " : "lowest of top speed · ") + heat;
                }
            }
            StatTile { label: "Quantizer"; value: job && job.quantizer >= 0 ? Format.number(job.quantizer, 1) : "—"
                       detail: (job && (job.droppedFrames > 0 || job.duplicatedFrames > 0))
                               ? job.droppedFrames + " dropped · " + job.duplicatedFrames + " duplicated frames"
                               : "lower is finer detail" }
        }

        // The graphs.
        GridLayout {
            Layout.fillWidth: true
            columns: root.width > 760 ? 2 : 1
            columnSpacing: 8
            rowSpacing: 8
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
        Panel {
            title: "Recipe"
            visible: job !== null && job.commandLine !== ""
            header: [
                Item { Layout.fillWidth: true },
                UiButton { text: "Copy command"; kind: "ghost"; implicitHeight: 22; onClicked: backend.copyText(job.commandLine) }
            ]

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
    readonly property bool dialogOpen: stopDialog.visible
    function cyclePreview() {
        preview.cycle();
    }
    function askStop() {
        if (live)
            stopDialog.open();
    }
}
