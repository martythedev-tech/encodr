import QtQuick
import QtQuick.Layouts
import "Format.js" as Format

// The computer doing the work: every core, clock speeds against their top
// speed, and whatever temperature and power sensors the kernel offers.
Rectangle {
    id: root
    property var tel
    color: theme.surface
    radius: 10
    implicitHeight: body.implicitHeight + 28

    ColumnLayout {
        id: body
        anchors.fill: parent
        anchors.margins: 14
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            Text { text: "THIS COMPUTER"; color: theme.dim; font.pixelSize: 11; font.bold: true; font.letterSpacing: 1 }
            Item { Layout.fillWidth: true }
            Rectangle {
                visible: tel.throttled
                color: theme.bad
                radius: 4
                implicitWidth: throttleText.implicitWidth + 12
                implicitHeight: 20
                Text { id: throttleText; anchors.centerIn: parent; text: "SLOWED BY HEAT"; color: theme.background; font.pixelSize: 10; font.bold: true }
            }
            Text { text: "CPU " + Format.number(tel.cpuTotal, 0) + "%"; color: theme.foreground; font.pixelSize: 13; font.bold: true }
        }

        // One bar per core.
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 64
            spacing: 6
            Repeater {
                model: tel.cores
                ColumnLayout {
                    required property var modelData
                    required property int index
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: 3
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        color: theme.raised
                        radius: 3
                        Rectangle {
                            anchors.bottom: parent.bottom
                            width: parent.width
                            height: parent.height * Math.min(1, modelData / 100)
                            radius: 3
                            color: modelData > 85 ? theme.warn : theme.accent
                        }
                    }
                    Text {
                        Layout.alignment: Qt.AlignHCenter
                        text: Format.number(modelData, 0)
                        color: theme.dim
                        font.pixelSize: 10
                    }
                }
            }
        }

        GridLayout {
            Layout.fillWidth: true
            columns: 3
            columnSpacing: 24
            rowSpacing: 4

            ColumnLayout {
                Layout.alignment: Qt.AlignTop
                spacing: 3
                Text { text: "Clock speed"; color: theme.dim; font.pixelSize: 11 }
                Repeater {
                    model: tel.clocks
                    Text {
                        required property var modelData
                        text: "CPUs " + modelData.cpus + ": " + Format.number(modelData.mhz / 1000, 2) + " / "
                              + Format.number(modelData.maxMhz / 1000, 2) + " GHz"
                        color: modelData.mhz < modelData.maxMhz * 0.85 && tel.cpuTotal > 60 ? theme.warn : theme.foreground
                        font.pixelSize: 12
                    }
                }
            }
            ColumnLayout {
                Layout.alignment: Qt.AlignTop
                spacing: 3
                Text { text: "Temperatures"; color: theme.dim; font.pixelSize: 11 }
                Text { visible: tel.temperatures.length === 0; text: "No sensors exposed"; color: theme.dim; font.pixelSize: 12 }
                Repeater {
                    model: tel.temperatures
                    Text {
                        required property var modelData
                        text: modelData.label + ": " + Format.number(modelData.celsius, 0) + " °C"
                        color: modelData.celsius >= 85 ? theme.bad : modelData.celsius >= 70 ? theme.warn : theme.foreground
                        font.pixelSize: 12
                    }
                }
            }
            ColumnLayout {
                Layout.alignment: Qt.AlignTop
                spacing: 3
                Text { text: "Power"; color: theme.dim; font.pixelSize: 11 }
                Text { visible: tel.power.length === 0; text: "No sensors exposed"; color: theme.dim; font.pixelSize: 12 }
                Repeater {
                    model: tel.power
                    Text {
                        required property var modelData
                        text: modelData.label + ": " + Format.number(modelData.watts, 1) + " W"
                        color: theme.foreground
                        font.pixelSize: 12
                    }
                }
            }
        }
    }
}
