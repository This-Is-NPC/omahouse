pragma ComponentBehavior: Bound

import QtQuick
import omahouse

// Are you sure. Only for the two things that cannot be undone by pressing the
// same key again: taking an account off the books, and taking a program off the
// list somebody may be using right now.
//
// Nothing else asks. A confirmation on an action that is its own undo is a
// keystroke charged for nothing, and a window that asks about everything is one
// where the answer stops being read.
Rectangle {
    id: sheet

    Accessible.ignored: true

    property string title: ""
    property string detail: ""
    property string topic: ""
    property var returnFocus: function () {}
    signal confirmed(string topic)

    visible: false
    color: Theme.fill(Theme.background, 0.55)
    z: 90

    function ask(topic, title, detail) {
        sheet.topic = topic
        sheet.title = title
        sheet.detail = detail
        sheet.visible = true
        scope.forceActiveFocus()
    }

    function accept() {
        const topic = sheet.topic
        sheet.close()
        sheet.confirmed(topic)
    }

    function close() {
        sheet.visible = false
        sheet.returnFocus()
    }

    MouseArea {
        anchors.fill: parent
        onClicked: sheet.close()
    }

    FocusScope {
        id: scope
        anchors.fill: parent
        focus: sheet.visible

        Keys.onPressed: function (event) {
            if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                sheet.accept()
                event.accepted = true
            } else if (event.key === Qt.Key_Escape) {
                sheet.close()
                event.accepted = true
            }
        }

        Rectangle {
            width: Math.min(460, parent.width - 48)
            height: body.implicitHeight + 28
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.top
            anchors.topMargin: 110
            radius: Theme.rounding
            color: Theme.panel
            border.width: 1
            border.color: Theme.urgent

            Accessible.role: Accessible.Dialog
            Accessible.name: sheet.title
            MouseArea {
                anchors.fill: parent
                onClicked: function (mouse) { mouse.accepted = true }
            }

            Column {
                id: body
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 14
                spacing: 8

                Label {
                    width: parent.width
                    text: sheet.title
                    font.bold: true
                }
                Label {
                    width: parent.width
                    visible: sheet.detail !== ""
                    quiet: true
                    text: sheet.detail
                    elide: Text.ElideNone
                    wrapMode: Text.WordWrap
                }
                Row {
                    anchors.right: parent.right
                    spacing: 8

                    Chip {
                        objectName: "confirmNo"
                        label: "no"
                        key: "Esc"
                        quiet: true
                        onClicked: sheet.close()
                    }
                    Chip {
                        objectName: "confirmYes"
                        label: "yes"
                        key: "Enter"
                        role: Theme.urgent
                        on: true
                        onClicked: sheet.accept()
                    }
                }
            }
        }
    }
}
