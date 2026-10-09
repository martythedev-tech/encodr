import QtQuick
import QtQuick.Layouts

// A live line graph over the whole run: values arrive oldest first.
Rectangle {
    id: root
    property var values: []
    property color lineColor: theme.accent
    property string title: ""
    property string current: ""
    property real floor: 0          // the y axis never tops out below this
    property string maxLabel: ""

    color: theme.surface
    radius: 6
    border.width: 1
    border.color: theme.border
    implicitHeight: 112

    readonly property real peak: {
        var m = floor;
        for (var i = 0; i < values.length; i++)
            m = Math.max(m, values[i]);
        return m;
    }

    onValuesChanged: canvas.requestPaint()
    onLineColorChanged: canvas.requestPaint()

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 6

        RowLayout {
            Layout.fillWidth: true
            Text { text: root.title; color: theme.dim; font.pixelSize: 11 }
            Item { Layout.fillWidth: true }
            Text { text: root.current; color: theme.foreground; font.pixelSize: 12; font.weight: Font.Medium; font.features: { "tnum": 1 } }
        }

        Canvas {
            id: canvas
            Layout.fillWidth: true
            Layout.fillHeight: true
            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()

            onPaint: {
                var ctx = getContext("2d");
                ctx.reset();
                var v = root.values;
                var w = width, h = height;
                ctx.strokeStyle = theme.border;
                ctx.lineWidth = 1;
                for (var g = 1; g < 4; g++) {
                    ctx.beginPath();
                    ctx.moveTo(0, Math.round(h * g / 4) + 0.5);
                    ctx.lineTo(w, Math.round(h * g / 4) + 0.5);
                    ctx.stroke();
                }
                if (v.length < 2 || root.peak <= 0)
                    return;
                var top = root.peak * 1.1;
                var step = w / (v.length - 1);
                ctx.beginPath();
                ctx.moveTo(0, h - v[0] / top * h);
                for (var i = 1; i < v.length; i++)
                    ctx.lineTo(i * step, h - v[i] / top * h);
                ctx.strokeStyle = root.lineColor;
                ctx.lineWidth = 1.5;
                ctx.lineJoin = "round";
                ctx.stroke();
                ctx.lineTo(w, h);
                ctx.lineTo(0, h);
                ctx.closePath();
                var gradient = ctx.createLinearGradient(0, 0, 0, h);
                gradient.addColorStop(0, Qt.rgba(root.lineColor.r, root.lineColor.g, root.lineColor.b, 0.16));
                gradient.addColorStop(1, Qt.rgba(root.lineColor.r, root.lineColor.g, root.lineColor.b, 0.0));
                ctx.fillStyle = gradient;
                ctx.fill();
            }

            Text {
                anchors.right: parent.right
                anchors.top: parent.top
                text: root.maxLabel
                visible: root.maxLabel !== "" && root.values.length > 1
                color: theme.dim
                font.pixelSize: 10
            }
        }
    }
}
