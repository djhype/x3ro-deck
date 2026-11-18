import QtQuick

// Small rounded status label with an optional leading dot.
Rectangle {
    id: pill
    property alias text: label.text
    readonly property Theme t: Theme {}
    property color fg: t.text
    property bool dot: true
    property bool mono: false

    implicitWidth: row.implicitWidth + 16
    implicitHeight: 22
    radius: height / 2
    color: t.bg0

    Row {
        id: row
        anchors.centerIn: parent
        spacing: 6
        Rectangle {
            visible: pill.dot
            width: 7; height: 7; radius: 3.5
            anchors.verticalCenter: parent.verticalCenter
            color: pill.fg
        }
        Text {
            id: label
            color: pill.fg
            font.family: pill.mono ? pill.t.mono : pill.t.sans
            font.pixelSize: 11
        }
    }
}
