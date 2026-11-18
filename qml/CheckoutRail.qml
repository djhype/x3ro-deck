// SPDX-FileCopyrightText: 2026 x3ro
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick

Rectangle {
    id: root
    readonly property Theme t: Theme {}
    color: t.bg1

    Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: t.line }

    Text {
        id: heading
        x: 20; y: 18
        text: "CHECKOUTS"
        color: t.dim
        font.family: t.sans; font.pixelSize: 11; font.letterSpacing: 1.6
    }
    // The next-checkout shortcut.
    Text {
        anchors.right: parent.right; anchors.rightMargin: 20
        anchors.verticalCenter: heading.verticalCenter
        text: (app.shortcutHints || {})["next-checkout"] || ""
        color: t.dim
        font.family: t.mono; font.pixelSize: 10
        elide: Text.ElideLeft
        width: Math.min(implicitWidth, parent.width - heading.width - 56)
    }

    ListView {
        id: list
        anchors.top: heading.bottom
        anchors.topMargin: 10
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: footer.top
        anchors.margins: 12
        spacing: 8
        clip: true
        model: app.checkouts

        delegate: Rectangle {
            id: card
            required property int index
            required property string name
            required property string ticket
            required property string agentState
            required property string agentLabel
            required property string healthText
            required property string healthState
            required property bool selected
            readonly property bool needsYou: agentState === "permission" || agentState === "question"

            width: ListView.view.width
            height: col.implicitHeight + 24
            radius: 12
            color: selected ? t.selected : (mouse.containsMouse ? t.raised : t.panel)
            border.width: 1
            border.color: selected ? t.magenta : (needsYou ? t.alpha(t.amber, 0.6) : t.line)
            Behavior on color { ColorAnimation { duration: 140 } }
            Behavior on border.color { ColorAnimation { duration: 140 } }

            // Glow for the selected checkout and a pulse for one that needs you.
            Rectangle {
                anchors.fill: parent
                anchors.margins: -4
                radius: 16
                z: -1
                color: "transparent"
                border.width: 4
                border.color: card.selected ? t.alpha(t.magenta, 0.16) : t.alpha(t.amber, 0.22)
                visible: card.selected || card.needsYou
                SequentialAnimation on opacity {
                    running: card.needsYou && !card.selected
                    loops: Animation.Infinite
                    NumberAnimation { from: 1; to: 0.2; duration: 900; easing.type: Easing.InOutSine }
                    NumberAnimation { from: 0.2; to: 1; duration: 900; easing.type: Easing.InOutSine }
                }
            }

            Column {
                id: col
                x: 14; y: 12
                width: parent.width - 28
                spacing: 8
                Item {
                    width: parent.width; height: nameText.implicitHeight
                    Text {
                        id: nameText
                        text: card.name
                        color: card.selected ? t.magenta : t.text
                        font.family: t.sans; font.pixelSize: 15; font.weight: Font.DemiBold
                    }
                    Text {
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        text: (app.shortcutHints || {})["select-" + (card.index + 1)] || ""
                        color: t.dim
                        font.family: t.mono; font.pixelSize: 11
                    }
                }
                Item {
                    width: parent.width; height: ticketText.implicitHeight
                    Text {
                        id: ticketText
                        anchors.left: parent.left
                        anchors.right: agentText.left
                        anchors.rightMargin: 8
                        text: card.ticket || "—"
                        elide: Text.ElideRight
                        color: t.soft
                        font.family: t.mono; font.pixelSize: 12
                    }
                    Text {
                        id: agentText
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        text: card.agentLabel
                        color: t.dim
                        font.family: t.sans; font.pixelSize: 11
                    }
                }
                Flow {
                    width: parent.width
                    spacing: 6
                    Pill { text: t.agentLabel(card.agentState); fg: t.agentColor(card.agentState) }
                    Pill { text: card.healthText; fg: t.healthColor(card.healthState); mono: true; dot: false }
                }
            }

            MouseArea {
                id: mouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: app.select(card.index)
            }
        }
    }

    Column {
        id: footer
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: 12
        spacing: 8
        Text {
            visible: !!app.configError
            width: parent.width
            wrapMode: Text.Wrap
            text: app.configError
            color: t.pink
            font.family: t.sans; font.pixelSize: 11
        }
        NeonButton {
            width: parent.width
            text: "Edit checkouts…"
            onClicked: app.openSettings("checkouts")
        }
    }
}
