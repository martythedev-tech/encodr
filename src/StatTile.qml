import QtQuick
import QtQuick.Layouts

Rectangle {
    id: root
    property string label: ""
    property string value: "—"
    property string detail: ""
    property color valueColor: theme.foreground

    color: theme.raised
    radius: 8
    implicitHeight: 74
    Layout.fillWidth: true

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 2
        Text { text: root.label; color: theme.dim; font.pixelSize: 11; font.capitalization: Font.AllUppercase; font.letterSpacing: 0.6 }
        Text {
            text: root.value
            color: root.valueColor
            font.pixelSize: 20
            font.bold: true
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
