import QtQuick
import QtQuick.Layouts

Rectangle {
    id: root
    signal close()
    color: Qt.rgba(0, 0, 0, 0.7)

    MouseArea { anchors.fill: parent; onClicked: root.close() }

    readonly property var keys: [
        ["Space", "Start the queue, or pause / resume the current file"],
        ["O", "Add files (or drag them onto the window)"],
        ["V", "Preview: output, source, or side by side"],
        ["C", "Stop the current file"],
        ["R", "Convert the selected file again"],
        ["Enter", "Play the finished file"],
        ["F", "Show the finished file in its folder"],
        ["↑ ↓  or  K J", "Select a file"],
        ["Alt + ↑ ↓", "Move a file up or down the queue"],
        ["Delete", "Remove the selected file from the queue"],
        ["X", "Clear finished files from the queue"],
        ["Ctrl + Z / Ctrl + Shift + Z", "Undo / redo removing"],
        ["Double-click", "In the browser: open a folder, or add a file to the queue"],
        ["?", "Show or hide this list"],
        ["Q", "Quit (asks first while converting)"]
    ]

    Rectangle {
        anchors.centerIn: parent
        width: Math.min(620, parent.width - 40)
        implicitHeight: list.implicitHeight + 44
        radius: 8
        color: theme.surface
        border.width: 1
        border.color: theme.border

        ColumnLayout {
            id: list
            anchors.fill: parent
            anchors.margins: 20
            spacing: 7
            RowLayout {
                Layout.bottomMargin: 8
                Text { text: "Keyboard shortcuts"; color: theme.foreground; font.pixelSize: 15; font.weight: Font.DemiBold; Layout.fillWidth: true }
                Text { text: "Encodr " + Qt.application.version; color: theme.dim; font.pixelSize: 11 }
            }
            Repeater {
                model: root.keys
                RowLayout {
                    required property var modelData
                    spacing: 16
                    Item {
                        Layout.preferredWidth: 200
                        implicitHeight: 20
                        Rectangle {
                            implicitWidth: keyText.implicitWidth + 12
                            height: 20
                            radius: 3
                            color: theme.background
                            border.width: 1
                            border.color: theme.border
                            Text { id: keyText; anchors.centerIn: parent; text: modelData[0]; color: theme.foreground; font.pixelSize: 11; font.family: theme.mono }
                        }
                    }
                    Text { text: modelData[1]; color: theme.dim; font.pixelSize: 12; Layout.fillWidth: true; wrapMode: Text.Wrap }
                }
            }
        }
    }
}
