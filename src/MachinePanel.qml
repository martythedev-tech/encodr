import QtQuick
import QtQuick.Layouts
import "Format.js" as Format

// The computer doing the work: every core, clock speeds against their top
// speed, and whatever temperature and power sensors the kernel offers.
Rectangle {
    id: root
    property var tel
    color: theme.surface
    radius: 6
    border.width: 1
    border.color: theme.border
    implicitHeight: body.implicitHeight + 28

    ColumnLayout {
        id: body
        anchors.fill: parent
        anchors.margins: 14
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            Text { text: "THIS COMPUTER"; color: theme.dim; font.pixelSize: 11; font.weight: Font.DemiBold; font.letterSpacing: 0.8 }
            Item { Layout.fillWidth: true }
            Rectangle {
                visible: tel.throttled
                color: "transparent"
                border.width: 1
                border.color: theme.bad
                radius: 3
                implicitWidth: throttleText.implicitWidth + 12
                implicitHeight: 18
                Text { id: throttleText; anchors.centerIn: parent; text: tel.clocksMeasured ? "SLOWED BY HEAT · " + Math.round(tel.clockShare * 100) + "% SPEED" : "SLOWED BY HEAT"; color: theme.bad; font.pixelSize: 10; font.weight: Font.DemiBold; font.letterSpacing: 0.5 }
            }
            Text { text: "CPU " + Format.number(tel.cpuTotal, 0) + "%"; color: theme.foreground; font.pixelSize: 12; font.family: theme.mono }
        }

        // One bar per core.
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 52
            spacing: 4
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
                        radius: 2
                        Rectangle {
                            anchors.bottom: parent.bottom
                            width: parent.width
                            height: parent.height * Math.min(1, modelData / 100)
                            radius: 2
                            color: theme.accent
                            opacity: 0.35 + 0.65 * Math.min(1, modelData / 100)
                        }
                    }
                    Text {
                        Layout.alignment: Qt.AlignHCenter
                        text: Format.number(modelData, 0)
                        color: theme.dim
                        font.pixelSize: 10
                        font.family: theme.mono
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
                Text { text: "Clock speed"; color: theme.dim; font.pixelSize: 10; font.weight: Font.DemiBold; font.letterSpacing: 0.7; font.capitalization: Font.AllUppercase; Layout.bottomMargin: 2 }
                Repeater {
                    model: tel.clocks
                    Text {
                        required property var modelData
                        text: "CPUs " + modelData.cpus + ": " + Format.number(modelData.mhz / 1000, 2) + " / "
                              + Format.number(modelData.maxMhz / 1000, 2) + " GHz" + (modelData.measured ? "  (measured)" : "")
                        color: modelData.low ? theme.warn : theme.foreground
                        font.pixelSize: 12
                        font.family: theme.mono
                    }
                }
            }
            ColumnLayout {
                Layout.alignment: Qt.AlignTop
                spacing: 3
                Text { text: "Temperatures"; color: theme.dim; font.pixelSize: 10; font.weight: Font.DemiBold; font.letterSpacing: 0.7; font.capitalization: Font.AllUppercase; Layout.bottomMargin: 2 }
                Text { visible: tel.temperatures.length === 0; text: "No sensors exposed"; color: theme.dim; font.pixelSize: 12 }
                Repeater {
                    model: tel.temperatures
                    Text {
                        required property var modelData
                        text: modelData.label + ": " + Format.number(modelData.celsius, 0) + " °C"
                        color: modelData.celsius >= 85 ? theme.bad : modelData.celsius >= 70 ? theme.warn : theme.foreground
                        font.pixelSize: 12
                        font.family: theme.mono
                    }
                }
            }
            ColumnLayout {
                Layout.alignment: Qt.AlignTop
                spacing: 3
                Text { text: "Power"; color: theme.dim; font.pixelSize: 10; font.weight: Font.DemiBold; font.letterSpacing: 0.7; font.capitalization: Font.AllUppercase; Layout.bottomMargin: 2 }
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
