import QtQuick

Rectangle {
    id: root
    readonly property Theme t: Theme {}
    property double now: Date.now()
    color: t.bg1

    Timer { interval: 1000; running: true; repeat: true; onTriggered: root.now = Date.now() }

    Rectangle { anchors.left: parent.left; width: 1; height: parent.height; color: t.line }

    Item {
        id: header
        x: 20; y: 14
        width: parent.width - 40; height: 28
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: "NEEDS YOU · " + list.count
            color: list.count > 0 ? t.amber : t.dim
            font.family: t.sans; font.pixelSize: 13; font.weight: Font.DemiBold; font.letterSpacing: 1.8
        }
        Text {
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            text: "Ctrl+Alt+N"
            color: t.dim
            font.family: t.mono; font.pixelSize: 11
        }
    }

    ListView {
        id: list
        anchors.top: header.bottom
        anchors.topMargin: 12
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: 14
        anchors.rightMargin: 14
        spacing: 12
        clip: true
        model: app.attention
        boundsBehavior: Flickable.StopAtBounds

        add: Transition {
            ParallelAnimation {
                NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 220 }
                NumberAnimation { property: "x"; from: 40; to: 0; duration: 260; easing.type: Easing.OutCubic }
            }
        }
        remove: Transition {
            ParallelAnimation {
                NumberAnimation { property: "opacity"; to: 0; duration: 180 }
                NumberAnimation { property: "x"; to: 60; duration: 200 }
            }
        }
        displaced: Transition { NumberAnimation { properties: "y"; duration: 220; easing.type: Easing.OutCubic } }

        delegate: Rectangle {
            id: card
            required property string itemId
            required property string kind
            required property int checkout
            required property string checkoutName
            required property string title
            required property string detail
            required property double since
            required property bool canAlways
            required property bool dismissable
            required property string prUrl

            readonly property color accent: kind === "permission" || kind === "question" ? t.amber
                                          : kind === "docker" ? t.pink : t.magenta
            readonly property var checkoutColors: ["#7aa2ff", "#9ece6a", "#bb9af7", "#7dcfff", "#e0af68", "#f7768e"]

            width: ListView.view.width
            height: cardCol.implicitHeight + 28
            radius: 16
            color: kind === "permission" || kind === "question" ? "#15120a" : kind === "docker" ? "#170b10" : "#140d18"
            border.width: 1
            border.color: kind === "permission" || kind === "question" ? accent : Qt.darker(accent, 2.2)

            Column {
                id: cardCol
                x: 16; y: 14
                width: parent.width - 32
                spacing: 10

                Item {
                    width: parent.width; height: 18
                    Row {
                        spacing: 8
                        anchors.verticalCenter: parent.verticalCenter
                        Rectangle {
                            width: 10; height: 10; radius: 3
                            anchors.verticalCenter: parent.verticalCenter
                            color: card.checkoutColors[card.checkout % card.checkoutColors.length]
                        }
                        Text {
                            text: card.checkoutName + (card.kind === "docker" ? "  · docker" : "")
                            color: t.text
                            font.family: t.sans; font.pixelSize: 13
                        }
                    }
                    Text {
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        text: (card.kind === "done" ? "done " : "waiting ") + t.elapsed(card.since, root.now)
                        color: card.accent
                        font.family: t.mono; font.pixelSize: 11
                    }
                }

                Text {
                    width: parent.width
                    wrapMode: Text.Wrap
                    text: card.title
                    color: t.text
                    font.family: t.sans; font.pixelSize: 16; font.weight: Font.DemiBold
                }

                Rectangle {
                    visible: !!card.detail
                    width: parent.width
                    height: detailText.implicitHeight + 20
                    radius: 10
                    color: "#0a0a12"
                    Text {
                        id: detailText
                        x: 12; y: 10
                        width: parent.width - 24
                        wrapMode: card.kind === "done" ? Text.Wrap : Text.WrapAnywhere
                        maximumLineCount: card.kind === "done" ? 6 : 8
                        elide: Text.ElideRight
                        text: card.detail
                        color: card.kind === "permission" ? t.amber : t.soft
                        font.family: card.kind === "done" || card.kind === "question" ? t.sans : t.mono
                        font.pixelSize: 13
                        lineHeight: 1.2
                    }
                }

                Flow {
                    width: parent.width
                    spacing: 8

                    // permission
                    NeonButton { visible: card.kind === "permission"; text: "Allow"; variant: "solid"; accent: t.amber; onClicked: app.attentionAction(card.itemId, "allow") }
                    NeonButton { visible: card.kind === "permission" && card.canAlways; text: "Always"; variant: "outline"; accent: t.amber; onClicked: app.attentionAction(card.itemId, "always") }
                    NeonButton { visible: card.kind === "permission"; text: "Deny"; onClicked: app.attentionAction(card.itemId, "deny") }

                    // docker
                    NeonButton { visible: card.kind === "docker"; text: "Restart"; variant: "outline"; accent: t.pink; onClicked: app.attentionAction(card.itemId, "restart") }
                    NeonButton { visible: card.kind === "docker"; text: "Tail logs"; onClicked: app.attentionAction(card.itemId, "logs") }

                    // done / question
                    NeonButton { visible: card.kind === "done"; text: card.prUrl ? "View PR" : "/create-pr"; mono: !card.prUrl; variant: "outline"; accent: t.magenta
                        onClicked: {
                            if (card.prUrl) { app.openPr(card.checkout); return }
                            app.select(card.checkout); app.sendToAgent("/create-pr")
                        } }

                    NeonButton { text: "Open →"; variant: card.kind === "question" ? "outline" : "ghost"; accent: t.amber; onClicked: app.attentionAction(card.itemId, "open") }
                    NeonButton { visible: card.dismissable; text: "Dismiss"; onClicked: app.attentionAction(card.itemId, "dismiss") }
                }
            }
        }
    }

    Column {
        anchors.centerIn: parent
        visible: list.count === 0
        spacing: 8
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "All quiet."
            color: t.soft
            font.family: t.display; font.pixelSize: 22
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "Permission prompts, questions, finished\nturns and sick containers land here."
            horizontalAlignment: Text.AlignHCenter
            color: t.dim
            font.family: t.sans; font.pixelSize: 12
        }
    }
}
