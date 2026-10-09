import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import "Format.js" as Format

ApplicationWindow {
    id: win
    width: 1320
    height: 860
    minimumWidth: 860
    minimumHeight: 560
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

    property bool helpVisible: false
    property int tab: 0
    readonly property var job: backend.selectedJob
    // Letter shortcuts stay quiet while a text field has the keyboard.
    readonly property bool typing: activeFocusItem !== null && activeFocusItem.hasOwnProperty("cursorPosition")
    // ...and while a confirmation is up, so Enter answers the dialog instead of playing a file.
    readonly property bool blocked: typing || quitDialog.visible || progressPane.dialogOpen

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
    Shortcut { sequence: "1"; context: Qt.ApplicationShortcut; enabled: !win.blocked; onActivated: win.tab = 0 }
    Shortcut { sequence: "2"; context: Qt.ApplicationShortcut; enabled: !win.blocked; onActivated: win.tab = 1 }
    Shortcut { sequence: "3"; context: Qt.ApplicationShortcut; enabled: !win.blocked; onActivated: win.tab = 2 }
    Shortcut { sequence: "C"; context: Qt.ApplicationShortcut; enabled: !win.blocked; onActivated: progressPane.askStop() }
    Shortcut { sequence: "R"; context: Qt.ApplicationShortcut; enabled: !win.blocked; onActivated: backend.retryJob(backend.selectedIndex) }
    Shortcut { sequences: ["Return", "Enter"]; context: Qt.ApplicationShortcut; enabled: !win.blocked; onActivated: backend.openOutput(backend.selectedIndex) }
    Shortcut { sequence: "F"; context: Qt.ApplicationShortcut; enabled: !win.blocked; onActivated: backend.revealOutput(backend.selectedIndex) }
    Shortcut { sequence: "V"; context: Qt.ApplicationShortcut; enabled: !win.blocked; onActivated: { win.tab = 0; progressPane.cyclePreview(); } }
    Shortcut { sequence: "X"; context: Qt.ApplicationShortcut; enabled: !win.blocked; onActivated: backend.clearFinished() }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Top bar
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 58
            color: theme.surface

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 12
                spacing: 10

                Text { text: "Encodr"; color: theme.foreground; font.pixelSize: 20; font.bold: true }
                Text {
                    text: backend.jobs.length === 0 ? "" :
                          backend.jobs.length + " file" + (backend.jobs.length === 1 ? "" : "s") + " · "
                          + backend.doneCount + " done · " + backend.waitingCount + " waiting"
                    color: theme.dim
                    font.pixelSize: 13
                }
                Item { Layout.fillWidth: true }
                Button { text: "Add files  (O)"; onClicked: backend.addFilesDialog() }
                Button {
                    text: backend.paused ? "Resume  (Space)" : backend.hasActiveWork ? "Pause  (Space)" : "Start  (Space)"
                    highlighted: !backend.hasActiveWork || backend.paused
                    enabled: backend.hasActiveWork || backend.waitingCount > 0
                    onClicked: backend.toggleQueue()
                }
                ToolButton { text: "?"; font.bold: true; onClicked: win.helpVisible = true }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // Queue
            Rectangle {
                Layout.fillHeight: true
                Layout.preferredWidth: 340
                color: theme.surface

                ListView {
                    id: queueList
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 8
                    clip: true
                    model: backend.jobs
                    currentIndex: backend.selectedIndex

                    delegate: Rectangle {
                        id: card
                        required property var modelData
                        required property int index
                        readonly property var j: modelData
                        width: queueList.width
                        implicitHeight: cardBody.implicitHeight + 20
                        radius: 8
                        color: index === backend.selectedIndex ? theme.raised : "transparent"
                        border.width: index === backend.selectedIndex ? 1 : 0
                        border.color: theme.accent

                        MouseArea {
                            anchors.fill: parent
                            onClicked: backend.selectedIndex = card.index
                            onDoubleClicked: backend.openOutput(card.index)
                        }

                        ColumnLayout {
                            id: cardBody
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.margins: 10
                            spacing: 4

                            RowLayout {
                                Layout.fillWidth: true
                                Text {
                                    text: card.j.fileName
                                    color: theme.foreground
                                    font.pixelSize: 14
                                    font.bold: true
                                    elide: Text.ElideMiddle
                                    Layout.fillWidth: true
                                }
                                Text {
                                    text: Format.stateLabel(card.j.state)
                                    font.pixelSize: 11
                                    font.bold: true
                                    color: card.j.state === "failed" ? theme.bad : card.j.state === "done" ? theme.good
                                         : card.j.state === "running" ? theme.accent : card.j.state === "paused" ? theme.warn : theme.dim
                                }
                            }
                            Text {
                                text: card.j.preset.name + (card.j.info.summary ? " · " + card.j.info.summary : "")
                                color: theme.dim
                                font.pixelSize: 11
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                            ProgressBar {
                                Layout.fillWidth: true
                                visible: card.j.state === "running" || card.j.state === "paused" || card.j.state === "done"
                                from: 0; to: 100
                                value: card.j.percent
                            }
                            Text {
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                                font.pixelSize: 11
                                color: card.j.state === "failed" ? theme.bad : theme.dim
                                text: {
                                    var s = card.j.state;
                                    if (s === "running")
                                        return Format.number(card.j.percent, 1) + "% · " + Format.words(card.j.remaining) + " left · " + Format.number(card.j.speed, 2) + "×";
                                    if (s === "paused")
                                        return Format.number(card.j.percent, 1) + "% · paused";
                                    if (s === "done")
                                        return Format.bytes(card.j.projectedSize) + " · " + Format.words(card.j.elapsed);
                                    if (s === "ready")
                                        return Format.duration(card.j.info.duration) + " · " + Format.bytes(card.j.info.size);
                                    return card.j.message.split("\n")[0];
                                }
                            }
                        }
                    }

                    ColumnLayout {
                        anchors.centerIn: parent
                        visible: backend.jobs.length === 0
                        spacing: 10
                        width: parent.width - 40
                        Text { text: "Drop video or audio files here"; color: theme.foreground; font.pixelSize: 16; font.bold: true; Layout.alignment: Qt.AlignHCenter }
                        Text { text: "or press O to choose them"; color: theme.dim; font.pixelSize: 13; Layout.alignment: Qt.AlignHCenter }
                    }
                }
            }

            // Details
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 0

                TabBar {
                    id: tabs
                    Layout.fillWidth: true
                    currentIndex: win.tab
                    onCurrentIndexChanged: win.tab = currentIndex
                    TabButton { text: "Progress  1" }
                    TabButton { text: "Source  2" }
                    TabButton { text: "Settings  3" }
                }

                StackLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.margins: 14
                    currentIndex: win.tab
                    visible: win.job !== null

                    ProgressPane { id: progressPane; job: win.job; jobIndex: backend.selectedIndex }
                    SourcePane { job: win.job }
                    SettingsPane { job: win.job; jobIndex: backend.selectedIndex }
                }
                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: win.job === null
                    MachinePanel { anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; anchors.margins: 14; tel: backend.telemetry }
                }
            }
        }

        // Status bar
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 34
            color: theme.surface

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 16
                spacing: 18

                Text {
                    color: backend.notice !== "" ? theme.warn : theme.dim
                    font.pixelSize: 12
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
                Text {
                    visible: backend.inhibiting
                    text: "Sleep blocked while converting"
                    color: theme.dim
                    font.pixelSize: 12
                }
                Text {
                    text: "CPU " + Format.number(backend.telemetry.cpuTotal, 0) + "%"
                          + (backend.telemetry.systemWatts > 0 ? " · " + Format.number(backend.telemetry.systemWatts, 1) + " W" : "")
                          + (backend.telemetry.hottest > 0 ? " · " + Format.number(backend.telemetry.hottest, 0) + " °C" : "")
                    color: backend.telemetry.throttled ? theme.bad : theme.dim
                    font.pixelSize: 12
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
            border.width: 3
            Text { anchors.centerIn: parent; text: "Drop to add to the queue"; color: theme.foreground; font.pixelSize: 22; font.bold: true }
        }
    }

    HelpOverlay { anchors.fill: parent; visible: win.helpVisible; onClose: win.helpVisible = false }

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
