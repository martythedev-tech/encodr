import QtQuick
import QtQuick.Layouts

Rectangle {
    id: root
    property string label: ""
    property string value: "—"
    property string detail: ""
    property color valueColor: theme.foreground
    property bool compact: false

    color: compact ? theme.background : theme.surface
    radius: 6
    border.width: 1
    border.color: theme.border
    implicitHeight: compact ? 56 : 66
    Layout.fillWidth: true

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: root.compact ? 10 : 12
        anchors.rightMargin: root.compact ? 10 : 12
        anchors.topMargin: root.compact ? 7 : 9
        anchors.bottomMargin: root.compact ? 7 : 9
        spacing: 1
        Text { text: root.label.toUpperCase(); color: theme.dim; font.pixelSize: 10; font.weight: Font.DemiBold; font.letterSpacing: 0.7 }
        Text {
            text: root.value
            color: root.valueColor
            font.pixelSize: root.compact ? 15 : 18
            font.weight: Font.Medium
            font.features: { "tnum": 1 }
            elide: Text.ElideRight
            Layout.fillWidth: true
        }
        Text {
            text: root.detail
            visible: text !== ""
            color: theme.dim
            font.pixelSize: root.compact ? 10 : 11
            elide: Text.ElideRight
            Layout.fillWidth: true
        }
    }
}
