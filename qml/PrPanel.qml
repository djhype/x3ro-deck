import QtQuick

// Status of the current branch's pull request: review, conflicts and checks.
Rectangle {
    id: root
    readonly property Theme t: Theme {}
    property var pr: ({})
    readonly property bool open: pr.state === "OPEN" || pr.state === "DRAFT"
    readonly property bool conflicts: pr.mergeable === "CONFLICTING" || pr.mergeState === "DIRTY"
    readonly property bool approved: pr.review === "APPROVED" || (!pr.review && (pr.approvers || []).length > 0)
    readonly property bool trouble: open && (conflicts || pr.review === "CHANGES_REQUESTED" || pr.checksFailed > 0)
    readonly property bool ready: open && pr.state !== "DRAFT" && approved && pr.mergeable === "MERGEABLE"
                                  && !pr.checksFailed && !pr.checksPending

    height: col.implicitHeight + 28
    radius: 14
    color: t.panel
    border.color: trouble ? t.pink : ready ? t.cyan : t.line

    function stateColor(s) {
        switch (s) {
        case "OPEN": return t.cyan
        case "MERGED": return t.magenta
        default: return t.dim
        }
    }
    function names(list) {
        return list && list.length ? " · " + list.join(", ") : ""
    }
    function review() {
        if (pr.review === "CHANGES_REQUESTED") return { c: t.pink, s: "Changes requested" + names(pr.changesRequestedBy) }
        if (approved) return { c: t.cyan, s: "Approved" + names(pr.approvers) }
        if (pr.review === "REVIEW_REQUIRED") return { c: t.amber, s: "Review required" }
        return { c: t.dim, s: "No reviews yet" }
    }
    function merge() {
        if (conflicts) return { c: t.pink, s: "Merge conflicts" }
        if (pr.mergeable === "UNKNOWN") return { c: t.dim, s: "Checking…" }
        if (pr.mergeState === "BEHIND") return { c: t.amber, s: "None · behind " + pr.base }
        return { c: t.cyan, s: "None" }
    }
    function checks() {
        var parts = []
        if (pr.checksFailed) parts.push(pr.checksFailed + " failed")
        if (pr.checksPending) parts.push(pr.checksPending + " running")
        if (pr.checksPassed) parts.push(pr.checksPassed + " passed")
        if (!parts.length) return { c: t.dim, s: "No checks" }
        if (pr.checksFailed) return { c: t.pink, s: parts.join(" · ") }
        if (pr.checksPending) return { c: t.amber, s: parts.join(" · ") }
        return { c: t.cyan, s: "All " + pr.checksPassed + " passed" }
    }

    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        onClicked: app.openPr()
    }

    Column {
        id: col
        x: 16; y: 14
        width: parent.width - 32
        spacing: 10

        Item {
            width: parent.width
            height: statePill.height
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: "PULL REQUEST"
                color: t.dim; font.family: t.sans; font.pixelSize: 11; font.letterSpacing: 1.6
            }
            Pill {
                id: statePill
                anchors.right: parent.right
                text: root.ready ? "READY TO MERGE" : (pr.state || "")
                fg: root.ready ? t.cyan : root.stateColor(pr.state)
            }
        }

        Item {
            width: parent.width
            height: numberText.implicitHeight
            Text {
                id: numberText
                text: "#" + pr.number + "  "
                color: t.cyan
                font.family: t.mono; font.pixelSize: 15; font.weight: Font.DemiBold
            }
            Text {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                text: ""
                color: refreshMouse.containsMouse ? t.text : t.dim
                font.family: t.mono; font.pixelSize: 13
                MouseArea {
                    id: refreshMouse
                    anchors.fill: parent
                    anchors.margins: -6
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: app.refreshPr()
                }
            }
        }

        Text {
            width: parent.width
            wrapMode: Text.Wrap
            maximumLineCount: 3
            elide: Text.ElideRight
            text: pr.title || ""
            color: t.text
            font.family: t.sans; font.pixelSize: 13
            lineHeight: 1.2
        }

        Row {
            spacing: 10
            Text {
                text: "→ " + (pr.base || "")
                color: t.soft
                font.family: t.mono; font.pixelSize: 12
                elide: Text.ElideMiddle
                width: Math.min(implicitWidth, col.width - diffStat.implicitWidth - 10)
            }
            Text {
                id: diffStat
                textFormat: Text.StyledText
                text: "<font color='" + t.cyan + "'>+" + (pr.additions || 0) + "</font> <font color='" + t.pink + "'>−" + (pr.deletions || 0) + "</font>"
                font.family: t.mono; font.pixelSize: 12
            }
        }

        Rectangle { width: parent.width; height: 1; color: t.line }

        Grid {
            width: parent.width
            columns: 2
            columnSpacing: 12
            rowSpacing: 8

            Text { text: "Review"; color: t.dim; font.family: t.sans; font.pixelSize: 13 }
            StatusLine { status: root.review() }

            Text { visible: root.open; text: "Conflicts"; color: t.dim; font.family: t.sans; font.pixelSize: 13 }
            StatusLine { visible: root.open; status: root.merge() }

            Text { text: "Checks"; color: t.dim; font.family: t.sans; font.pixelSize: 13 }
            StatusLine { status: root.checks() }
        }

        Column {
            visible: (pr.failing || []).length > 0
            width: parent.width
            spacing: 3
            Repeater {
                model: (pr.failing || []).slice(0, 3)
                Text {
                    required property string modelData
                    width: parent.width
                    text: "✕ " + modelData
                    elide: Text.ElideRight
                    color: t.soft
                    font.family: t.mono; font.pixelSize: 11
                }
            }
            Text {
                visible: (pr.failing || []).length > 3
                text: "+" + ((pr.failing || []).length - 3) + " more failing"
                color: t.dim
                font.family: t.sans; font.pixelSize: 11
            }
        }
    }

    component StatusLine: Row {
        property var status: ({ c: "transparent", s: "" })
        spacing: 7
        width: col.width - 12 - 80
        Rectangle {
            width: 7; height: 7; radius: 3.5
            anchors.verticalCenter: parent.verticalCenter
            color: parent.status.c
        }
        Text {
            width: parent.width - 14
            text: parent.status.s
            color: parent.status.c
            elide: Text.ElideRight
            font.family: t.sans; font.pixelSize: 13; font.weight: Font.DemiBold
        }
    }
}
