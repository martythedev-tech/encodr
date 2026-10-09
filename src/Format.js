.pragma library

function duration(seconds) {
    if (seconds === undefined || seconds === null || seconds < 0 || !isFinite(seconds))
        return "—";
    var s = Math.round(seconds);
    var h = Math.floor(s / 3600);
    var m = Math.floor((s % 3600) / 60);
    var r = s % 60;
    var mm = (h > 0 && m < 10 ? "0" : "") + m;
    var ss = (r < 10 ? "0" : "") + r;
    return h > 0 ? h + ":" + mm + ":" + ss : mm + ":" + ss;
}

// "1 h 25 min", "4 min 10 s", "12 s"
function words(seconds) {
    if (seconds === undefined || seconds === null || seconds < 0 || !isFinite(seconds))
        return "—";
    var s = Math.round(seconds);
    var h = Math.floor(s / 3600);
    var m = Math.floor((s % 3600) / 60);
    if (h > 0)
        return h + " h " + m + " min";
    if (m > 0)
        return m + " min " + (s % 60) + " s";
    return s + " s";
}

function bytes(n) {
    if (n === undefined || n === null || n <= 0 || !isFinite(n))
        return "—";
    var units = ["B", "KB", "MB", "GB", "TB"];
    var i = 0;
    var v = n;
    while (v >= 1000 && i < units.length - 1) {
        v /= 1000;
        i++;
    }
    return (v >= 100 || i === 0 ? v.toFixed(0) : v >= 10 ? v.toFixed(1) : v.toFixed(2)) + " " + units[i];
}

function bitrate(kbps) {
    if (kbps === undefined || kbps === null || kbps <= 0 || !isFinite(kbps))
        return "—";
    return kbps >= 1000 ? (kbps / 1000).toFixed(kbps >= 10000 ? 1 : 2) + " Mbps" : kbps.toFixed(0) + " kbps";
}

function bps(bits) {
    return bitrate(bits / 1000);
}

function clock(date) {
    if (!date || isNaN(date.getTime()))
        return "—";
    var now = new Date();
    var text = date.toLocaleTimeString(Qt.locale(), "h:mm AP");
    if (date.toDateString() !== now.toDateString())
        text = date.toLocaleDateString(Qt.locale(), "ddd") + " " + text;
    return text;
}

function number(n, digits) {
    if (n === undefined || n === null || !isFinite(n))
        return "—";
    return Number(n).toFixed(digits === undefined ? 0 : digits);
}

function thousands(n) {
    if (n === undefined || n === null || !isFinite(n))
        return "—";
    return Math.round(n).toString().replace(/\B(?=(\d{3})+(?!\d))/g, ",");
}

function fileName(path) {
    if (!path)
        return "";
    return path.substring(path.lastIndexOf("/") + 1);
}

function folder(path) {
    if (!path)
        return "";
    var home = path.indexOf("/home/") === 0 ? path.replace(/^\/home\/[^\/]+/, "~") : path;
    return home;
}

function stateLabel(state) {
    return {
        "probing": "Reading",
        "ready": "Waiting",
        "running": "Converting",
        "paused": "Paused",
        "done": "Done",
        "failed": "Failed",
        "cancelled": "Stopped"
    }[state] || state;
}
