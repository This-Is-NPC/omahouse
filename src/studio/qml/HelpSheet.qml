pragma ComponentBehavior: Bound

import QtQuick
import omahouse

// The key map, on the window, under `?`.
//
// A program driven by the keyboard has to be able to say what its keys are
// without sending anyone to a README.
//
// This renders `win.keymap`, and half of that map is generated from the same
// `commands` table the chips and the palette are drawn from -- so an action
// cannot appear on a button without appearing here, and cannot be listed here
// without a key that runs it. The navigation half is written by hand and a test
// presses every key on it.
Rectangle {
    id: sheet

    // The scrim is visual only; the dialog panel carries the role.
    Accessible.ignored: true

    property var returnFocus: function () {}
    property var groups: []

    visible: false
    color: Theme.fill(Theme.background, 0.72)
    z: 95

    function open() {
        sheet.visible = true
        scope.forceActiveFocus()
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

        // Any key closes it. It is a thing you glance at, so needing to remember
        // a second key to dismiss the list of keys would be a joke at the
        // reader's expense.
        Keys.onPressed: function (event) {
            sheet.close()
            event.accepted = true
        }

        Rectangle {
            anchors.centerIn: parent
            width: Math.min(940, parent.width - 48)
            height: Math.min(columns.implicitHeight + 66, parent.height - 64)
            radius: Theme.rounding
            color: Theme.panel
            border.width: 1
            border.color: Theme.accent

            Accessible.role: Accessible.Dialog
            Accessible.name: heading.text
            MouseArea {
                anchors.fill: parent
                onClicked: function (mouse) { mouse.accepted = true }
            }

            Label {
                id: heading
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.margins: 18
                text: "keys"
                font.bold: true
            }
            Label {
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 18
                quiet: true
                text: "any key closes"
            }

            Flickable {
                anchors.fill: parent
                anchors.margins: 18
                anchors.topMargin: heading.height + 26
                contentHeight: columns.implicitHeight
                clip: true
                boundsBehavior: Flickable.StopAtBounds

                Flow {
                    id: columns
                    readonly property int across: width >= 700 ? 3 : 2
                    width: parent.width
                    spacing: 26

                    Repeater {
                        model: sheet.groups

                        Column {
                            id: group
                            required property var modelData
                            readonly property var spec: group.modelData
                            width: (columns.width - (columns.across - 1) * 26) / columns.across
                            spacing: 6

                            Label {
                                quiet: true
                                text: group.spec.title
                                bottomPadding: 2
                            }

                            Repeater {
                                model: group.spec.keys

                                Row {
                                    id: line
                                    required property var modelData
                                    readonly property var binding: line.modelData
                                    width: group.width
                                    spacing: 7

                                    Row {
                                        id: caps
                                        anchors.verticalCenter: parent.verticalCenter
                                        spacing: 3
                                        Repeater {
                                            model: line.binding.key.split(" ")
                                            KeyCap {
                                                required property string modelData
                                                key: modelData
                                            }
                                        }
                                    }
                                    Label {
                                        objectName: "helpEntry"
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: line.width - caps.width - 7
                                        text: line.binding.label
                                        font.pixelSize: 11
                                        // Nothing on this sheet may be cut
                                        // short: a reference that answers with
                                        // the half you could already guess is
                                        // one you stop looking at. It wraps.
                                        elide: Text.ElideNone
                                        wrapMode: Text.WordWrap
                                    }
                                }
                            }

                            Item { width: 1; height: 6 }
                        }
                    }
                }
            }
        }
    }
}
