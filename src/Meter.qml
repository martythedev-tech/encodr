import QtQuick

// A thin progress line, 0-100.
Rectangle {
    id: root
    property real value: 0
    property color barColor: theme.accent
    implicitHeight: 3
    radius: height / 2
    color: theme.raised
    Rectangle {
        height: parent.height
        radius: parent.radius
        width: parent.width * Math.max(0, Math.min(1, root.value / 100))
        color: root.barColor
    }
}
