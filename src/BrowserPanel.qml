import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "Format.js" as Format

// Finds files without leaving Encodr: places and drives across the top,
// then the folder's contents with a thumbnail and details for every video.
// Double-click (or drag onto the window) adds a file to the queue.
Dock {
    id: root
    property string selected: ""

    // Nerd Font glyphs from the Omarchy terminal font.
    readonly property var icons: ({ home: "", folder: "", drive: "", computer: "",
                                    film: "", music: "", file: "", up: "", check: "" })

    function add(url) {
        backend.addUrls([url]);
    }

    title: "Browser"
    header: [
        Item { Layout.fillWidth: true },
        Text {
            visible: browser.pending > 0
            text: "reading " + browser.pending + "…"
            color: theme.dim
            font.pixelSize: 11
        }
    ]

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Places and drives.
        Flow {
            Layout.fillWidth: true
            Layout.margins: 8
            spacing: 4
            Repeater {
                model: browser.places
                Rectangle {
                    required property var modelData
                    readonly property bool here: browser.path === modelData.path
                    implicitWidth: placeRow.implicitWidth + 14
                    implicitHeight: 22
                    radius: 3
                    color: here ? theme.raised : placeMouse.containsMouse ? theme.hover : "transparent"
                    border.width: 1
                    border.color: here ? theme.border : "transparent"
                    Row {
                        id: placeRow
                        anchors.centerIn: parent
                        spacing: 5
                        Text {
                            text: root.icons[modelData.kind] || root.icons.folder
                            color: modelData.kind === "drive" ? theme.accent : theme.dim
                            font.family: theme.mono
                            font.pixelSize: 10
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        Text {
                            text: modelData.name
                            color: parent.parent.here ? theme.foreground : theme.dim
                            font.pixelSize: 11
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }
                    MouseArea {
                        id: placeMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: browser.open(modelData.path)
                    }
                }
            }
        }

        // Where we are, and the way up.
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 8
            Layout.rightMargin: 8
            Layout.bottomMargin: 6
            spacing: 6
            UiButton {
                text: root.icons.up
                font.family: theme.mono
                kind: "ghost"
                implicitWidth: 24
                implicitHeight: 22
                leftPadding: 0
                rightPadding: 0
                enabled: browser.canGoUp
                onClicked: browser.up()
            }
            Text {
                text: browser.folderName
                color: theme.foreground
                font.pixelSize: 12
                font.weight: Font.Medium
                elide: Text.ElideMiddle
                Layout.fillWidth: true
            }
            Text {
                text: browser.mediaCount > 0 ? browser.mediaCount + " media" : ""
                color: theme.dim
                font.pixelSize: 11
            }
        }
        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: theme.border }

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: browser
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            delegate: Rectangle {
                id: row
                required property string name
                required property string path
                required property url url
                required property bool isDir
                required property bool isMedia
                required property bool isAudio
                required property string thumb
                required property string details
                required property bool converted
                required property string convertedName
                required property bool read
                readonly property bool picked: root.selected === path

                width: list.width
                height: isMedia ? 52 : 28
                color: picked ? theme.raised : mouse.containsMouse ? theme.hover : "transparent"

                // Dragged out as a file, so it can be dropped on the queue (or another app).
                Item {
                    id: dragProxy
                    Drag.active: mouse.drag.active
                    Drag.dragType: Drag.Automatic
                    Drag.supportedActions: Qt.CopyAction
                    Drag.mimeData: { "text/uri-list": row.url.toString() }
                }

                MouseArea {
                    id: mouse
                    anchors.fill: parent
                    hoverEnabled: true
                    drag.target: row.isMedia ? dragProxy : null
                    onClicked: root.selected = row.path
                    onDoubleClicked: {
                        if (row.isDir)
                            browser.open(row.path);
                        else if (row.isMedia)
                            root.add(row.url);
                    }
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8
                    spacing: 9

                    // Thumbnail, or an icon for folders, audio and other files.
                    Rectangle {
                        Layout.preferredWidth: row.isMedia ? 64 : 16
                        Layout.preferredHeight: row.isMedia ? 36 : 16
                        radius: 3
                        color: row.isMedia ? "black" : "transparent"
                        clip: true
                        Image {
                            anchors.fill: parent
                            visible: row.thumb !== ""
                            source: row.thumb
                            fillMode: Image.PreserveAspectCrop
                            asynchronous: true
                            sourceSize.height: 72
                        }
                        Text {
                            anchors.centerIn: parent
                            visible: row.thumb === ""
                            text: row.isDir ? root.icons.folder : row.isAudio ? root.icons.music : row.isMedia ? root.icons.film : root.icons.file
                            color: row.isDir ? theme.accent : theme.dim
                            opacity: row.isMedia && !row.read ? 0.5 : 1
                            font.family: theme.mono
                            font.pixelSize: row.isMedia ? 14 : 12
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        Text {
                            text: row.name
                            color: row.isDir || row.isMedia ? theme.foreground : theme.dim
                            font.pixelSize: 12
                            font.weight: row.isMedia ? Font.Medium : Font.Normal
                            elide: Text.ElideMiddle
                            Layout.fillWidth: true
                        }
                        Text {
                            visible: row.isMedia
                            text: row.read ? row.details : "Reading…"
                            color: theme.dim
                            font.pixelSize: 11
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                    }

                    Text {
                        visible: row.converted && !(mouse.containsMouse && row.isMedia)
                        text: root.icons.check
                        color: theme.good
                        font.family: theme.mono
                        font.pixelSize: 11
                        ToolTip.visible: checkMouse.containsMouse
                        ToolTip.text: "Already converted: " + row.convertedName
                        MouseArea { id: checkMouse; anchors.fill: parent; hoverEnabled: true; acceptedButtons: Qt.NoButton }
                    }
                    UiButton {
                        visible: row.isMedia && mouse.containsMouse
                        text: "Add"
                        implicitHeight: 22
                        onClicked: root.add(row.url)
                    }
                }
            }

            Text {
                anchors.centerIn: parent
                visible: list.count === 0
                text: "This folder is empty"
                color: theme.dim
                font.pixelSize: 12
            }
        }
    }
}
