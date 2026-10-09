import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The one button style: compact and flat with small corners. `kind` is
// "primary" (accent fill, the main action), "default" (outlined) or "ghost"
// (text only). `hint` shows the keyboard shortcut as a small keycap, and
// `selected` marks the active choice in a group.
Button {
    id: control
    property string kind: "default"
    property string hint: ""
    property bool danger: false
    property bool selected: false

    implicitHeight: 28
    // Material pads every button with invisible margins for touch targets.
    topInset: 0
    bottomInset: 0
    leftInset: 0
    rightInset: 0
    leftPadding: 10
    rightPadding: hint !== "" ? 6 : 10
    topPadding: 0
    bottomPadding: 0
    font.pixelSize: 12
    font.weight: Font.Medium
    hoverEnabled: true
    // Buttons never take the keyboard, so Space and Enter stay window shortcuts.
    focusPolicy: Qt.NoFocus

    readonly property color fg: kind === "primary" ? theme.accentForeground
                              : danger ? theme.bad
                              : kind === "ghost" && !selected && !hovered ? theme.dim : theme.foreground

    contentItem: RowLayout {
        spacing: 8
        opacity: control.enabled ? 1 : 0.4
        Text {
            text: control.text
            color: control.fg
            font: control.font
            verticalAlignment: Text.AlignVCenter
            Layout.alignment: Qt.AlignVCenter
        }
        Rectangle {
            visible: control.hint !== ""
            Layout.alignment: Qt.AlignVCenter
            implicitWidth: Math.max(16, hintText.implicitWidth + 8)
            implicitHeight: 16
            radius: 3
            color: "transparent"
            border.width: 1
            border.color: control.kind === "primary" ? Qt.rgba(control.fg.r, control.fg.g, control.fg.b, 0.3) : theme.border
            Text {
                id: hintText
                anchors.centerIn: parent
                text: control.hint
                color: control.fg
                opacity: 0.65
                font.pixelSize: 10
                font.family: theme.mono
            }
        }
    }

    background: Rectangle {
        radius: 4
        color: {
            if (control.kind === "primary")
                return control.down ? Qt.darker(theme.accent, 1.15) : control.hovered ? Qt.lighter(theme.accent, 1.08) : theme.accent;
            if (control.selected)
                return theme.raised;
            if (control.down || control.hovered)
                return theme.hover;
            return control.kind === "ghost" ? "transparent" : theme.surface;
        }
        border.width: control.kind === "default" ? 1 : 0
        border.color: theme.border
        opacity: control.enabled ? 1 : 0.6
    }
}
