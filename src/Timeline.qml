import QtQuick
import QtQuick.Layouts
import "Format.js" as Format

// The source laid out in time: thumbnails across the whole clip with the
// playhead (press or drag on them to scrub), and under them a thin lane for
// the in and out points (press or drag there and the nearer one follows).
// The player owns playback; this only asks it to seek.
ColumnLayout {
    id: bar
    property var job: null
    property real position: 0        // the player's, in seconds
    property bool playing: false
    property bool muted: false
    property bool canPlay: true
    signal seek(real seconds)
    signal scrubStarted()
    signal scrubFinished()
    signal playPause()
    signal toggleMute()

    readonly property real duration: job && job.info.duration > 0 ? job.info.duration : 0
    property real dragIn: -1
    property real dragOut: -1
    readonly property real inPoint: dragIn >= 0 ? dragIn : (job ? job.trimIn : 0)
    readonly property real outPoint: dragOut >= 0 ? dragOut : (job ? job.trimOut : 0)
    readonly property bool cut: job !== null && (job.trimmed || dragIn >= 0 || dragOut >= 0)

    // For the keys: the in or out point at the playhead.
    function markIn() { if (job) job.setTrim(Math.min(position, outPoint - 0.1), job.trimOut); }
    function markOut() { if (job) job.setTrim(job.trimIn, Math.max(position, inPoint + 0.1)); }

    spacing: 6

    RowLayout {
        Layout.fillWidth: true
        spacing: 6
        UiButton {
            kind: "ghost"
            implicitWidth: 28
            implicitHeight: 24
            enabled: bar.canPlay
            onClicked: bar.playPause()
            Icon { anchors.centerIn: parent; name: bar.playing ? "pause" : "play"; color: theme.foreground; size: 14 }
        }
        Text {
            color: theme.foreground
            font.pixelSize: 12
            font.features: { "tnum": 1 }
            text: Format.precise(bar.position) + " / " + Format.duration(bar.duration)
        }
        UiButton {
            kind: "ghost"
            implicitWidth: 28
            implicitHeight: 24
            enabled: bar.canPlay
            onClicked: bar.toggleMute()
            Icon { anchors.centerIn: parent; name: bar.muted ? "mute" : "sound"; color: bar.muted ? theme.dim : theme.foreground; size: 14 }
        }
        // Takes the rest of the row and right-aligns, rather than sizing
        // itself from the row's width (which looped the layout).
        Text {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignRight
            elide: Text.ElideLeft
            color: bar.cut ? theme.foreground : theme.dim
            font.pixelSize: 11
            font.features: { "tnum": 1 }
            text: bar.cut ? "In " + Format.precise(bar.inPoint) + "  ·  Out " + Format.precise(bar.outPoint)
                            + "  ·  " + Format.precise(bar.outPoint - bar.inPoint)
                          : "Drag the tabs under the strip to convert just part"
        }
        UiButton {
            visible: job !== null && job.trimmed
            kind: "ghost"
            text: "Reset"
            implicitHeight: 22
            onClicked: job.clearTrim()
        }
    }

    Item {
        id: track
        Layout.fillWidth: true
        implicitHeight: 40

        function xFor(t) { return bar.duration > 0 ? t / bar.duration * width : 0; }
        function timeAt(x) { return Math.max(0, Math.min(bar.duration, x / Math.max(1, width) * bar.duration)); }

        Rectangle {
            anchors.fill: parent
            radius: 3
            color: theme.raised
            clip: true
            Image {
                anchors.fill: parent
                visible: job !== null && job.hasStrip
                source: visible ? "file://" + job.strip : ""
                fillMode: Image.Stretch
                cache: false
                asynchronous: true
            }
            // Outside the in and out points: dimmed.
            Rectangle { x: 0; width: track.xFor(bar.inPoint); height: parent.height; color: Qt.rgba(0, 0, 0, 0.6) }
            Rectangle {
                x: track.xFor(bar.outPoint)
                width: parent.width - x
                height: parent.height
                color: Qt.rgba(0, 0, 0, 0.6)
            }
        }
        // What the whole-file check found: cuts as faint lines, problems as
        // coloured bars along the bottom.
        Repeater {
            model: job && job.analysed ? job.moments : []
            Rectangle {
                required property var modelData
                readonly property bool cut: modelData.kind === "cut"
                x: track.xFor(modelData.start)
                y: cut ? 0 : track.height - 5
                width: cut ? 1 : Math.max(2, track.xFor(modelData.end) - x)
                height: cut ? track.height : 5
                color: cut ? "white" : modelData.kind === "black" ? theme.dim : modelData.kind === "freeze" ? theme.bad : theme.warn
                opacity: cut ? 0.35 : 0.95
            }
        }
        // The kept part, outlined.
        Rectangle {
            visible: bar.cut
            x: track.xFor(bar.inPoint)
            width: Math.max(1, track.xFor(bar.outPoint) - x)
            height: parent.height
            color: "transparent"
            border.color: theme.accent
            border.width: 1
            radius: 3
        }
        // The playhead.
        Rectangle {
            x: track.xFor(bar.position) - 1
            y: -3
            width: 2
            height: parent.height + 6
            color: "white"
            visible: bar.duration > 0
        }
        MouseArea {
            anchors.fill: parent
            enabled: bar.duration > 0
            cursorShape: Qt.PointingHandCursor
            onPressed: function (mouse) { bar.scrubStarted(); bar.seek(track.timeAt(mouse.x)); }
            onPositionChanged: function (mouse) { if (pressed) bar.seek(track.timeAt(mouse.x)); }
            onReleased: bar.scrubFinished()
            onCanceled: bar.scrubFinished()
        }
    }

    // The in and out points: tabs on their own lane.
    Item {
        id: lane
        Layout.fillWidth: true
        Layout.topMargin: -2
        implicitHeight: 12

        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width
            height: 2
            radius: 1
            color: theme.border
        }
        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            x: track.xFor(bar.inPoint)
            width: Math.max(1, track.xFor(bar.outPoint) - x)
            height: 2
            color: bar.cut ? theme.accent : theme.dim
        }
        Repeater {
            model: [0, 1]
            Rectangle {
                required property int modelData
                x: track.xFor(modelData === 0 ? bar.inPoint : bar.outPoint) - (modelData === 0 ? 0 : width)
                width: 10
                height: parent.height
                radius: 2
                color: bar.cut ? theme.accent : theme.foreground
            }
        }
        MouseArea {
            anchors.fill: parent
            anchors.topMargin: -2
            anchors.bottomMargin: -4
            enabled: bar.duration > 0
            cursorShape: Qt.SizeHorCursor
            property bool movingIn: true
            function place(mx) {
                const t = track.timeAt(mx);
                if (movingIn)
                    bar.dragIn = Math.min(t, bar.outPoint - 0.1);
                else
                    bar.dragOut = Math.max(t, bar.inPoint + 0.1);
                bar.seek(movingIn ? bar.dragIn : bar.dragOut);    // show the frame at the point
            }
            onPressed: function (mouse) {
                bar.scrubStarted();
                const t = track.timeAt(mouse.x);
                movingIn = Math.abs(t - bar.inPoint) <= Math.abs(t - bar.outPoint);
                place(mouse.x);
            }
            onPositionChanged: function (mouse) { if (pressed) place(mouse.x); }
            onReleased: {
                bar.job.setTrim(bar.inPoint, bar.outPoint);
                bar.dragIn = -1;
                bar.dragOut = -1;
                bar.scrubFinished();
            }
            onCanceled: { bar.dragIn = -1; bar.dragOut = -1; bar.scrubFinished(); }
        }
    }
}
