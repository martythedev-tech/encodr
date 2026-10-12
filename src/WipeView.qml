import QtQuick

// Two frames of the same moment, one over the other, split by a line you
// drag: the source as the conversion was meant to make it on the left, the
// converted file on the right.
Rectangle {
    id: root
    property string beforePath: ""
    property int beforeStamp: 0
    property string afterPath: ""
    property int afterStamp: 0
    property string beforeLabel: "Source"
    property string afterLabel: "Converted"
    property real split: 0.5
    readonly property bool ready: before.ready && after.ready

    color: "black"
    radius: 4
    clip: true

    FrameView {
        id: after
        anchors.fill: parent
        path: root.afterPath
        stamp: root.afterStamp
    }
    Item {
        width: Math.round(root.width * root.split)
        height: root.height
        clip: true
        FrameView {
            id: before
            width: root.width
            height: root.height
            path: root.beforePath
            stamp: root.beforeStamp
        }
    }

    Rectangle {
        x: Math.round(root.width * root.split) - 1
        width: 2
        height: root.height
        color: Qt.rgba(1, 1, 1, 0.85)
    }
    Rectangle {
        x: Math.round(root.width * root.split) - width / 2
        anchors.verticalCenter: parent.verticalCenter
        width: 22
        height: 22
        radius: 11
        color: Qt.rgba(0, 0, 0, 0.6)
        border.width: 1
        border.color: Qt.rgba(1, 1, 1, 0.85)
        Text { anchors.centerIn: parent; text: "↔"; color: "white"; font.pixelSize: 12 }
    }

    component Tag: Rectangle {
        property alias text: label.text
        color: Qt.rgba(0, 0, 0, 0.6)
        radius: 3
        implicitWidth: label.implicitWidth + 12
        implicitHeight: label.implicitHeight + 6
        Text { id: label; anchors.centerIn: parent; color: "white"; font.pixelSize: 11 }
    }
    Tag { anchors.left: parent.left; anchors.top: parent.top; anchors.margins: 8; text: root.beforeLabel }
    Tag { anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 8; text: root.afterLabel }

    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.SizeHorCursor
        onPressed: function (mouse) { root.split = Math.max(0, Math.min(1, mouse.x / root.width)); }
        onPositionChanged: function (mouse) { if (pressed) root.split = Math.max(0, Math.min(1, mouse.x / root.width)); }
    }

    Text {
        anchors.centerIn: parent
        visible: !root.ready
        text: "Making the frames…"
        color: "#aaaaaa"
        font.pixelSize: 12
    }
}
