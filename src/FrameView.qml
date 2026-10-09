import QtQuick

// Shows a JPEG that ffmpeg keeps rewriting. Each new frame loads into a
// hidden image and only replaces the visible one once it has fully decoded,
// so the picture never blanks or flickers between updates.
Item {
    id: root
    property string path: ""
    property int stamp: 0
    readonly property bool ready: front.status === Image.Ready || back.status === Image.Ready
    property bool showingA: true

    readonly property Image front: showingA ? a : b
    readonly property Image back: showingA ? b : a

    function load() {
        // Stamp 0 means no frame has been written yet.
        if (path === "" || stamp === 0)
            return;
        back.source = "file://" + path + "?" + stamp;
    }
    onStampChanged: load()
    onPathChanged: load()
    Component.onCompleted: load()

    Image {
        id: a
        anchors.fill: parent
        fillMode: Image.PreserveAspectFit
        cache: false
        asynchronous: true
        smooth: true
        visible: root.showingA
        onStatusChanged: if (status === Image.Ready && !root.showingA) root.showingA = true
    }
    Image {
        id: b
        anchors.fill: parent
        fillMode: Image.PreserveAspectFit
        cache: false
        asynchronous: true
        smooth: true
        visible: !root.showingA
        onStatusChanged: if (status === Image.Ready && root.showingA) root.showingA = false
    }
}
