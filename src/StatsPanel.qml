import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "Format.js" as Format

// Every number about the job and the machine doing it, kept beside the
// picture: times, speed, sizes, live graphs, cores, clocks, heat and power.
Dock {
    id: dock
    property var job: null
    property var tel: backend.telemetry
    readonly property bool live: job !== null && (job.state === "running" || job.state === "paused")
    readonly property bool done: job !== null && job.state === "done"
    readonly property var info: job ? job.info : ({})
    readonly property var hist: job ? job.history : ({})

    title: "Stats"
    header: [
        Item { Layout.fillWidth: true },
        Text {
            visible: dock.tel.throttled
            text: "SLOWED BY HEAT" + (dock.tel.clocksMeasured ? " · " + Math.round(dock.tel.clockShare * 100) + "%" : "")
            color: theme.bad
            font.pixelSize: 10
            font.weight: Font.DemiBold
            font.letterSpacing: 0.5
        }
    ]

    ScrollView {
        id: root
        anchors.fill: parent
        contentWidth: availableWidth
        clip: true
        readonly property var job: dock.job
        readonly property var tel: dock.tel
        readonly property bool live: dock.live
        readonly property bool done: dock.done
        readonly property var info: dock.info
        readonly property var hist: dock.hist

        ColumnLayout {
            x: 10
            width: root.availableWidth - 20
            spacing: 8

            Item { implicitHeight: 2 }

        // The numbers.
            GridLayout {
                Layout.fillWidth: true
                columns: root.width > 640 ? 4 : 2
                columnSpacing: 8
                rowSpacing: 8

                StatTile { compact: true; label: "Elapsed"; value: job ? Format.duration(job.elapsed) : "—"
                           detail: job && job.startedAt && !isNaN(job.startedAt.getTime()) ? "started " + Format.clock(job.startedAt) : "" }
                StatTile { compact: true; label: "Time left"; value: job && job.state === "running" ? Format.duration(job.remaining) : root.done ? "0:00" : "—"
                           detail: job && job.state === "running" && job.remaining >= 0 ? Format.words(job.remaining) : "" }
                StatTile { compact: true; label: "Finishes at"; value: job && job.state === "running" ? Format.clock(job.finishAt) : root.done ? Format.clock(job.finishedAt) : "—"
                           valueColor: theme.accent; detail: root.done ? "finished" : job && job.state === "paused" ? "paused" : "" }
                StatTile { compact: true; label: "Speed"; value: job && root.live ? Format.number(job.speed, 2) + "×" : root.done ? Format.number(job.averageSpeed, 2) + "×" : "—"
                           detail: job && job.averageSpeed > 0 ? "average " + Format.number(job.averageSpeed, 2) + "× real time" : "" }
                StatTile { compact: true; label: "Frames"; value: job ? Format.thousands(job.frame) + (job.totalFrames > 0 ? " / " + Format.thousands(job.totalFrames) : "") : "—"
                           detail: job && job.fps > 0 ? Format.number(job.fps, 1) + " fps now · " + Format.number(job.averageFps, 1) + " avg" : "" }
                StatTile { compact: true; label: "Converted"; value: job ? Format.duration(job.outTime) + " / " + Format.duration(info.duration) : "—"
                           detail: "of the source's running time" }
                StatTile { compact: true; label: "Bitrate"; value: job ? Format.bitrate(root.live ? job.bitrateKbps : job.averageBitrateKbps) : "—"
                           detail: job && job.averageBitrateKbps > 0 ? "average " + Format.bitrate(job.averageBitrateKbps) + " · source " + Format.bps(info.bitrate) : "" }
                StatTile { compact: true;
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
                StatTile { compact: true; label: "Encoder CPU"; value: root.live ? Format.number(job.cpu, 0) + "%" : job && job.peakCpu > 0 ? Format.number(job.peakCpu, 0) + "%" : "—"
                       detail: root.live ? (backend.runningCount > 1 ? "all " + backend.runningCount + " files " + Format.number(tel.processCpu, 0) + "% · " : "of all " + tel.coreCount + " cores · ")
                                           + "peak " + Format.number(job.peakCpu, 0) + "%" : job && job.peakCpu > 0 ? "peak" : "" }
                StatTile { compact: true; label: "Energy used"; value: job && job.energyWh > 0 ? Format.number(job.energyWh, job.energyWh < 10 ? 2 : 1) + " Wh" : "—"
                           detail: tel.systemWatts > 0 ? "whole computer now " + Format.number(tel.systemWatts, 1) + " W" : "" }
                StatTile { compact: true;
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
                StatTile { compact: true; label: "Quantizer"; value: job && job.quantizer >= 0 ? Format.number(job.quantizer, 1) : "—"
                           detail: (job && (job.droppedFrames > 0 || job.duplicatedFrames > 0))
                                   ? job.droppedFrames + " dropped · " + job.duplicatedFrames + " duplicated frames"
                                   : "lower is finer detail" }
            }

            // The graphs.
            GridLayout {
                Layout.fillWidth: true
                columns: root.width > 640 ? 2 : 1
                columnSpacing: 8
                rowSpacing: 8
                Graph { Layout.fillWidth: true; implicitHeight: 84; color: theme.background; title: "Speed (× real time)"; values: hist.speed || []; floor: 1
                        current: job && job.speed > 0 ? Format.number(job.speed, 2) + "×" : ""; lineColor: theme.accent }
                Graph { Layout.fillWidth: true; implicitHeight: 84; color: theme.background; title: "Frames per second"; values: hist.fps || []
                        current: job && job.fps > 0 ? Format.number(job.fps, 1) : ""; lineColor: theme.good }
                Graph { Layout.fillWidth: true; implicitHeight: 84; color: theme.background; title: "Output bitrate"; values: hist.bitrate || []
                        current: job ? Format.bitrate(job.bitrateKbps) : ""; lineColor: theme.warn }
                Graph { Layout.fillWidth: true; implicitHeight: 84; color: theme.background; title: "Encoder CPU (% of the machine)"; values: hist.cpu || []; floor: 100
                        current: root.live ? Format.number(job.cpu, 0) + "%" : ""; lineColor: theme.bad }
                Graph { Layout.fillWidth: true; implicitHeight: 84; color: theme.background; Layout.columnSpan: parent.columns; title: "Power draw, whole computer (W)"; values: hist.watts || []
                        current: tel.systemWatts > 0 ? Format.number(tel.systemWatts, 1) + " W" : ""; lineColor: theme.dim
                        visible: tel.systemWatts > 0 }
            }


            MachinePanel { Layout.fillWidth: true; tel: root.tel; flat: true }
            Item { implicitHeight: 4 }
        }
    }
}
