import QtCore
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "Format.js" as Format

// Finds files without leaving Encodr: places and drives across the top,
// then the folder's contents, as a compact list (the default) or with a
// thumbnail and details for every video. Files Encodr made are dimmed and
// say which original they came from; other files are tucked away unless
// asked for. Double-click (or drag onto the window) adds a file to the queue.
Dock {
    id: root
    property string selected: ""

    // Nerd Font glyphs from the Omarchy terminal font.
    readonly property var icons: ({ home: "", folder: "", drive: "", computer: "",
                                    film: "", music: "", file: "", up: "", check: "" })

    readonly property bool listView: prefs.view !== "thumbnails"
    // In a narrow panel an output's "from …" goes on a line of its own, so
    // the name keeps the whole width.
    readonly property bool narrow: list.width < 480

    function add(url) {
        backend.addUrls([url]);
    }

    Settings {
        id: prefs
        category: "browser"
        property string view: "list"
        property bool showAll: false
        property string sortBy: "name"
    }
    Binding { target: browser; property: "showAll"; value: prefs.showAll }
    Binding { target: browser; property: "sortBy"; value: prefs.sortBy }

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
                        Icon {
                            name: modelData.kind === "home" || modelData.kind === "drive" || modelData.kind === "computer" ? modelData.kind : "folder"
                            color: modelData.kind === "drive" ? theme.accent : theme.dim
                            size: 12
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
                kind: "ghost"
                implicitWidth: 24
                implicitHeight: 22
                leftPadding: 0
                rightPadding: 0
                enabled: browser.canGoUp
                onClicked: browser.up()
                Icon { anchors.centerIn: parent; name: "up"; size: 13; color: parent.enabled ? theme.foreground : theme.dim }
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
        // How the folder is shown: sort order, and list or thumbnails.
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 8
            Layout.rightMargin: 8
            Layout.bottomMargin: 6
            spacing: 2
            Repeater {
                model: [{ id: "name", label: "Name" }, { id: "date", label: "Newest" }, { id: "size", label: "Largest" }]
                UiButton {
                    required property var modelData
                    kind: "ghost"
                    selected: prefs.sortBy === modelData.id
                    text: modelData.label
                    implicitHeight: 22
                    leftPadding: 7
                    rightPadding: 7
                    onClicked: prefs.sortBy = modelData.id
                }
            }
            Item { Layout.fillWidth: true }
            Repeater {
                model: [{ id: "list", icon: "list", tip: "List" }, { id: "thumbnails", icon: "grid", tip: "Thumbnails" }]
                UiButton {
                    required property var modelData
                    readonly property bool on: modelData.id === "list" ? root.listView : !root.listView
                    kind: "ghost"
                    selected: on
                    implicitWidth: 24
                    implicitHeight: 22
                    leftPadding: 0
                    rightPadding: 0
                    onClicked: prefs.view = modelData.id
                    ToolTip.visible: hovered
                    ToolTip.text: modelData.tip
                    Icon { anchors.centerIn: parent; name: modelData.icon; size: 13; color: parent.on ? theme.foreground : theme.dim }
                }
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
                required property string from
                required property real size
                required property real duration
                readonly property bool picked: root.selected === path
                // A file Encodr made: dimmed, with the original it came from.
                readonly property bool isOutput: from !== ""

                width: list.width
                height: root.listView ? (isOutput && root.narrow ? 38 : 26) : isMedia ? 52 : 28
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

                // List: one line per file.
                RowLayout {
                    visible: root.listView
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8
                    spacing: 7
                    Icon {
                        name: row.isDir ? "folder" : row.isAudio ? "music" : row.isMedia ? "film" : "file"
                        color: row.isDir ? theme.accent : theme.dim
                        opacity: row.isOutput ? 0.6 : 1
                        size: 13
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 1
                        Text {
                            text: row.name
                            color: row.isOutput || !(row.isDir || row.isMedia) ? theme.dim : theme.foreground
                            font.pixelSize: 12
                            elide: Text.ElideMiddle
                            Layout.fillWidth: true
                        }
                        Text {
                            visible: row.isOutput && root.narrow
                            text: "from " + row.from
                            color: theme.dim
                            opacity: 0.8
                            font.pixelSize: 10
                            elide: Text.ElideMiddle
                            Layout.fillWidth: true
                        }
                    }
                    Text {
                        visible: row.isOutput && !root.narrow
                        text: "from " + row.from
                        color: theme.dim
                        opacity: 0.8
                        font.pixelSize: 10
                        elide: Text.ElideMiddle
                        Layout.maximumWidth: row.width * 0.34
                    }
                    Text {
                        visible: row.isMedia && !row.isOutput && !(mouse.containsMouse)
                        text: row.read ? Format.duration(row.duration) : "…"
                        color: theme.dim
                        font.pixelSize: 11
                        font.features: { "tnum": 1 }
                    }
                    Text {
                        visible: !row.isDir && list.width > 300 && !(row.isMedia && mouse.containsMouse)
                        text: Format.bytes(row.size)
                        color: theme.dim
                        font.pixelSize: 11
                        font.features: { "tnum": 1 }
                        horizontalAlignment: Text.AlignRight
                        Layout.preferredWidth: 52
                    }
                    Icon {
                        visible: row.converted && !(mouse.containsMouse && row.isMedia)
                        name: "check"
                        color: theme.good
                        size: 12
                    }
                    UiButton {
                        visible: row.isMedia && mouse.containsMouse
                        text: "Add"
                        implicitHeight: 20
                        onClicked: root.add(row.url)
                    }
                }
                // The whole story on hover: details, and what it was made from or into.
                ToolTip.visible: root.listView && mouse.containsMouse && row.isMedia && row.read
                ToolTip.delay: 700
                ToolTip.text: row.details + (row.isOutput ? "\nMade from " + row.from : "")
                              + (row.converted ? "\nAlready converted: " + row.convertedName : "")

                // Thumbnails: a picture and a line of details.
                RowLayout {
                    visible: !root.listView
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
                        opacity: row.isOutput ? 0.6 : 1
                        Image {
                            anchors.fill: parent
                            visible: !root.listView && row.thumb !== ""
                            source: row.thumb
                            fillMode: Image.PreserveAspectCrop
                            asynchronous: true
                            sourceSize.height: 72
                        }
                        Icon {
                            anchors.centerIn: parent
                            visible: row.thumb === ""
                            name: row.isDir ? "folder" : row.isAudio ? "music" : row.isMedia ? "film" : "file"
                            color: row.isDir ? theme.accent : theme.dim
                            opacity: row.isMedia && !row.read ? 0.5 : 1
                            size: row.isMedia ? 16 : 14
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        Text {
                            text: row.name
                            color: (row.isDir || row.isMedia) && !row.isOutput ? theme.foreground : theme.dim
                            font.pixelSize: 12
                            font.weight: row.isMedia ? Font.Medium : Font.Normal
                            elide: Text.ElideMiddle
                            Layout.fillWidth: true
                        }
                        Text {
                            visible: row.isMedia
                            text: (row.isOutput ? "from " + row.from + " · " : "") + (row.read ? row.details : "Reading…")
                            color: theme.dim
                            font.pixelSize: 11
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                    }

                    Icon {
                        visible: row.converted && !(mouse.containsMouse && row.isMedia)
                        name: "check"
                        color: theme.good
                        size: 13
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
                text: browser.hiddenCount > 0 ? "No videos or folders here" : "This folder is empty"
                color: theme.dim
                font.pixelSize: 12
            }
        }

        // Subtitles, LUTs, logos and the like: out of the way until wanted.
        RowLayout {
            visible: browser.hiddenCount > 0 || prefs.showAll
            Layout.fillWidth: true
            Layout.leftMargin: 8
            Layout.rightMargin: 8
            Layout.topMargin: 4
            Layout.bottomMargin: 6
            Text {
                text: prefs.showAll ? "Showing all files" : browser.hiddenCount + (browser.hiddenCount === 1 ? " other file" : " other files") + " hidden"
                color: theme.dim
                font.pixelSize: 11
                Layout.fillWidth: true
            }
            UiButton {
                kind: "ghost"
                text: prefs.showAll ? "Hide others" : "Show"
                implicitHeight: 20
                onClicked: prefs.showAll = !prefs.showAll
            }
        }
    }
}
