// SPDX-FileCopyrightText: 2026 x3ro
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick

// Color and font tokens. Values come from the theme JSON (data/themes);
// the literals are Cyberdecker's, for when no theme is loaded.
QtObject {
    readonly property color bg0: c("bg0", "#0a0a16")
    readonly property color bg1: c("bg1", "#0d0d1f")
    readonly property color panel: c("panel", "#11112a")
    readonly property color raised: c("raised", "#14142e")
    readonly property color selected: c("selected", "#1c1c44")
    readonly property color line: c("line", "#22224a")
    readonly property color line2: c("line2", "#33335e")
    readonly property color text: c("text", "#e6e6f5")
    readonly property color soft: c("soft", "#c8c8e8")
    readonly property color dim: c("dim", "#a0a0c8")
    readonly property color magenta: c("magenta", "#ff6ff2")
    readonly property color pink: c("pink", "#ff5c7a")
    readonly property color cyan: c("cyan", "#00fff5")
    readonly property color amber: c("amber", "#f5a623")
    readonly property color ink: c("ink", "#1a1a2e")
    readonly property color rowLine: c("rowLine", "#1a1a3a")
    readonly property color shade: c("shade", "#0a0a12")
    readonly property color tabActive: c("tabActive", "#2a1640")
    readonly property color permissionCard: c("permissionCard", "#15120a")
    readonly property color dockerCard: c("dockerCard", "#170b10")
    readonly property color doneCard: c("doneCard", "#140d18")
    readonly property var checkoutColors: typeof appTheme !== "undefined" && appTheme.checkoutColors.length
                                          ? appTheme.checkoutColors
                                          : ["#7aa2ff", "#9ece6a", "#bb9af7", "#7dcfff", "#e0af68", "#f7768e"]

    readonly property string sans: font("sans", "Fira Sans")
    readonly property string mono: font("mono", "MesloLGS Nerd Font Mono")
    readonly property string display: font("display", "Oxanium")

    // The active theme's value (AppTheme, the context property `appTheme`),
    // or the Cyberdecker default where it is unset (tst_qml has no theme).
    function c(token, fallback) {
        return typeof appTheme !== "undefined" && appTheme.colors[token] ? appTheme.colors[token] : fallback
    }
    function font(name, fallback) {
        return typeof appTheme !== "undefined" && appTheme.fonts[name] ? appTheme.fonts[name] : fallback
    }
    function alpha(color, a) {
        return Qt.rgba(color.r, color.g, color.b, a)
    }

    function agentColor(state) {
        switch (state) {
        case "working": return cyan
        case "permission": case "question": return amber
        case "done": return magenta
        case "ready": return soft
        default: return dim
        }
    }
    // A quick command's accent. check, env and data keep their colours; any
    // other group gets a stable one from the checkout palette.
    function groupColor(group) {
        switch (group) {
        case "check": case "": case undefined: return cyan
        case "env": return magenta
        case "data": return amber
        }
        var h = 0
        for (var i = 0; i < group.length; ++i) h = (h * 31 + group.charCodeAt(i)) % 9973
        return checkoutColors[h % checkoutColors.length]
    }
    function agentLabel(state) {
        switch (state) {
        case "working": return "Working"
        case "permission": return "Needs permission"
        case "question": return "Has a question"
        case "done": return "Done · waiting"
        case "ready": return "Ready"
        default: return "Offline"
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
