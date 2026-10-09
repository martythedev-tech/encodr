import QtQuick
import QtQuick.Layouts

// Left-aligned tabs with an accent underline and the number key that
// switches to each.
Rectangle {
    id: root
    property var labels: []
    property int current: 0
    signal picked(int index)

    implicitHeight: 38
    color: theme.background

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: theme.border
    }

    Row {
        anchors.left: parent.left
        anchors.leftMargin: 8
        height: parent.height
        Repeater {
            model: root.labels
            Item {
                required property string modelData
                required property int index
                readonly property bool on: root.current === index
                width: tabRow.implicitWidth + 24
                height: root.height
                Row {
                    id: tabRow
                    anchors.centerIn: parent
                    spacing: 7
                    Text {
                        text: modelData
                        color: parent.parent.on ? theme.foreground : mouse.containsMouse ? theme.foreground : theme.dim
                        font.pixelSize: 13
                        font.weight: parent.parent.on ? Font.DemiBold : Font.Normal
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Text {
                        text: index + 1
                        color: theme.dim
                        opacity: 0.6
                        font.pixelSize: 10
                        font.family: theme.mono
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
                Rectangle {
                    visible: parent.on
                    anchors.bottom: parent.bottom
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: parent.width - 16
                    height: 2
                    color: theme.accent
                }
                MouseArea {
                    id: mouse
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: root.picked(index)
                }
            }
        }
    }
}
