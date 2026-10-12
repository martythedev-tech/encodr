import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtMultimedia
import "Format.js" as Format

// The middle of the workspace: the picture being converted (or a poster
// frame before the run), and under it where the job stands and its controls.
Dock {
    id: root
    property var job: null
    property int jobIndex: -1
    property string mode: "output"      // output, source, both, compare (after a run)
    // Before converting, the player can show the frame at the playhead as the
    // conversion will make it (a still, while paused): source or output.
    property string playerView: "source"
    signal stopRequested()

    readonly property bool live: job !== null && job.hasLivePreview
    readonly property bool running: job !== null && job.state === "running"
    readonly property bool active: job !== null && (job.state === "running" || job.state === "paused")
    readonly property bool done: job !== null && job.state === "done"
    readonly property bool hasPicture: job !== null && job.info.hasVideo === true
    readonly property color stateColor: !job ? theme.dim : job.state === "failed" ? theme.bad : job.state === "done" ? theme.good
                                       : job.state === "paused" ? theme.warn : job.state === "running" ? theme.accent : theme.dim

    function cycle() {
        if (stillMode)
            playerView = playerView === "output" ? "source" : "output";
        else
            mode = mode === "output" ? "source" : mode === "source" ? "both"
                 : mode === "both" && canCompare ? "compare" : "output";
    }

    // Outside a conversion the picture is a player: the source, or after a
    // run (with Output chosen) the finished file.
    readonly property bool playerMode: job !== null && !active && job.state !== "probing" && job.info.duration > 0
    readonly property bool playsOutput: playerMode && done && (mode === "output" || mode === "compare")
    // After a run, paused: the source as the conversion meant to make it
    // and the converted file, wiped across each other (not for joined
    // clips: the Output still knows only the first).
    readonly property bool canCompare: done && hasPicture && job.canShowOutput && job.clipCount < 2
    readonly property bool comparing: playerMode && canCompare && mode === "compare" && !playing
    function requestCompare() {
        if (!comparing)
            return;
        job.showOutputAt(job.trimIn + position);
        job.showFinishedAt(position);
    }
    onComparingChanged: requestCompare()
    readonly property string playPath: !playerMode ? "" : playsOutput ? job.outputPath : job.inputPath
    readonly property real position: player.position / 1000
    readonly property bool playing: player.playbackState === MediaPlayer.PlayingState
    readonly property real frameStep: job && job.info.fps > 0 ? 1 / job.info.fps : 0.04
    // The player before a finished run, with something to process.
    readonly property bool stillMode: playerMode && !done && hasPicture && job.canShowOutput
    readonly property bool showsStill: stillMode && playerView === "output" && !playing

    function requestStill() {
        if (showsStill)
            job.showOutputAt(position);
    }
    onShowsStillChanged: requestStill()
    onPositionChanged: if (!playing) { requestStill(); requestCompare(); }

    function seek(seconds) {
        player.position = Math.max(0, Math.round(seconds * 1000));
    }
    // While dragging, at most one seek per tick to the latest spot: every
    // seek decodes from the previous keyframe, and a backlog of them is what
    // makes scrubbing stutter.
    property real pendingSeek: -1
    function scrubTo(seconds) {
        if (!scrubTimer.running) {
            seek(seconds);
            scrubTimer.start();
        } else {
            pendingSeek = seconds;
        }
    }
    Timer {
        id: scrubTimer
        interval: 50
        onTriggered: {
            if (root.pendingSeek >= 0) {
                root.seek(root.pendingSeek);
                root.pendingSeek = -1;
                start();
            }
        }
    }
    function togglePlay() {
        if (!playerMode)
            return;
        if (playing) {
            player.pause();
            return;
        }
        // From the in point when the playhead sits outside the kept part.
        if (job.trimmed && !playsOutput && (position < job.trimIn || position >= job.trimOut - 0.05))
            seek(job.trimIn);
        player.play();
    }
    function step(seconds) {
        if (!playerMode)
            return;
        player.pause();
        seek(position + seconds);
    }
    function markIn() { if (playerMode && !playsOutput) timeline.markIn(); }
    function markOut() { if (playerMode && !playsOutput) timeline.markOut(); }

    MediaPlayer {
        id: player
        source: root.playPath === "" ? "" : "file://" + encodeURI(root.playPath).replace(/#/g, "%23").replace(/\?/g, "%3F")
        audioOutput: AudioOutput { id: audioOut }
        videoOutput: videoOut
        // Show a frame as soon as a file opens, at the in point. Once per file:
        // seeking reports "loaded" again, and must not stop playback.
        property url opened: ""
        onSourceChanged: opened = ""
        onMediaStatusChanged: {
            if (mediaStatus === MediaPlayer.LoadedMedia && opened !== source) {
                opened = source;
                root.seek(root.playsOutput || !root.job ? 0 : root.job.trimIn);
                pause();
            }
        }
        onPositionChanged: {
            // A trimmed source stops at its out point.
            if (root.playing && root.job && root.job.trimmed && !root.playsOutput && root.position >= root.job.trimOut)
                pause();
        }
    }

    title: "Preview"
    header: [
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
        },
        Item { Layout.fillWidth: true },
        Segmented {
            // Something to compare only once a conversion has started.
            visible: root.hasPicture && (!root.playerMode || root.done || root.stillMode)
            implicitHeight: 22
            options: root.stillMode ? [{ id: "source", text: "Source" }, { id: "output", text: "Output" }]
                                    : [{ id: "output", text: "Output" }, { id: "source", text: "Source" }, { id: "both", text: "Side by side" }]
                                      .concat(root.canCompare ? [{ id: "compare", text: "Compare" }] : [])
            current: root.stillMode ? root.playerView : root.mode
            onPicked: function (id) {
                if (root.stillMode)
                    root.playerView = id;
                else
                    root.mode = id;
            }
        }
    ]

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 10

        // The picture, as big as the panel allows.
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 120

            RowLayout {
                anchors.fill: parent
                spacing: 8
                visible: root.hasPicture
                Repeater {
                    model: root.playerMode ? ["player"] : root.mode === "both" ? ["source", "output"] : [root.mode]
                    Rectangle {
                        required property string modelData
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.preferredWidth: 1     // equal shares side by side
                        color: "black"
                        radius: 4
                        clip: true
                        FrameView {
                            id: frame
                            anchors.fill: parent
                            visible: modelData !== "player" || !root.playsOutput
                            // Until the run writes processed frames, both views show the source poster.
                            path: !job ? "" : modelData === "output" && root.live ? job.previewOut : job.previewSource
                            stamp: job ? job.previewStamp : 0
                        }
                        Rectangle {
                            anchors.left: parent.left
                            anchors.bottom: parent.bottom
                            anchors.margins: 8
                            visible: frame.ready && job !== null && modelData !== "player"
                            color: Qt.rgba(0, 0, 0, 0.6)
                            radius: 3
                            implicitWidth: where.implicitWidth + 12
                            implicitHeight: where.implicitHeight + 6
                            Text {
                                id: where
                                anchors.centerIn: parent
                                color: "white"
                                font.pixelSize: 11
                                font.features: { "tnum": 1 }
                                text: (root.mode === "both" ? (modelData === "source" ? "Source · " : "Output · ") : "")
                                      + (!job ? "" : root.live ? Format.duration(job.outTime) + " / " + Format.duration(job.totalDuration)
                                                              : "Frame at " + Format.precise(job.posterTime))
                            }
                        }
                        Text {
                            anchors.centerIn: parent
                            visible: !frame.ready
                            text: job && job.state === "probing" ? "Reading the file…" : "No picture yet"
                            color: theme.dim
                            font.pixelSize: 12
                        }
                    }
                }
            }

            // The player's picture, over the poster until its first frame.
            VideoOutput {
                id: videoOut
                anchors.fill: parent
                visible: root.playerMode && root.hasPicture
                fillMode: VideoOutput.PreserveAspectFit
            }
            // The crop: what's taken off dimmed, what's kept outlined.
            Item {
                id: cropMask
                readonly property bool shown: videoOut.visible && !root.playsOutput && !root.showsStill && job !== null
                                              && (job.cropLeft + job.cropTop + job.cropRight + job.cropBottom) > 0
                                              && job.info.width > 0 && job.info.height > 0
                readonly property rect area: videoOut.contentRect
                readonly property real sx: area.width / Math.max(1, job ? job.info.width : 1)
                readonly property real sy: area.height / Math.max(1, job ? job.info.height : 1)
                visible: shown
                x: area.x
                y: area.y
                width: area.width
                height: area.height
                readonly property real l: job ? job.cropLeft * sx : 0
                readonly property real t: job ? job.cropTop * sy : 0
                readonly property real r: job ? width - job.cropRight * sx : width
                readonly property real b: job ? height - job.cropBottom * sy : height
                Rectangle { x: 0; y: 0; width: parent.width; height: parent.t; color: Qt.rgba(0, 0, 0, 0.65) }
                Rectangle { x: 0; y: parent.b; width: parent.width; height: parent.height - parent.b; color: Qt.rgba(0, 0, 0, 0.65) }
                Rectangle { x: 0; y: parent.t; width: parent.l; height: parent.b - parent.t; color: Qt.rgba(0, 0, 0, 0.65) }
                Rectangle { x: parent.r; y: parent.t; width: parent.width - parent.r; height: parent.b - parent.t; color: Qt.rgba(0, 0, 0, 0.65) }
                Rectangle {
                    x: parent.l
                    y: parent.t
                    width: parent.r - parent.l
                    height: parent.b - parent.t
                    color: "transparent"
                    border.color: theme.accent
                    border.width: 1
                }
            }
            // Before and after, over the player's picture while paused.
            WipeView {
                anchors.fill: parent
                visible: root.comparing
                beforePath: job ? job.outputStill : ""
                beforeStamp: job ? job.outputStillStamp : 0
                afterPath: job ? job.finishedStill : ""
                afterStamp: job ? job.finishedStillStamp : 0
                beforeLabel: "Source, processed · " + Format.precise(root.position)
                afterLabel: "Converted"
            }
            // The output still, over the player's picture while paused.
            Rectangle {
                anchors.fill: parent
                visible: root.showsStill
                color: "black"
                radius: 4
                FrameView {
                    id: still
                    anchors.fill: parent
                    path: job ? job.outputStill : ""
                    stamp: job ? job.outputStillStamp : 0
                }
                Text {
                    anchors.centerIn: parent
                    visible: !still.ready
                    text: job && job.outputStillError !== "" ? job.outputStillError : "Processing the frame…"
                    color: job && job.outputStillError !== "" ? theme.bad : theme.dim
                    font.pixelSize: 12
                    width: Math.min(implicitWidth, parent.width - 24)
                    wrapMode: Text.Wrap
                    horizontalAlignment: Text.AlignHCenter
                }
            }
            Rectangle {
                anchors.left: parent.left
                anchors.bottom: parent.bottom
                anchors.margins: 8
                visible: videoOut.visible
                color: Qt.rgba(0, 0, 0, 0.6)
                radius: 3
                implicitWidth: playerTime.implicitWidth + 12
                implicitHeight: playerTime.implicitHeight + 6
                Text {
                    id: playerTime
                    anchors.centerIn: parent
                    color: "white"
                    font.pixelSize: 11
                    font.features: { "tnum": 1 }
                    text: {
                        if (!root.showsStill)
                            return (root.playsOutput ? "Output · " : "Source · ") + Format.precise(root.position)
                                   + (root.stillMode && root.playerView === "output" ? " · pause to see the output" : "")
                                   + (root.mode === "compare" && root.playing ? " · pause to compare" : "");
                        if (job.outputStillError !== "" && still.ready)
                            return "Output · couldn't update: " + job.outputStillError;
                        return "Output · " + Format.precise(job.outputStillTime >= 0 ? job.outputStillTime : root.position)
                               + (job.outputStillBusy ? " · updating…" : "");
                    }
                }
            }

            ColumnLayout {
                anchors.centerIn: parent
                visible: !root.hasPicture
                spacing: 6
                Text {
                    Layout.alignment: Qt.AlignHCenter
                    text: !job ? "Nothing selected" : job.state === "probing" ? "Reading the file…" : "Audio only — no picture"
                    color: theme.foreground
                    font.pixelSize: 13
                    font.weight: Font.Medium
                }
                Text {
                    Layout.alignment: Qt.AlignHCenter
                    visible: !job
                    text: "Double-click a file in the browser, drop files here, or press O"
                    color: theme.dim
                    font.pixelSize: 12
                }
            }
        }

        Timeline {
            id: timeline
            Layout.fillWidth: true
            job: root.job
            output: root.playsOutput
            visible: root.playerMode
            position: root.position
            playing: root.playing
            muted: audioOut.muted
            // Hold still while dragging so the picture follows the mouse, then
            // carry on playing if it was.
            property bool resume: false
            onScrubStarted: { resume = root.playing; if (resume) player.pause(); }
            onSeek: function (seconds) { root.scrubTo(seconds); }
            onScrubFinished: {
                // Land exactly where the mouse let go.
                scrubTimer.stop();
                if (root.pendingSeek >= 0)
                    root.seek(root.pendingSeek);
                root.pendingSeek = -1;
                if (resume)
                    player.play();
                resume = false;
            }
            onPlayPause: root.togglePlay()
            onToggleMute: audioOut.muted = !audioOut.muted
        }

        // Where the job stands.
        ColumnLayout {
            Layout.fillWidth: true
            visible: job !== null
            spacing: 6

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Rectangle { implicitWidth: 6; implicitHeight: 6; radius: 3; color: root.stateColor }
                Text {
                    text: job ? Format.stateLabel(job.state).toUpperCase() : ""
                    color: root.stateColor
                    font.pixelSize: 10
                    font.weight: Font.DemiBold
                    font.letterSpacing: 0.8
                }
                Text {
                    text: job ? job.fileName : ""
                    color: theme.foreground
                    font.pixelSize: 13
                    font.weight: Font.Medium
                    elide: Text.ElideMiddle
                    Layout.fillWidth: true
                }
                Text {
                    text: job ? Format.number(job.percent, job.percent > 0 && job.percent < 100 ? 1 : 0) + "%" : ""
                    color: theme.foreground
                    font.pixelSize: 20
                    font.weight: Font.Medium
                    font.features: { "tnum": 1 }
                }
            }

            Meter {
                Layout.fillWidth: true
                implicitHeight: 4
                value: job ? job.percent : 0
                barColor: root.stateColor
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 10
                Text {
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    color: job && job.state === "failed" ? theme.bad : theme.dim
                    font.pixelSize: 12
                    font.features: { "tnum": 1 }
                    text: {
                        if (!job)
                            return "";
                        if (job.state === "running" && job.message !== "")
                            return job.message;    // a first pass, e.g. measuring loudness
                        if (job.state === "running" && job.remaining >= 0)
                            return Format.duration(job.elapsed) + " elapsed · " + Format.words(job.remaining) + " left · done around "
                                   + Format.clock(job.finishAt) + " · " + Format.number(job.speed, 2) + "×";
                        if (job.state === "running")
                            return "Measuring speed…";
                        if (job.state === "done")
                            return "Took " + Format.words(job.elapsed) + " · " + Format.bytes(job.projectedSize)
                                   + " (" + Math.round(100 * job.projectedSize / Math.max(1, job.totalSize)) + "% of the original)";
                        if (job.state === "ready")
                            return "Waiting in the queue";
                        return job.message.split("\n")[0];
                    }
                }
                UiButton {
                    visible: job !== null && job.state === "ready" && !backend.running
                    text: "Start queue"; hint: "Space"; kind: "primary"
                    onClicked: backend.startQueue()
                }
                UiButton {
                    visible: root.active
                    text: "Stop"; hint: "C"; kind: "ghost"; danger: true
                    onClicked: root.stopRequested()
                }
                UiButton {
                    visible: root.active
                    text: job && job.state === "paused" ? "Resume" : "Pause"; hint: "Space"
                    kind: job && job.state === "paused" ? "primary" : "default"
                    onClicked: backend.toggleQueue()
                }
                UiButton {
                    visible: job !== null && (job.state === "failed" || job.state === "cancelled" || job.state === "done")
                    text: "Convert again"; hint: "R"; kind: "ghost"
                    onClicked: backend.retryJob(root.jobIndex)
                }
                UiButton {
                    visible: root.done
                    text: "Show in folder"; hint: "F"
                    onClicked: backend.revealOutput(root.jobIndex)
                }
                UiButton {
                    visible: root.done
                    text: "Play"; hint: "Enter"; kind: "primary"
                    onClicked: backend.openOutput(root.jobIndex)
                }
            }
        }
    }
}
