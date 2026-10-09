import QtQuick
import QtQuick.Layouts

// A row of mutually exclusive choices in one outlined group, for switching
// views. `options` is [{id, text}]; `current` is the chosen id.
Rectangle {
    id: root
    property var options: []
    property string current: ""
    signal picked(string id)

    implicitWidth: row.implicitWidth + 4
    implicitHeight: 26
    radius: 4
    color: theme.background
    border.width: 1
    border.color: theme.border

    RowLayout {
        id: row
        anchors.fill: parent
        anchors.margins: 2
        spacing: 2
        Repeater {
            model: root.options
            Rectangle {
                required property var modelData
                readonly property bool on: root.current === modelData.id
                Layout.fillHeight: true
                implicitWidth: label.implicitWidth + 18
                radius: 3
                color: on ? theme.raised : mouse.containsMouse ? theme.hover : "transparent"
                Text {
                    id: label
                    anchors.centerIn: parent
                    text: modelData.text
                    color: parent.on ? theme.foreground : theme.dim
                    font.pixelSize: 11
                    font.weight: Font.Medium
                }
                MouseArea {
                    id: mouse
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: root.picked(modelData.id)
                }
            }
        }
    }
}
