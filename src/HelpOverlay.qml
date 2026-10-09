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
        ["1  2  3", "Progress, Source and Settings tabs"],
        ["?", "Show or hide this list"],
        ["Q", "Quit (asks first while converting)"]
    ]

    Rectangle {
        anchors.centerIn: parent
        width: Math.min(620, parent.width - 40)
        implicitHeight: list.implicitHeight + 44
        radius: 12
        color: theme.surface
        border.color: theme.raised

        ColumnLayout {
            id: list
            anchors.fill: parent
            anchors.margins: 22
            spacing: 9
            Text { text: "Keyboard shortcuts"; color: theme.foreground; font.pixelSize: 18; font.bold: true; Layout.bottomMargin: 6 }
            Repeater {
                model: root.keys
                RowLayout {
                    required property var modelData
                    spacing: 16
                    Text { text: modelData[0]; color: theme.accent; font.pixelSize: 13; font.bold: true; Layout.preferredWidth: 210 }
                    Text { text: modelData[1]; color: theme.foreground; font.pixelSize: 13; Layout.fillWidth: true; wrapMode: Text.Wrap }
                }
            }
        }
    }
}
