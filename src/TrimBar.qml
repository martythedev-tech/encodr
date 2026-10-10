import QtQuick
import QtQuick.Layouts
import "Format.js" as Format

// In and out points on the source's timeline. Press or drag anywhere: the
// nearer handle follows and the preview shows that frame; letting go sets
// the range to convert.
ColumnLayout {
    id: bar
    property var job: null
    readonly property real duration: job && job.info.duration > 0 ? job.info.duration : 0
    // While dragging, the handle being moved; otherwise the job's own points.
    property real dragIn: -1
    property real dragOut: -1
    readonly property real inPoint: dragIn >= 0 ? dragIn : (job ? job.trimIn : 0)
    readonly property real outPoint: dragOut >= 0 ? dragOut : (job ? job.trimOut : 0)
    readonly property bool cut: job !== null && (job.trimmed || dragIn >= 0 || dragOut >= 0)

    spacing: 4

    RowLayout {
        Layout.fillWidth: true
        spacing: 8
        Text { text: "TRIM"; color: theme.dim; font.pixelSize: 10; font.weight: Font.DemiBold; font.letterSpacing: 0.8 }
        Text {
            Layout.fillWidth: true
            elide: Text.ElideRight
            color: bar.cut ? theme.foreground : theme.dim
            font.pixelSize: 11
            font.features: { "tnum": 1 }
            text: bar.cut ? "In " + Format.precise(bar.inPoint) + "  ·  Out " + Format.precise(bar.outPoint)
                            + "  ·  " + Format.precise(bar.outPoint - bar.inPoint) + " of " + Format.duration(bar.duration)
                          : "Whole file · drag the ends to convert just part of it"
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
        implicitHeight: 18

        function xFor(t) { return bar.duration > 0 ? t / bar.duration * width : 0; }
        function timeAt(x) { return Math.max(0, Math.min(bar.duration, x / Math.max(1, width) * bar.duration)); }

        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width
            height: 4
            radius: 2
            color: theme.raised
        }
        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            x: track.xFor(bar.inPoint)
            width: Math.max(1, track.xFor(bar.outPoint) - x)
            height: 4
            radius: 2
            color: bar.cut ? theme.accent : theme.border
        }
        Repeater {
            model: [0, 1]
            Rectangle {
                required property int modelData
                anchors.verticalCenter: parent.verticalCenter
                x: track.xFor(modelData === 0 ? bar.inPoint : bar.outPoint) - width / 2
                width: 6
                height: 16
                radius: 2
                color: bar.cut ? theme.accent : theme.foreground
            }
        }
        MouseArea {
            anchors.fill: parent
            anchors.leftMargin: -6
            anchors.rightMargin: -6
            enabled: bar.duration > 0
            cursorShape: Qt.SizeHorCursor
            property bool movingIn: true
            function place(mx) {
                const t = track.timeAt(mx - 6);
                if (movingIn)
                    bar.dragIn = Math.min(t, bar.outPoint - 0.1);
                else
                    bar.dragOut = Math.max(t, bar.inPoint + 0.1);
                bar.job.showFrameAt(movingIn ? bar.dragIn : bar.dragOut);
            }
            onPressed: function (mouse) {
                const t = track.timeAt(mouse.x - 6);
                movingIn = Math.abs(t - bar.inPoint) <= Math.abs(t - bar.outPoint);
                place(mouse.x);
            }
            onPositionChanged: function (mouse) { if (pressed) place(mouse.x); }
            onReleased: {
                bar.job.setTrim(bar.inPoint, bar.outPoint);
                bar.dragIn = -1;
                bar.dragOut = -1;
            }
            onCanceled: { bar.dragIn = -1; bar.dragOut = -1; }
        }
    }
}
