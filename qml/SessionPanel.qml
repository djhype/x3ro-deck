import QtQuick

Rectangle {
    id: root
    readonly property Theme t: Theme {}
    readonly property var cur: app.current
    property double now: Date.now()
    color: t.bg0

    Timer { interval: 1000; running: root.visible; repeat: true; onTriggered: root.now = Date.now() }

    Flickable {
        anchors.fill: parent
        anchors.margins: 16
        contentHeight: col.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        Column {
            id: col
            width: parent.width
            spacing: 12

            // Ticket
            Rectangle {
                width: parent.width
                height: ticketCol.implicitHeight + 28
                radius: 14
                color: t.panel
                border.color: t.line
                Column {
                    id: ticketCol
                    x: 16; y: 14
                    width: parent.width - 32
                    spacing: 6
                    Text { text: "TICKET"; color: t.dim; font.family: t.sans; font.pixelSize: 11; font.letterSpacing: 1.6 }
                    Text {
                        text: cur.ticket || "no ticket in branch"
                        color: cur.ticket ? t.cyan : t.dim
                        font.family: t.mono; font.pixelSize: 14
                    }
                    Text {
                        width: parent.width
                        wrapMode: Text.Wrap
                        text: cur.title || ""
                        color: t.text
                        font.family: t.sans; font.pixelSize: 14
                        lineHeight: 1.2
                    }
                }
            }

            // Session
            Rectangle {
                width: parent.width
                height: sessCol.implicitHeight + 28
                radius: 14
                color: t.panel
                border.color: ["permission", "question"].indexOf(cur.agentState) >= 0 ? t.amber : t.line
                Column {
                    id: sessCol
                    x: 16; y: 14
                    width: parent.width - 32
                    spacing: 10
                    Text { text: "SESSION"; color: t.dim; font.family: t.sans; font.pixelSize: 11; font.letterSpacing: 1.6 }

                    // Agent picker: one agent per checkout, switchable while none runs.
                    Flow {
                        width: parent.width
                        spacing: 6
                        Repeater {
                            model: app.agents
                            delegate: Rectangle {
                                required property var modelData
                                readonly property bool active: modelData.id === cur.agentId
                                readonly property bool usable: active || !cur.agentRunning
                                width: optLabel.implicitWidth + 24
                                height: 28
                                radius: 7
                                opacity: usable ? 1 : 0.4
                                color: active ? Qt.rgba(1, 0.44, 0.95, 0.12) : (optMouse.containsMouse && usable ? t.selected : t.raised)
                                border.color: active ? t.magenta : t.line
                                Text {
                                    id: optLabel
                                    anchors.centerIn: parent
                                    text: modelData.label
                                    color: active ? t.magenta : t.soft
                                    font.family: t.sans; font.pixelSize: 12; font.weight: active ? Font.DemiBold : Font.Normal
                                }
                                MouseArea {
                                    id: optMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    enabled: usable && !active
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: app.setAgentProfile(modelData.id)
                                }
                            }
                        }
                    }
                    Text {
                        visible: !!cur.agentRunning && app.agents.length > 1
                        width: parent.width
                        wrapMode: Text.Wrap
                        text: "Exit " + (cur.agentLabel || "the agent") + " to switch agents."
                        color: t.dim
                        font.family: t.sans; font.pixelSize: 11
                    }

                    Grid {
                        columns: 2
                        columnSpacing: 12
                        rowSpacing: 8
                        Text { text: "State"; color: t.dim; font.family: t.sans; font.pixelSize: 13 }
                        Text { text: t.agentLabel(cur.agentState); color: t.agentColor(cur.agentState); font.family: t.sans; font.pixelSize: 13; font.weight: Font.DemiBold }
                        Text { visible: !!cur.model; text: "Model"; color: t.dim; font.family: t.sans; font.pixelSize: 13 }
                        Text { visible: !!cur.model; text: cur.model || ""; color: t.text; font.family: t.mono; font.pixelSize: 13 }
                        Text { text: "In state"; color: t.dim; font.family: t.sans; font.pixelSize: 13 }
                        Text { text: t.elapsed(cur.since, root.now); color: t.text; font.family: t.mono; font.pixelSize: 13 }
                        Text { text: "Session"; color: t.dim; font.family: t.sans; font.pixelSize: 13 }
                        Text { text: t.elapsed(cur.startedAt, root.now); color: t.text; font.family: t.mono; font.pixelSize: 13 }
                        Text { text: "Tool calls"; color: t.dim; font.family: t.sans; font.pixelSize: 13 }
                        Text { text: String(cur.tools || 0); color: t.text; font.family: t.mono; font.pixelSize: 13 }
                        Text { text: "Files edited"; color: t.dim; font.family: t.sans; font.pixelSize: 13 }
                        Text { text: String(cur.files || 0); color: t.text; font.family: t.mono; font.pixelSize: 13 }
                    }
                    Column {
                        visible: !!cur.lastTool
                        width: parent.width
                        spacing: 4
                        Text { text: "Last tool"; color: t.dim; font.family: t.sans; font.pixelSize: 12 }
                        Text {
                            width: parent.width
                            text: cur.lastTool || ""
                            elide: Text.ElideRight
                            maximumLineCount: 2
                            wrapMode: Text.WrapAnywhere
                            color: t.soft
                            font.family: t.mono; font.pixelSize: 12
                        }
                    }
                    // Fresh agent processes announce themselves through hooks within seconds.
                    Text {
                        visible: !!cur.hookHint && root.now - (cur.runningSince || 0) > 15000
                        width: parent.width
                        wrapMode: Text.Wrap
                        text: cur.hookHint || ""
                        color: t.amber
                        font.family: t.sans; font.pixelSize: 12
                        lineHeight: 1.2
                    }
                }
            }

            PrPanel {
                visible: !!(cur.pr && cur.pr.number)
                width: parent.width
                pr: cur.pr || ({})
            }

            // Last message
            Rectangle {
                visible: !!cur.lastMessage
                width: parent.width
                height: msgCol.implicitHeight + 28
                radius: 14
                color: t.panel
                border.color: t.line
                Column {
                    id: msgCol
                    x: 16; y: 14
                    width: parent.width - 32
                    spacing: 8
                    Text { text: "LAST MESSAGE"; color: t.dim; font.family: t.sans; font.pixelSize: 11; font.letterSpacing: 1.6 }
                    Text {
                        width: parent.width
                        wrapMode: Text.Wrap
                        maximumLineCount: 10
                        elide: Text.ElideRight
                        text: cur.lastMessage || ""
                        color: t.soft
                        font.family: t.sans; font.pixelSize: 13
                        lineHeight: 1.25
                    }
                }
            }

            // Actions
            Grid {
                width: parent.width
                columns: 2
                spacing: 8
                readonly property real cellW: (width - 8) / 2
                NeonButton {
                    width: parent.cellW
                    visible: !!(cur.prUrl || cur.prPrompt)
                    text: cur.prUrl ? "View PR" : (cur.prPrompt || "")
                    variant: "outline"; accent: t.magenta; mono: !cur.prUrl
                    onClicked: cur.prUrl ? app.openPr() : app.sendToAgent(cur.prPrompt)
                }
                Repeater {
                    model: cur.prompts || []
                    delegate: NeonButton {
                        required property string modelData
                        width: parent.cellW
                        text: modelData
                        mono: true
                        onClicked: app.sendToAgent(modelData)
                    }
                }
                NeonButton { width: parent.cellW; text: "Interrupt (Esc)"; enabledState: cur.agentState === "working"; onClicked: app.agentAction("interrupt") }
                NeonButton {
                    width: parent.cellW
                    text: cur.agentRunning ? "Focus terminal" : "Start " + (cur.agentLabel || "agent")
                    variant: cur.agentRunning ? "ghost" : "outline"
                    accent: t.cyan
                    onClicked: cur.agentRunning ? app.agentAction("focus") : app.agentAction("start")
                }
            }
            NeonButton {
                width: parent.width
                visible: !cur.agentRunning && !!cur.continueCommand
                text: cur.continueCommand || ""
                mono: true
                onClicked: app.agentAction("continue")
            }

            Text {
                width: parent.width
                wrapMode: Text.Wrap
                text: "Agents live in tmux and outlive this window. From any terminal: tmux -L x3ro-deck attach -t " + (cur.session || "")
                color: t.dim
                font.family: t.sans; font.pixelSize: 11
                lineHeight: 1.2
            }
        }
    }
}
