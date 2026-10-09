import QtQuick
import QtQuick.Layouts

// One panel of the workspace: a slim title bar, then a body that fills the
// rest. `header` items sit in the title bar after the title.
Rectangle {
    id: root
    property string title: ""
    property alias header: headerRow.data
    default property alias content: body.data

    color: theme.surface
    clip: true

    Item {
        id: bar
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: 32

        Text {
            id: titleText
            anchors.left: parent.left
            anchors.leftMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            text: root.title.toUpperCase()
            color: theme.dim
            font.pixelSize: 11
            font.weight: Font.DemiBold
            font.letterSpacing: 0.8
        }
        RowLayout {
            id: headerRow
            anchors.left: titleText.right
            anchors.leftMargin: 10
            anchors.right: parent.right
            anchors.rightMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            spacing: 6
        }
        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 1
            color: theme.border
        }
    }

    Item {
        id: body
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: bar.bottom
        anchors.bottom: parent.bottom
    }
}
