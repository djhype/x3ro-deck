// SPDX-FileCopyrightText: 2026 x3ro
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick

// Button in the mockup's style: "solid", "outline" (accent border) or "ghost" (neutral).
Rectangle {
    id: btn
    property string text
    property string subtext
    property color accent: t.magenta
    property string variant: "ghost"
    property bool mono: false
    property int reserve: 0 // room kept free on the right, for controls laid over the button
    property bool enabledState: true
    signal clicked()
    readonly property Theme t: Theme {}

    implicitWidth: Math.max(content.implicitWidth + 28, 44)
    implicitHeight: subtext ? 52 : 38
    radius: 9
    opacity: enabledState ? 1 : 0.4
    color: variant === "solid" ? accent
         : variant === "outline" ? Qt.rgba(accent.r, accent.g, accent.b, mouse.containsMouse ? 0.18 : 0.08)
         : (mouse.containsMouse ? t.selected : t.raised)
    border.width: variant === "solid" ? 0 : 1
    border.color: variant === "outline" ? accent : (mouse.containsMouse ? t.line2 : t.line)
    Behavior on color { ColorAnimation { duration: 120 } }

    Column {
        id: content
        x: btn.subtext ? 12 : (btn.width - width) / 2
        anchors.verticalCenter: parent.verticalCenter
        spacing: 2
        Text {
            text: btn.text
            color: btn.variant === "solid" ? btn.t.ink : (btn.variant === "outline" ? btn.accent : btn.t.text)
            font.family: btn.mono ? btn.t.mono : btn.t.sans
            font.pixelSize: 13
            font.weight: btn.variant === "ghost" ? Font.Normal : Font.DemiBold
            elide: Text.ElideRight
            width: btn.subtext ? Math.min(implicitWidth, btn.width - 24 - btn.reserve) : implicitWidth
        }
        Text {
            visible: !!btn.subtext
            text: btn.subtext
            color: btn.t.dim
            font.family: btn.t.mono
            font.pixelSize: 11
            elide: Text.ElideRight
            width: Math.min(implicitWidth, btn.width - 24 - btn.reserve)
        }
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        enabled: btn.enabledState
        onClicked: btn.clicked()
    }
}
