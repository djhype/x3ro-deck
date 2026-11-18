import QtQuick

Rectangle {
    id: root
    readonly property Theme t: Theme {}
    readonly property var cur: app.current
    readonly property bool narrow: width < 900
    color: t.bg0

    // ── Services ──
    Rectangle {
        id: servicesBox
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.right: side.left
        anchors.margins: 16
        anchors.leftMargin: 24
        radius: 14
        color: t.panel
        border.color: t.line
        clip: true

        Item {
            id: boxHeader
            width: parent.width; height: 46
            Text {
                anchors.left: parent.left; anchors.leftMargin: 16
                anchors.verticalCenter: parent.verticalCenter
                text: "Services"
                color: t.text
                font.family: t.sans; font.pixelSize: 14; font.weight: Font.DemiBold
            }
            Text {
                anchors.right: parent.right; anchors.rightMargin: 16
                anchors.verticalCenter: parent.verticalCenter
                text: !app.dockerConnected ? "docker daemon unreachable"
                    : cur.containersTotal ? cur.containersTotal + " containers · " + cur.containersUp + " up"
                    : "no compose project running here"
                color: !app.dockerConnected ? t.pink : t.dim
                font.family: t.mono; font.pixelSize: 12
            }
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: t.line }
        }

        // Column layout shared by header and rows.
        readonly property var cols: [0.30, 0.22, 0.13, 0.08, 0.15, 0.12]
        function colX(i) { var x = 16; for (var k = 0; k < i; k++) x += cols[k] * (width - 32); return x }
        function colW(i) { return cols[i] * (width - 32) - 8 }

        Item {
            id: colHeader
            anchors.top: boxHeader.bottom
            width: parent.width; height: 30
            Repeater {
                model: ["SERVICE", "HOST PORTS", "STATE", "UP", "CPU · MEM", ""]
                Text {
                    required property int index
                    required property string modelData
                    x: servicesBox.colX(index)
                    anchors.verticalCenter: parent.verticalCenter
                    text: modelData
                    color: t.dim
                    font.family: t.sans; font.pixelSize: 11; font.letterSpacing: 1.2
                }
            }
        }

        ListView {
            id: services
            anchors.top: colHeader.bottom
            anchors.bottom: parent.bottom
            width: parent.width
            clip: true
            model: app.services
            boundsBehavior: Flickable.StopAtBounds

            delegate: Rectangle {
                id: row
                required property int index
                required property string name
                required property string health
                required property string ports
                required property string uptime
                required property string cpu
                required property string mem
                width: ListView.view.width
                height: 36
                color: rowHover.hovered ? t.raised : "transparent"
                Rectangle { width: parent.width; height: 1; color: t.rowLine }

                HoverHandler { id: rowHover }

                Row {
                    x: servicesBox.colX(0)
                    width: servicesBox.colW(0)
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 10
                    Rectangle {
                        width: 8; height: 8; radius: 4
                        anchors.verticalCenter: parent.verticalCenter
                        color: t.healthColor(row.health)
                        Rectangle {
                            anchors.centerIn: parent
                            width: 16; height: 16; radius: 8
                            color: "transparent"
                            border.width: 3
                            border.color: Qt.rgba(parent.color.r, parent.color.g, parent.color.b, 0.25)
                        }
                    }
                    Text {
                        width: parent.width - 18
                        elide: Text.ElideRight
                        text: row.name
                        color: t.text
                        font.family: t.mono; font.pixelSize: 13
                    }
                }
                Text {
                    x: servicesBox.colX(1); width: servicesBox.colW(1)
                    anchors.verticalCenter: parent.verticalCenter
                    elide: Text.ElideRight
                    text: row.ports
                    color: row.ports === "—" ? t.dim : t.cyan
                    font.family: t.mono; font.pixelSize: 13
                }
                Text {
                    x: servicesBox.colX(2); width: servicesBox.colW(2)
                    anchors.verticalCenter: parent.verticalCenter
                    text: row.health
                    color: t.healthColor(row.health)
                    font.family: t.mono; font.pixelSize: 13
                }
                Text {
                    x: servicesBox.colX(3); width: servicesBox.colW(3)
                    anchors.verticalCenter: parent.verticalCenter
                    text: row.uptime
                    color: t.soft
                    font.family: t.mono; font.pixelSize: 13
                }
                Text {
                    x: servicesBox.colX(4); width: servicesBox.colW(4)
                    anchors.verticalCenter: parent.verticalCenter
                    text: row.cpu ? row.cpu + " · " + row.mem : "·"
                    color: t.dim
                    font.family: t.mono; font.pixelSize: 13
                }
                Row {
                    x: servicesBox.colX(5)
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 2
                    opacity: rowHover.hovered ? 1 : 0.35
                    Repeater {
                        model: [ { label: "logs", action: "logs" }, { label: "↻", action: "restart" } ]
                        Rectangle {
                            required property var modelData
                            width: modelData.label.length > 1 ? 40 : 28; height: 26; radius: 6
                            color: actionMouse.containsMouse ? t.selected : "transparent"
                            Text {
                                anchors.centerIn: parent
                                text: modelData.label
                                color: t.soft
                                font.family: t.mono; font.pixelSize: 12
                            }
                            MouseArea {
                                id: actionMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: app.serviceAction(row.index, modelData.action)
                            }
                        }
                    }
                }
            }
        }

        Text {
            anchors.centerIn: services
            visible: services.count === 0
            text: !app.dockerConnected ? "Waiting for /var/run/docker.sock …"
                  : app.current.composeFound === false ? "No compose file found.\nAdd compose.yaml to the checkout, or pick files in Settings → Checkouts."
                  : "Nothing running for this checkout.\nUse ▲ Up to start the compose services."
            horizontalAlignment: Text.AlignHCenter
            color: t.dim
            font.family: t.sans; font.pixelSize: 13
        }
    }

    // ── Quick commands + links ──
    Flickable {
        id: side
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.margins: 16
        anchors.rightMargin: 24
        width: root.narrow ? 260 : 340
        contentHeight: sideCol.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        Column {
            id: sideCol
            width: parent.width
            spacing: 16

            Notice {
                width: parent.width
                notice: app.composeMissing
            }

            Rectangle {
                id: quickCard
                property bool showHidden: false
                readonly property int hiddenCount: app.quickCommands.filter(q => q.hidden).length
                // Pinned entries first (the controller sorts them so), then one
                // section per source in the order they arrive.
                readonly property var sections: {
                    var out = []
                    var bySource = {}
                    for (var i = 0; i < app.quickCommands.length; ++i) {
                        var q = app.quickCommands[i]
                        if (q.hidden && !showHidden) continue
                        var title = q.pinned ? "Pinned" : q.source
                        if (bySource[title] === undefined) {
                            bySource[title] = out.length
                            out.push({ title: title, items: [] })
                        }
                        out[bySource[title]].items.push(q)
                    }
                    return out
                }
                width: parent.width
                height: qc.implicitHeight + 28
                radius: 14
                color: t.panel
                border.color: t.line
                Column {
                    id: qc
                    x: 14; y: 14
                    width: parent.width - 28
                    spacing: 10
                    Text {
                        text: "Quick commands"
                        color: t.text
                        font.family: t.sans; font.pixelSize: 14; font.weight: Font.DemiBold
                    }
                    Text {
                        visible: quickCard.sections.length === 0
                        width: parent.width
                        wrapMode: Text.WordWrap
                        text: quickCard.hiddenCount > 0 ? "Every quick command is hidden."
                              : "None yet. Makefile targets and package.json scripts show up here, and Settings → Docker adds your own."
                        color: t.dim
                        font.family: t.sans; font.pixelSize: 12
                    }
                    Repeater {
                        model: quickCard.sections
                        Column {
                            id: section
                            required property var modelData
                            width: qc.width
                            spacing: 6
                            Text {
                                visible: quickCard.sections.length > 1 || section.modelData.title !== "Manual"
                                text: section.modelData.title.toUpperCase()
                                color: t.dim
                                font.family: t.sans; font.pixelSize: 10; font.weight: Font.DemiBold
                                font.letterSpacing: 0.8
                            }
                            Grid {
                                columns: 2
                                spacing: 8
                                width: parent.width
                                Repeater {
                                    model: section.modelData.items
                                    Item {
                                        id: cell
                                        required property var modelData
                                        width: (qc.width - 8) / 2
                                        height: btn.height
                                        opacity: modelData.hidden ? 0.45 : 1
                                        HoverHandler { id: hover }
                                        NeonButton {
                                            id: btn
                                            width: parent.width
                                            text: cell.modelData.label
                                            subtext: cell.modelData.command
                                            reserve: 34
                                            variant: "ghost"
                                            onClicked: app.runQuick(cell.modelData.key)
                                        }
                                        Rectangle {
                                            width: 3; height: 14; radius: 1.5
                                            anchors.right: parent.right; anchors.rightMargin: 10
                                            anchors.top: parent.top; anchors.topMargin: 10
                                            color: t.groupColor(cell.modelData.group)
                                        }
                                        Row {
                                            anchors.right: parent.right; anchors.rightMargin: 6
                                            anchors.bottom: parent.bottom; anchors.bottomMargin: 4
                                            spacing: 2
                                            QuickGlyph {
                                                visible: hover.hovered || cell.modelData.pinned
                                                glyph: cell.modelData.pinned ? "★" : "☆"
                                                active: cell.modelData.pinned
                                                onClicked: app.pinQuick(cell.modelData.key, !cell.modelData.pinned)
                                            }
                                            QuickGlyph {
                                                visible: hover.hovered
                                                glyph: cell.modelData.hidden ? "↺" : "×"
                                                onClicked: app.hideQuick(cell.modelData.key, !cell.modelData.hidden)
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                    Text {
                        visible: quickCard.hiddenCount > 0
                        text: quickCard.showHidden ? "Hide the hidden ones" : quickCard.hiddenCount + " hidden · show"
                        color: hiddenMouse.containsMouse ? t.magenta : t.dim
                        font.family: t.sans; font.pixelSize: 11
                        MouseArea {
                            id: hiddenMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: quickCard.showHidden = !quickCard.showHidden
                        }
                    }
                }
            }

            Rectangle {
                width: parent.width
                height: lk.implicitHeight + 28
                radius: 14
                color: t.panel
                border.color: t.line
                Column {
                    id: lk
                    x: 14; y: 14
                    width: parent.width - 28
                    spacing: 10
                    Text {
                        text: "Open in browser"
                        color: t.text
                        font.family: t.sans; font.pixelSize: 14; font.weight: Font.DemiBold
                    }
                    Text {
                        visible: app.links.length === 0
                        width: parent.width
                        wrapMode: Text.WordWrap
                        text: "Web ports of this checkout's running containers show up here."
                        color: t.dim
                        font.family: t.sans; font.pixelSize: 12
                    }
                    Flow {
                        width: parent.width
                        spacing: 8
                        Repeater {
                            model: app.links
                            Rectangle {
                                required property var modelData
                                required property int index
                                readonly property bool live: modelData.port > 0
                                width: linkText.implicitWidth + 24
                                height: 32
                                radius: 16
                                color: linkMouse.containsMouse && live ? t.selected : t.bg0
                                border.color: t.line
                                opacity: live ? 1 : 0.45
                                Text {
                                    id: linkText
                                    anchors.centerIn: parent
                                    textFormat: Text.StyledText
                                    text: modelData.label + (parent.live ? " <font color='" + t.dim + "'>:" + modelData.port + "</font>" : "")
                                    color: t.cyan
                                    font.family: t.mono; font.pixelSize: 12
                                }
                                MouseArea {
                                    id: linkMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: parent.live ? Qt.PointingHandCursor : Qt.ArrowCursor
                                    onClicked: if (parent.live) app.openLink(index)
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
