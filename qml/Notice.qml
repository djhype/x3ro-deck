import QtQuick

// A missing tool or another problem the user can fix: a title and how to fix it.
Rectangle {
    id: root
    readonly property Theme t: Theme {}
    // {title, hint, tone ("warning" or "info"), action (a button's text)}
    property var notice: ({})
    readonly property bool info: notice.tone === "info"
    signal activated()
    visible: !!notice.title
    height: visible ? col.implicitHeight + 24 : 0
    radius: 14
    color: t.panel
    border.color: info ? t.line : t.amber

    Column {
        id: col
        x: 16; y: 12
        width: parent.width - 32
        spacing: 6
        Text {
            width: parent.width
            wrapMode: Text.Wrap
            text: root.notice.title || ""
            color: root.info ? t.soft : t.amber
            font.family: t.sans; font.pixelSize: 13; font.weight: Font.DemiBold
        }
        Text {
            width: parent.width
            wrapMode: Text.Wrap
            visible: !!root.notice.hint
            text: root.notice.hint || ""
            color: t.soft
            font.family: t.sans; font.pixelSize: 12
            lineHeight: 1.2
        }
        Text {
            visible: !!root.notice.action
            text: (root.notice.action || "") + " ⟳"
            color: actionMouse.containsMouse ? t.text : t.cyan
            font.family: t.sans; font.pixelSize: 12; font.weight: Font.DemiBold
            MouseArea {
                id: actionMouse
                anchors.fill: parent
                anchors.margins: -4
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: root.activated()
            }
        }
    }
}
