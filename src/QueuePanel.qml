import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "Format.js" as Format

// The files waiting, converting and done, in the order they'll run.
Dock {
    id: root
    title: "Queue"
    header: [
        Text {
            text: backend.jobs.length === 0 ? "" : backend.doneCount + " of " + backend.jobs.length + " done"
            color: theme.dim
            font.pixelSize: 11
        },
        Item { Layout.fillWidth: true },
        UiButton {
            visible: backend.joinableCount >= 2
            text: "Join " + backend.joinableCount; kind: "ghost"
            implicitHeight: 22
            onClicked: backend.joinWaiting()
        },
        UiButton {
            visible: backend.doneCount > 0
            text: "Clear done"; hint: "X"; kind: "ghost"
            implicitHeight: 22
            onClicked: backend.clearFinished()
        }
    ]

    ListView {
        id: queueList
        anchors.fill: parent
        anchors.margins: 6
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
                        text: card.j.fileName + (card.j.clipCount > 1 ? " + " + (card.j.clipCount - 1) + " more" : "")
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
                    text: card.j.preset.name + (card.j.clipCount > 1 ? " · " + card.j.clipCount + " clips joined"
                                               : card.j.info.summary ? " · " + card.j.info.summary : "")
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
                            return Format.duration(card.j.totalDuration) + " · " + Format.bytes(card.j.totalSize);
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
            Text { text: "Nothing queued yet"; color: theme.foreground; font.pixelSize: 13; font.weight: Font.Medium; Layout.alignment: Qt.AlignHCenter }
            Text { text: "Double-click files in the browser, drop them here, or press O"; color: theme.dim; font.pixelSize: 12; Layout.alignment: Qt.AlignHCenter; Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.Wrap }
        }
    }
}
