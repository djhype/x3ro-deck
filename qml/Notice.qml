import QtQuick

// A missing tool or another problem the user can fix: a title and how to fix it.
Rectangle {
    id: root
    readonly property Theme t: Theme {}
    property var notice: ({})
    visible: !!notice.title
    height: visible ? col.implicitHeight + 24 : 0
    radius: 14
    color: t.panel
    border.color: t.amber

    Column {
        id: col
        x: 16; y: 12
        width: parent.width - 32
        spacing: 6
        Text {
            width: parent.width
            wrapMode: Text.Wrap
            text: root.notice.title || ""
            color: t.amber
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
    }
}
