// SPDX-FileCopyrightText: 2026 x3ro
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick

// A small glyph button on a quick command: pin or hide.
Rectangle {
    id: g
    property string glyph
    property bool active: false
    signal clicked()
    readonly property Theme t: Theme {}

    width: 18; height: 18; radius: 5
    color: mouse.containsMouse ? t.selected : "transparent"
    Text {
        anchors.centerIn: parent
        text: g.glyph
        color: g.active || mouse.containsMouse ? g.t.magenta : g.t.dim
        font.family: g.t.sans; font.pixelSize: 12
    }
    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: g.clicked()
    }
}
