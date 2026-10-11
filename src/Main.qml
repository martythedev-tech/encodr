import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtCore
import QtQuick.Layouts
import "Format.js" as Format

ApplicationWindow {
    id: win
    width: 1560
    height: 960
    minimumWidth: 1080
    minimumHeight: 620
    visible: true
    title: {
        var running = null;
        for (var i = 0; i < backend.jobs.length; i++)
            if (backend.jobs[i].state === "running")
                running = backend.jobs[i];
        return running ? Math.floor(running.percent) + "% · " + running.fileName + " — Encodr" : "Encodr";
    }
    color: theme.background

    Material.theme: Material.Dark
    Material.accent: theme.accent
    Material.background: theme.surface
    Material.foreground: theme.foreground
    Material.roundedScale: Material.ExtraSmallScale
    Material.containerStyle: Material.Outlined

    property bool helpVisible: false
    readonly property var job: backend.selectedJob
    // Letter shortcuts stay quiet while a text field has the keyboard.
    readonly property bool typing: activeFocusItem !== null && activeFocusItem.hasOwnProperty("cursorPosition")
    // ...and while a confirmation is up, so Enter answers the dialog instead of playing a file.
    readonly property bool blocked: typing || quitDialog.visible || stopDialog.visible

    function quit() {
        if (backend.hasActiveWork)
            quitDialog.open();
        else
            Qt.quit();
    }
    onClosing: function (close) {
        if (backend.hasActiveWork && !quitDialog.confirmed) {
            close.accepted = false;
            quitDialog.open();
        }
    }

    Shortcut { sequence: "?"; context: Qt.ApplicationShortcut; enabled: !win.blocked; onActivated: win.helpVisible = !win.helpVisible }
    Shortcut { sequence: "Esc"; context: Qt.ApplicationShortcut; enabled: win.helpVisible; onActivated: win.helpVisible = false }
    Shortcut { sequence: "Q"; context: Qt.ApplicationShortcut; enabled: !win.blocked; onActivated: win.quit() }
    Shortcut { sequence: "Space"; context: Qt.ApplicationShortcut; enabled: !win.blocked; onActivated: backend.toggleQueue() }
    Shortcut { sequence: "O"; context: Qt.ApplicationShortcut; enabled: !win.blocked; onActivated: backend.addFilesDialog() }
    Shortcut { sequences: ["Delete", "Backspace"]; context: Qt.ApplicationShortcut; enabled: !win.blocked; onActivated: backend.removeJob(backend.selectedIndex) }
    Shortcut { sequence: "Ctrl+Z"; context: Qt.ApplicationShortcut; enabled: !win.blocked; onActivated: backend.undo() }
    Shortcut { sequence: "Ctrl+Shift+Z"; context: Qt.ApplicationShortcut; enabled: !win.blocked; onActivated: backend.redo() }
    Shortcut { sequences: ["Down", "J"]; context: Qt.ApplicationShortcut; enabled: !win.blocked; onActivated: backend.selectedIndex = backend.selectedIndex + 1 }
    Shortcut { sequences: ["Up", "K"]; context: Qt.ApplicationShortcut; enabled: !win.blocked; onActivated: backend.selectedIndex = Math.max(0, backend.selectedIndex - 1) }
    Shortcut { sequence: "Alt+Down"; context: Qt.ApplicationShortcut; onActivated: backend.moveJob(backend.selectedIndex, 1) }
    Shortcut { sequence: "Alt+Up"; context: Qt.ApplicationShortcut; onActivated: backend.moveJob(backend.selectedIndex, -1) }
    Shortcut { sequence: "C"; context: Qt.ApplicationShortcut; enabled: !win.blocked; onActivated: win.askStop() }
    Shortcut { sequence: "R"; context: Qt.ApplicationShortcut; enabled: !win.blocked; onActivated: backend.retryJob(backend.selectedIndex) }
    Shortcut { sequences: ["Return", "Enter"]; context: Qt.ApplicationShortcut; enabled: !win.blocked; onActivated: backend.openOutput(backend.selectedIndex) }
    Shortcut { sequence: "F"; context: Qt.ApplicationShortcut; enabled: !win.blocked; onActivated: backend.revealOutput(backend.selectedIndex) }
    Shortcut { sequence: "V"; context: Qt.ApplicationShortcut; enabled: !win.blocked; onActivated: viewer.cycle() }
    Shortcut { sequence: "X"; context: Qt.ApplicationShortcut; enabled: !win.blocked; onActivated: backend.clearFinished() }
    // The player and the in and out points.
    Shortcut { sequence: "P"; context: Qt.ApplicationShortcut; enabled: !win.blocked; onActivated: viewer.togglePlay() }
    Shortcut { sequence: "["; context: Qt.ApplicationShortcut; enabled: !win.blocked; onActivated: viewer.markIn() }
    Shortcut { sequence: "]"; context: Qt.ApplicationShortcut; enabled: !win.blocked; onActivated: viewer.markOut() }
    Shortcut { sequence: "Left"; context: Qt.ApplicationShortcut; enabled: !win.blocked; onActivated: viewer.step(-1) }
    Shortcut { sequence: "Right"; context: Qt.ApplicationShortcut; enabled: !win.blocked; onActivated: viewer.step(1) }
    Shortcut { sequence: ","; context: Qt.ApplicationShortcut; enabled: !win.blocked; onActivated: viewer.step(-viewer.frameStep) }
    Shortcut { sequence: "."; context: Qt.ApplicationShortcut; enabled: !win.blocked; onActivated: viewer.step(viewer.frameStep) }

    component Hairline: Rectangle { color: theme.border; implicitHeight: 1; implicitWidth: 1 }
    // A split handle: a gap of window background between panels that shows a
    // line while hovered or dragged.
    component Divider: Rectangle {
        implicitWidth: 5
        implicitHeight: 5
        color: SplitHandle.pressed ? theme.accent : SplitHandle.hovered ? theme.border : theme.background
    }

    function askStop() {
        if (win.job && (win.job.state === "running" || win.job.state === "paused"))
            stopDialog.open();
    }

    Settings {
        id: layout
        category: "layout"
        property var columns
        property var left
        property var middle
        property var right
    }
    Component.onCompleted: {
        if (layout.columns) columns.restoreState(layout.columns);
        if (layout.left) leftColumn.restoreState(layout.left);
        if (layout.middle) middleColumn.restoreState(layout.middle);
        if (layout.right) rightColumn.restoreState(layout.right);
    }
    Component.onDestruction: {
        layout.columns = columns.saveState();
        layout.left = leftColumn.saveState();
        layout.middle = middleColumn.saveState();
        layout.right = rightColumn.saveState();
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Top bar
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 46
            color: theme.surface

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 10
                spacing: 8

                Text { text: "Encodr"; color: theme.foreground; font.pixelSize: 15; font.weight: Font.DemiBold }
                Text { text: Qt.application.version; color: theme.dim; font.pixelSize: 11; Layout.alignment: Qt.AlignBaseline }
                Rectangle { visible: status.text !== ""; implicitWidth: 1; implicitHeight: 14; color: theme.border; Layout.leftMargin: 4; Layout.rightMargin: 4 }
                Text {
                    id: status
                    text: backend.jobs.length === 0 ? "" :
                          backend.jobs.length + " file" + (backend.jobs.length === 1 ? "" : "s") + " · "
                          + backend.doneCount + " done · " + backend.waitingCount + " waiting"
                    color: theme.dim
                    font.pixelSize: 12
                }
                Item { Layout.fillWidth: true }
                UiButton { text: "Add files"; hint: "O"; onClicked: backend.addFilesDialog() }
                UiButton {
                    text: backend.paused ? "Resume" : backend.hasActiveWork ? "Pause" : "Start"
                    hint: "Space"
                    kind: !backend.hasActiveWork || backend.paused ? "primary" : "default"
                    enabled: backend.hasActiveWork || backend.waitingCount > 0
                    onClicked: backend.toggleQueue()
                }
                UiButton { text: "?"; kind: "ghost"; implicitWidth: 28; onClicked: win.helpVisible = true }
            }
        }
        Hairline { Layout.fillWidth: true }

        // The workspace: browser and queue | picture and settings | source and stats.
        // Every divider drags, and the layout is remembered.
        SplitView {
            id: columns
            Layout.fillWidth: true
            Layout.fillHeight: true
            orientation: Qt.Horizontal
            handle: Divider {}

            SplitView {
                id: leftColumn
                orientation: Qt.Vertical
                handle: Divider {}
                SplitView.preferredWidth: 300
                SplitView.minimumWidth: 220
                BrowserPanel { SplitView.fillHeight: true; SplitView.minimumHeight: 160 }
                QueuePanel { SplitView.preferredHeight: 240; SplitView.minimumHeight: 110 }
            }

            SplitView {
                id: middleColumn
                orientation: Qt.Vertical
                handle: Divider {}
                SplitView.fillWidth: true
                SplitView.minimumWidth: 420
                ViewerPanel {
                    id: viewer
                    SplitView.fillHeight: true
                    SplitView.minimumHeight: 240
                    job: win.job
                    jobIndex: backend.selectedIndex
                    onStopRequested: win.askStop()
                }
                Dock {
                    title: "Settings"
                    SplitView.preferredHeight: 320
                    SplitView.minimumHeight: 120
                    header: [
                        Text {
                            visible: win.job !== null
                            text: win.job ? win.job.preset.name : ""
                            color: theme.dim
                            font.pixelSize: 11
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                    ]
                    SettingsPane { anchors.fill: parent; anchors.topMargin: 10; job: win.job; jobIndex: backend.selectedIndex; visible: win.job !== null }
                    Text { anchors.centerIn: parent; visible: win.job === null; text: "Select a file to see its settings"; color: theme.dim; font.pixelSize: 12 }
                }
            }

            SplitView {
                id: rightColumn
                orientation: Qt.Vertical
                handle: Divider {}
                SplitView.preferredWidth: 340
                SplitView.minimumWidth: 260
                Dock {
                    title: "Source"
                    SplitView.preferredHeight: 340
                    SplitView.minimumHeight: 100
                    SourcePane { anchors.fill: parent; anchors.topMargin: 10; job: win.job; visible: win.job !== null; onSeekRequested: function (t) { viewer.seek(t); } }
                    Text { anchors.centerIn: parent; visible: win.job === null; text: "Select a file to inspect it"; color: theme.dim; font.pixelSize: 12 }
                }
                StatsPanel { SplitView.fillHeight: true; SplitView.minimumHeight: 160; job: win.job }
            }
        }

        // Status bar
        Hairline { Layout.fillWidth: true }
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 26
            color: theme.surface

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 16
                spacing: 18

                Text {
                    color: backend.notice !== "" ? theme.warn : theme.dim
                    font.pixelSize: 11
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    text: {
                        if (backend.notice !== "")
                            return backend.notice;
                        if (backend.running && backend.queueRemaining >= 0)
                            return "Queue " + Format.number(backend.queuePercent, 0) + "% · all done around "
                                   + Format.clock(backend.queueFinishAt) + " (" + Format.words(backend.queueRemaining) + ")";
                        return "Press ? for shortcuts";
                    }
                }
                // An AppImage isn't in the app menu until it's added.
                RowLayout {
                    visible: appMenu.offer
                    spacing: 6
                    Text { text: "Add Encodr to your app menu?"; color: theme.dim; font.pixelSize: 11 }
                    UiButton { kind: "primary"; text: "Add"; implicitHeight: 22; onClicked: appMenu.add() }
                    UiButton { kind: "ghost"; text: "No thanks"; implicitHeight: 22; onClicked: appMenu.decline() }
                }
                Text {
                    visible: backend.inhibiting
                    text: "Sleep blocked while converting"
                    color: theme.dim
                    font.pixelSize: 11
                }
                Text {
                    text: "CPU " + Format.number(backend.telemetry.cpuTotal, 0) + "%"
                          + (backend.telemetry.systemWatts > 0 ? " · " + Format.number(backend.telemetry.systemWatts, 1) + " W" : "")
                          + (backend.telemetry.hottest > 0 ? " · " + Format.number(backend.telemetry.hottest, 0) + " °C" : "")
                    color: backend.telemetry.throttled ? theme.bad : theme.dim
                    font.pixelSize: 11
                    font.features: { "tnum": 1 }
                }
            }
            Timer {
                running: backend.notice !== ""
                interval: 6000
                onTriggered: backend.dismissNotice()
            }
        }
    }

    // Drag files or folders in from anywhere.
    DropArea {
        id: drop
        anchors.fill: parent
        onDropped: function (event) {
            if (event.hasUrls)
                backend.addUrls(event.urls);
        }
        Rectangle {
            anchors.fill: parent
            visible: drop.containsDrag
            color: Qt.rgba(0, 0, 0, 0.55)
            border.color: theme.accent
            border.width: 2
            Text { anchors.centerIn: parent; text: "Drop to add to the queue"; color: theme.foreground; font.pixelSize: 16; font.weight: Font.DemiBold }
        }
    }

    HelpOverlay { anchors.fill: parent; visible: win.helpVisible; onClose: win.helpVisible = false }

    Dialog {
        id: stopDialog
        anchors.centerIn: Overlay.overlay
        modal: true
        title: "Stop converting?"
        standardButtons: Dialog.Yes | Dialog.No
        Label { text: "The unfinished file will be deleted. The original isn't touched." }
        onAccepted: backend.cancelJob(backend.selectedIndex)
    }

    Dialog {
        id: quitDialog
        property bool confirmed: false
        anchors.centerIn: Overlay.overlay
        modal: true
        title: "Quit while converting?"
        standardButtons: Dialog.Yes | Dialog.No
        Label { text: "The conversion in progress will stop and its unfinished file will be deleted." }
        onAccepted: {
            confirmed = true;
            Qt.quit();
        }
    }
}
