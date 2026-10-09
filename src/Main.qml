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
    Material.roundedScale: Material.ExtraSmallScale
    Material.containerStyle: Material.Outlined

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

    component Hairline: Rectangle { color: theme.border; implicitHeight: 1; implicitWidth: 1 }

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

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // Queue
            Rectangle {
                Layout.fillHeight: true
                Layout.preferredWidth: 300
                color: theme.surface

                Text {
                    id: queueTitle
                    x: 16
                    y: 14
                    text: "QUEUE"
                    color: theme.dim
                    font.pixelSize: 11
                    font.weight: Font.DemiBold
                    font.letterSpacing: 0.8
                }

                ListView {
                    id: queueList
                    anchors.fill: parent
                    anchors.topMargin: 36
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8
                    anchors.bottomMargin: 8
                    spacing: 2
                    clip: true
                    model: backend.jobs
                    currentIndex: backend.selectedIndex

                    delegate: Rectangle {
                        id: card
                        required property var modelData
                        required property int index
                        readonly property var j: modelData
                        readonly property bool selected: index === backend.selectedIndex
                        readonly property color stateColor: j.state === "failed" ? theme.bad : j.state === "done" ? theme.good
                                                          : j.state === "running" ? theme.accent : j.state === "paused" ? theme.warn : theme.dim
                        width: queueList.width
                        implicitHeight: cardBody.implicitHeight + 18
                        radius: 4
                        color: selected ? theme.raised : cardMouse.containsMouse ? theme.hover : "transparent"

                        Rectangle {
                            visible: card.selected
                            width: 2
                            height: parent.height - 12
                            anchors.verticalCenter: parent.verticalCenter
                            radius: 1
                            color: theme.accent
                        }

                        MouseArea {
                            id: cardMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: backend.selectedIndex = card.index
                            onDoubleClicked: backend.openOutput(card.index)
                        }

                        ColumnLayout {
                            id: cardBody
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.leftMargin: 12
                            anchors.rightMargin: 10
                            anchors.topMargin: 9
                            spacing: 3

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 8
                                Text {
                                    text: card.j.fileName
                                    color: theme.foreground
                                    font.pixelSize: 13
                                    font.weight: Font.Medium
                                    elide: Text.ElideMiddle
                                    Layout.fillWidth: true
                                }
                                Rectangle { implicitWidth: 6; implicitHeight: 6; radius: 3; color: card.stateColor }
                                Text {
                                    text: Format.stateLabel(card.j.state)
                                    font.pixelSize: 11
                                    color: card.stateColor
                                }
                            }
                            Text {
                                text: card.j.preset.name + (card.j.info.summary ? " · " + card.j.info.summary : "")
                                color: theme.dim
                                font.pixelSize: 11
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                            Meter {
                                Layout.fillWidth: true
                                Layout.topMargin: 3
                                Layout.bottomMargin: 1
                                visible: card.j.state === "running" || card.j.state === "paused" || card.j.state === "done"
                                value: card.j.percent
                                barColor: card.stateColor
                            }
                            Text {
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                                font.pixelSize: 11
                                font.features: { "tnum": 1 }
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
                        spacing: 6
                        width: parent.width - 40
                        Text { text: "Drop video or audio files here"; color: theme.foreground; font.pixelSize: 13; font.weight: Font.Medium; Layout.alignment: Qt.AlignHCenter }
                        Text { text: "or press O to choose them"; color: theme.dim; font.pixelSize: 12; Layout.alignment: Qt.AlignHCenter }
                    }
                }
            }
            Hairline { Layout.fillHeight: true }

            // Details
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 0

                TabStrip {
                    Layout.fillWidth: true
                    labels: ["Progress", "Source", "Settings"]
                    current: win.tab
                    onPicked: function (index) { win.tab = index; }
                }

                StackLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.margins: 16
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
                    MachinePanel { anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; anchors.margins: 16; tel: backend.telemetry }
                }
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
