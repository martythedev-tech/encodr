import QtQuick
import QtQuick.Layouts

// A flat section of the window: hairline border, small corners, an optional
// small-caps title. Children stack in a column; `header` items sit to the
// right of the title.
Rectangle {
    id: root
    property string title: ""
    property int padding: 14
    property int spacing: 8
    default property alias content: body.data
    property alias header: headerRow.data

    Layout.fillWidth: true
    color: theme.surface
    radius: 6
    border.width: 1
    border.color: theme.border
    implicitHeight: column.implicitHeight + padding * 2

    ColumnLayout {
        id: column
        x: root.padding
        y: root.padding
        width: root.width - root.padding * 2
        spacing: 10

        RowLayout {
            visible: root.title !== "" || headerRow.children.length > 0
            Layout.fillWidth: true
            Layout.minimumHeight: 18
            spacing: 8
            Text {
                visible: root.title !== ""
                text: root.title.toUpperCase()
                color: theme.dim
                font.pixelSize: 11
                font.weight: Font.DemiBold
                font.letterSpacing: 0.8
            }
            RowLayout {
                id: headerRow
                Layout.fillWidth: true
                spacing: 8
            }
        }

        ColumnLayout {
            id: body
            Layout.fillWidth: true
            spacing: root.spacing
        }
    }
}
