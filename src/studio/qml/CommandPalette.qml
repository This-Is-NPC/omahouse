pragma ComponentBehavior: Bound

import QtQuick
import omahouse

// Every command the window has right now, by name, with the key beside it.
//
// It is fed the same `commands` table the chips are drawn from, so it cannot
// list an action the buttons do not have or miss one they do. It shows what is
// usable and not everything that exists: a menu of things that would refuse is
// a menu you stop reading.
//
// The id is `sheet` and not `palette` on purpose: every Item in Qt 6 has a
// `palette` grouped property, so an id by that name is one resolution rule away
// from silently meaning the colour palette instead of this object.
Rectangle {
    id: sheet

    Accessible.ignored: true

    property var commands: []
    property var returnFocus: function () {}
    signal chosen(string id)

    visible: false
    color: Theme.fill(Theme.background, 0.55)
    z: 80

    function open() {
        query.text = ""
        query.focus = true
        sheet.visible = true
        list.currentIndex = 0
    }

    function close() {
        sheet.visible = false
        sheet.returnFocus()
    }

    readonly property var shown: {
        const usable = sheet.commands.filter(function (cmd) { return cmd.usable })
        const needle = query.text.trim().toLowerCase()
        if (needle.length === 0)
            return usable
        return usable.filter(function (cmd) {
            return cmd.label.toLowerCase().indexOf(needle) >= 0
                || cmd.id.toLowerCase().indexOf(needle) >= 0
        })
    }

    // And once more after the frame has settled.
    //
    // The scope below is the declaration and this is the check on it: a sheet
    // that is drawn without the keyboard is a sheet whose question is being
    // answered into the window behind it, and that is a failure with no symptom
    // except the wrong thing happening. Cheap, idempotent, and it runs after
    // everything else this frame had to say about the focus.
    onVisibleChanged: if (sheet.visible) Qt.callLater(sheet.takeTheKeyboard)

    function takeTheKeyboard() {
        if (sheet.visible)
            query.forceActiveFocus()
    }

    MouseArea {
        anchors.fill: parent
        onClicked: sheet.close()
    }

    // A focus scope, and the query field takes the keyboard from it.
    //
    // Declared rather than forced at the end of `open`: an imperative
    // call races whatever else the same tick is doing with the focus,
    // and a sheet that is up without the keyboard sends what is typed
    // into it to the window behind, where the letters are commands.
    FocusScope {
        id: scope
        anchors.fill: parent
        focus: sheet.visible

        // Escape from anywhere inside, so the way out never depends on where
        // the keyboard happens to be.
        Keys.onPressed: function (event) {
            if (event.key === Qt.Key_Escape) {
                sheet.close()
                event.accepted = true
            }
        }

        Rectangle {
            width: Math.min(520, parent.width - 48)
            height: Math.min(380, 68 + list.contentHeight)
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.top
            anchors.topMargin: 80
            radius: Theme.rounding
            color: Theme.panel
            border.width: 1
            border.color: Theme.accent

            Accessible.role: Accessible.Dialog
            Accessible.name: "Commands"
            MouseArea {
                anchors.fill: parent
                onClicked: function (mouse) { mouse.accepted = true }
            }

            Column {
                anchors.fill: parent
                anchors.margins: 10
                spacing: 8

                Rectangle {
                    width: parent.width
                    height: 28
                    radius: Theme.rounding
                    color: Theme.sunken
                    border.width: 1
                    border.color: Theme.fill(Theme.accent, 0.45)

                    Text {
                        id: lead
                        anchors.left: parent.left
                        anchors.leftMargin: 9
                        anchors.verticalCenter: parent.verticalCenter
                        text: ":"
                        color: Theme.accent
                        font.family: Theme.fontFamily
                        font.pixelSize: 13
                        renderType: Text.NativeRendering
                        Accessible.ignored: true
                    }

                    Text {
                        anchors.left: lead.right
                        anchors.leftMargin: 6
                        anchors.verticalCenter: parent.verticalCenter
                        visible: query.text === ""
                        text: "run a command"
                        color: Theme.dim
                        font.family: Theme.fontFamily
                        font.pixelSize: 13
                        renderType: Text.NativeRendering
                        Accessible.ignored: true
                    }

                    TextInput {
                        id: query
                        objectName: "paletteQuery"
                        // The scope's own focus item: it has the keyboard for
                        // exactly as long as the sheet is up.
                        focus: true
                        anchors.fill: parent
                        anchors.leftMargin: lead.width + 15
                        anchors.rightMargin: 9
                        verticalAlignment: TextInput.AlignVCenter
                        color: Theme.foreground
                        selectionColor: Theme.fill(Theme.accent, 0.35)
                        selectedTextColor: Theme.foreground
                        font.family: Theme.fontFamily
                        font.pixelSize: 13
                        renderType: Text.NativeRendering
                        clip: true
                        Accessible.name: "command query"
                        Keys.onPressed: function (event) {
                            if (event.key === Qt.Key_Escape) {
                                sheet.close()
                                event.accepted = true
                            // The arrows and the readline pair. Not `j` and `k`:
                            // this is a text field, and a field that eats two
                            // letters is a field you cannot type `limit` into.
                            } else if (event.key === Qt.Key_Down
                                       || (event.modifiers === Qt.ControlModifier
                                           && event.key === Qt.Key_N)) {
                                list.incrementCurrentIndex()
                                event.accepted = true
                            } else if (event.key === Qt.Key_Up
                                       || (event.modifiers === Qt.ControlModifier
                                           && event.key === Qt.Key_P)) {
                                list.decrementCurrentIndex()
                                event.accepted = true
                            } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                                if (list.currentIndex >= 0 && list.currentIndex < sheet.shown.length)
                                    sheet.chosen(sheet.shown[list.currentIndex].id)
                                event.accepted = true
                            }
                        }
                    }
                }

                ListView {
                    id: list
                    objectName: "paletteList"
                    width: parent.width
                    height: parent.height - 36
                    clip: true
                    interactive: false
                    model: sheet.shown
                    currentIndex: 0
                    onModelChanged: list.currentIndex = 0
                    Accessible.role: Accessible.List

                    delegate: Rectangle {
                        id: entryRow
                        required property var modelData
                        required property int index
                        width: list.width
                        height: 28
                        radius: Theme.rounding
                        color: list.currentIndex === entryRow.index ? Theme.fill(Theme.accent, 0.18)
                                                                    : "transparent"

                        Label {
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.left: parent.left
                            anchors.leftMargin: 9
                            anchors.right: shortcut.left
                            anchors.rightMargin: 8
                            text: entryRow.modelData.label
                        }
                        KeyCap {
                            id: shortcut
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.right: parent.right
                            anchors.rightMargin: 8
                            visible: (entryRow.modelData.key || "") !== ""
                            key: entryRow.modelData.key || ""
                        }
                        MouseArea {
                            anchors.fill: parent
                            hoverEnabled: true
                            preventStealing: true
                            onEntered: list.currentIndex = entryRow.index
                            onClicked: {
                                list.currentIndex = entryRow.index
                                sheet.chosen(entryRow.modelData.id)
                            }
                        }
                    }
                }
            }
        }
    }
}
