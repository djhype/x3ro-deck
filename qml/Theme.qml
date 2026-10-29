import QtQuick

// Synthwave palette from the another-hook tmux profile, tuned for contrast.
QtObject {
    readonly property color bg0: "#0a0a16"
    readonly property color bg1: "#0d0d1f"
    readonly property color panel: "#11112a"
    readonly property color raised: "#14142e"
    readonly property color selected: "#1c1c44"
    readonly property color line: "#22224a"
    readonly property color line2: "#33335e"
    readonly property color text: "#e6e6f5"
    readonly property color soft: "#c8c8e8"
    readonly property color dim: "#a0a0c8"
    readonly property color magenta: "#ff6ff2"
    readonly property color pink: "#ff5c7a"
    readonly property color cyan: "#00fff5"
    readonly property color amber: "#f5a623"
    readonly property color ink: "#1a1a2e"

    readonly property string sans: "Fira Sans"
    readonly property string mono: "MesloLGS Nerd Font Mono"
    readonly property string display: "Oxanium"

    function agentColor(state) {
        switch (state) {
        case "working": return cyan
        case "permission": case "question": return amber
        case "done": return magenta
        case "ready": return soft
        default: return dim
        }
    }
    function agentLabel(state) {
        switch (state) {
        case "working": return "Working"
        case "permission": return "Needs permission"
        case "question": return "Has a question"
        case "done": return "Done · waiting"
        case "ready": return "Ready"
        default: return "No agent"
        }
    }
    function healthColor(state) {
        switch (state) {
        case "ok": case "healthy": case "running": return cyan
        case "warn": case "starting": return pink
        case "bad": case "unhealthy": case "exited": case "restarting": return amber
        default: return dim
        }
    }
    function elapsed(ms, now) {
        if (!ms) return "—"
        var s = Math.max(0, Math.floor((now - ms) / 1000))
        var h = Math.floor(s / 3600), m = Math.floor((s % 3600) / 60), sec = s % 60
        if (h > 0) return h + "h " + (m < 10 ? "0" : "") + m + "m"
        if (m > 0) return m + "m " + (sec < 10 ? "0" : "") + sec + "s"
        return sec + "s"
    }
}
