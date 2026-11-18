import QtQuick

Rectangle {
    id: root
    readonly property Theme t: Theme {}
    color: t.bg1

    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: t.line }

    // Logo
    Row {
        anchors.left: parent.left
        anchors.leftMargin: 20
        anchors.verticalCenter: parent.verticalCenter
        spacing: 10
        Canvas {
            width: 26; height: 26
            anchors.verticalCenter: parent.verticalCenter
            onPaint: {
                var ctx = getContext("2d")
                ctx.lineWidth = 2
                ctx.lineJoin = "round"
                ctx.strokeStyle = t.magenta
                ctx.beginPath(); ctx.moveTo(3, 22); ctx.lineTo(13, 4); ctx.lineTo(23, 22); ctx.closePath(); ctx.stroke()
                ctx.strokeStyle = t.cyan; ctx.lineCap = "round"
                ctx.beginPath(); ctx.moveTo(8, 16); ctx.lineTo(18, 16); ctx.stroke()
            }
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            textFormat: Text.StyledText
            text: "X3RO<font color='" + t.magenta + "'>//</font>DECK"
            color: t.text
            font.family: t.display
            font.pixelSize: 17
            font.weight: Font.Bold
            font.letterSpacing: 2.5
        }
    }

    // View tabs
    Rectangle {
        anchors.centerIn: parent
        width: tabs.implicitWidth + 8
        height: 46
        radius: 12
        color: t.raised
        border.color: t.line
        Row {
            id: tabs
            anchors.centerIn: parent
            spacing: 4
            Repeater {
                model: [
                    { key: "docker", label: "Docker", badge: app.dockerConnected ? app.containersUp + "/" + app.containersTotal : "offline",
                      badgeBg: t.bg0, badgeFg: app.dockerConnected && app.containersUp === app.containersTotal ? t.cyan : t.pink, hint: "Ctrl+Alt+D" },
                    { key: "agents", label: "Agents", badge: app.needsYou > 0 ? app.needsYou + " need you" : "all quiet",
                      badgeBg: app.needsYou > 0 ? t.amber : t.bg0, badgeFg: app.needsYou > 0 ? t.ink : t.dim, hint: "Ctrl+Alt+A" }
                ]
                delegate: Rectangle {
                    required property var modelData
                    readonly property bool active: app.tab === modelData.key
                    width: tabRow.implicitWidth + 32
                    height: 38
                    radius: 9
                    color: active ? t.tabActive : (tabMouse.containsMouse ? t.selected : "transparent")
                    Behavior on color { ColorAnimation { duration: 140 } }
                    Row {
                        id: tabRow
                        anchors.centerIn: parent
                        spacing: 10
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: modelData.label
                            color: active ? t.magenta : t.dim
                            font.family: t.sans; font.pixelSize: 14; font.weight: Font.DemiBold
                        }
                        Rectangle {
                            anchors.verticalCenter: parent.verticalCenter
                            width: badgeText.implicitWidth + 14; height: 20; radius: 6
                            color: modelData.badgeBg
                            Text {
                                id: badgeText
                                anchors.centerIn: parent
                                text: modelData.badge
                                color: modelData.badgeFg
                                font.family: t.mono; font.pixelSize: 11; font.weight: Font.Bold
                            }
                        }
                    }
                    MouseArea {
                        id: tabMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: app.setTab(modelData.key)
                    }
                }
            }
        }
    }

    Row {
        anchors.right: parent.right
        anchors.rightMargin: 16
        anchors.verticalCenter: parent.verticalCenter
        spacing: 8
        NeonButton {
            text: "Settings"
            onClicked: app.openSettings("")
        }
        NeonButton {
            text: app.railVisible ? "Hide rail" : "Needs you ›"
            variant: app.railVisible ? "ghost" : "outline"
            accent: t.amber
            onClicked: app.toggleRail()
        }
    }
}
