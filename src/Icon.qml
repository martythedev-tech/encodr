import QtQuick

// Small line icons, drawn from SVG in the theme's colours, so they look the
// same everywhere instead of depending on an icon font being installed.
Image {
    id: root
    property string name: "file"
    property color color: theme.dim
    property real size: 14

    readonly property var shapes: ({
        home: '<path d="M3 11 12 3l9 8"/><path d="M5 9.5V20h5v-6h4v6h5V9.5"/>',
        folder: '<path d="M3 7a2 2 0 0 1 2-2h4l2 2h8a2 2 0 0 1 2 2v8a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2z"/>',
        drive: '<path d="M3 14 6 6h12l3 8"/><rect x="3" y="14" width="18" height="6" rx="1.5"/><path d="M7 17h.01"/>',
        computer: '<rect x="3" y="4" width="18" height="12" rx="1.5"/><path d="M8 20h8M12 16v4"/>',
        film: '<rect x="3" y="4" width="18" height="16" rx="1.5"/><path d="M7 4v16M17 4v16M3 9h4M3 15h4M17 9h4M17 15h4"/>',
        music: '<path d="M9 18V5l11-2v13"/><circle cx="6" cy="18" r="3"/><circle cx="17" cy="16" r="3"/>',
        file: '<path d="M6 3h8l4 4v14H6z"/><path d="M14 3v4h4"/>',
        up: '<path d="M12 19V5M5 12l7-7 7 7"/>',
        check: '<path d="m5 12 5 5 9-10"/>'
    })

    width: size
    height: size
    sourceSize: Qt.size(size * 2, size * 2)
    smooth: true
    source: "data:image/svg+xml," + encodeURIComponent(
        '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="' + color
        + '" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">' + (shapes[name] || shapes.file) + '</svg>')
}
