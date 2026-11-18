// SPDX-FileCopyrightText: 2026 x3ro
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick

// First run: no checkouts yet. Pick a folder, then the worktrees to add.
Rectangle {
    id: root
    readonly property Theme t: Theme {}
    readonly property var candidates: app.welcomeCandidates
    readonly property int picked: candidates.filter(function (c) { return c.checked }).length
    color: t.bg0

    Column {
        anchors.centerIn: parent
        width: Math.min(640, root.width - 64)
        spacing: 18

        Text {
            text: "Welcome to X3RO Deck"
            color: t.text
            font.family: t.sans; font.pixelSize: 28; font.weight: Font.DemiBold
        }
        Text {
            width: parent.width
            wrapMode: Text.Wrap
            text: "Each checkout gets its own agent session, compose stack and pull request status. "
                  + "Choose a repository to start; its other git worktrees can come along."
            color: t.soft
            font.family: t.sans; font.pixelSize: 14
            lineHeight: 1.3
        }

        NeonButton {
            visible: root.candidates.length === 0
            width: 220
            text: "Choose a folder…"
            variant: "outline"; accent: t.cyan
            onClicked: app.chooseWelcomeFolder()
        }

        Rectangle {
            visible: root.candidates.length > 0
            width: parent.width
            height: list.implicitHeight + 28
            radius: 14
            color: t.panel
            border.color: t.line

            Column {
                id: list
                x: 16; y: 14
                width: parent.width - 32
                spacing: 10
                Text {
                    text: root.candidates.length > 1 ? "CHECKOUTS TO ADD" : "CHECKOUT TO ADD"
                    color: t.dim; font.family: t.sans; font.pixelSize: 11; font.letterSpacing: 1.6
                }
                Repeater {
                    model: root.candidates
                    delegate: Item {
                        required property var modelData
                        required property int index
                        width: list.width
                        height: 40
                        Rectangle {
                            id: box
                            width: 18; height: 18; radius: 4
                            anchors.verticalCenter: parent.verticalCenter
                            color: modelData.checked ? t.magenta : "transparent"
                            border.color: modelData.checked ? t.magenta : t.dim
                            Text {
                                anchors.centerIn: parent
                                visible: modelData.checked
                                text: "✓"
                                color: t.bg0
                                font.pixelSize: 12; font.weight: Font.Bold
                            }
                        }
                        Column {
                            anchors.left: box.right; anchors.leftMargin: 12
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 2
                            Text {
                                text: modelData.name + (modelData.branch ? "  ·  " + modelData.branch : "")
                                color: t.text
                                font.family: t.sans; font.pixelSize: 14; font.weight: Font.DemiBold
                            }
                            Text {
                                width: parent.width
                                text: modelData.path
                                elide: Text.ElideMiddle
                                color: t.dim
                                font.family: t.mono; font.pixelSize: 12
                            }
                        }
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: app.toggleWelcomeCandidate(index)
                        }
                    }
                }
            }
        }

        Row {
            visible: root.candidates.length > 0
            spacing: 10
            NeonButton {
                width: 220
                text: root.picked === 1 ? "Add 1 checkout" : "Add " + root.picked + " checkouts"
                variant: "outline"; accent: t.cyan
                enabledState: root.picked > 0
                onClicked: app.finishWelcome()
            }
            NeonButton {
                width: 140
                text: "Cancel"
                variant: "ghost"
                onClicked: app.cancelWelcome()
            }
        }

        Text {
            width: parent.width
            wrapMode: Text.Wrap
            text: "You can add, rename and reorder checkouts later in Settings → Checkouts."
            color: t.dim
            font.family: t.sans; font.pixelSize: 12
        }
    }
}
