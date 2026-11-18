// SPDX-FileCopyrightText: 2026 x3ro
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick

Rectangle {
    id: root
    readonly property Theme t: Theme {}
    readonly property var cur: app.current
    color: t.bg0

    Column {
        anchors.left: parent.left
        anchors.leftMargin: 24
        anchors.right: actions.left
        anchors.rightMargin: 16
        anchors.verticalCenter: parent.verticalCenter
        spacing: 4
        clip: true
        Row {
            spacing: 12
            Text {
                id: nameLabel
                text: cur.name || ""
                color: t.text
                font.family: t.sans; font.pixelSize: 26; font.weight: Font.Bold
            }
            Text {
                anchors.baseline: nameLabel.baseline
                text: cur.path || ""
                color: t.dim
                font.family: t.mono; font.pixelSize: 12
            }
            Text {
                anchors.baseline: nameLabel.baseline
                visible: !!cur.project
                text: "compose: " + (cur.project || "")
                color: t.cyan
                font.family: t.mono; font.pixelSize: 12
            }
        }
        Text {
            width: parent.width
            elide: Text.ElideRight
            text: "⎇ " + (cur.branch || "") + (cur.commit ? "   ·   " + cur.commit : "")
            color: t.magenta
            font.family: t.mono; font.pixelSize: 12
        }
    }

    Row {
        id: actions
        anchors.right: parent.right
        anchors.rightMargin: 24
        anchors.verticalCenter: parent.verticalCenter
        spacing: 8
        visible: app.tab === "docker"
        NeonButton { text: "▲ Up"; variant: "outline"; accent: t.cyan; onClicked: app.runCompose("up") }
        NeonButton { text: "↻ Restart all"; onClicked: app.runCompose("restart") }
        NeonButton { text: "■ Down"; onClicked: app.runCompose("down") }
    }
}
