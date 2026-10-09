import QtQuick
import QtQuick.Layouts

Rectangle {
    id: root
    property string label: ""
    property string value: "—"
    property string detail: ""
    property color valueColor: theme.foreground

    color: theme.surface
    radius: 6
    border.width: 1
    border.color: theme.border
    implicitHeight: 66
    Layout.fillWidth: true

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: 12
        anchors.rightMargin: 12
        anchors.topMargin: 9
        anchors.bottomMargin: 9
        spacing: 1
        Text { text: root.label.toUpperCase(); color: theme.dim; font.pixelSize: 10; font.weight: Font.DemiBold; font.letterSpacing: 0.7 }
        Text {
            text: root.value
            color: root.valueColor
            font.pixelSize: 18
            font.weight: Font.Medium
            font.features: { "tnum": 1 }
            elide: Text.ElideRight
            Layout.fillWidth: true
        }
        Text {
            text: root.detail
            visible: text !== ""
            color: theme.dim
            font.pixelSize: 11
            elide: Text.ElideRight
            Layout.fillWidth: true
        }
    }
}
