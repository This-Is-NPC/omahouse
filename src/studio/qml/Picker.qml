pragma ComponentBehavior: Bound

import QtQuick
import omahouse

// The programs on this machine, to pick one to release.
//
// Two lists in one, and the order is the point. What is open in that account
// right now comes first, because a running scope is the ground truth -- it
// exists, it has that id, and what is inside it can be read. The `.desktop`
// entries under it are the programs that would get a scope the next time they
// are launched, and their id is inferred from the file name.
//
// And it says when a scope is not what its name says. spec.md §5: an app started
// through a shim takes the shim's name, so releasing `gtk-launch` is releasing
// whatever it launches next. The line under the id is what is really running in
// there, in the urgent colour when the two disagree. An operator cannot weigh
// that without seeing it, and this window will not let them write the rule
// without being shown.
Rectangle {
    id: sheet

    Accessible.ignored: true

    property var programs: []
    property var returnFocus: function () {}
    signal picked(string id)

    visible: false
    color: Theme.fill(Theme.background, 0.55)
    z: 90

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

    function take() {
        if (list.currentIndex >= 0 && list.currentIndex < sheet.shown.length) {
            const id = sheet.shown[list.currentIndex].id
            sheet.close()
            sheet.picked(id)
        }
    }

    readonly property var shown: {
        const needle = query.text.trim().toLowerCase()
        if (needle.length === 0)
            return sheet.programs
        return sheet.programs.filter(function (app) {
            return app.name.toLowerCase().indexOf(needle) >= 0
                || app.id.toLowerCase().indexOf(needle) >= 0
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
            width: Math.min(620, parent.width - 48)
            height: Math.min(460, parent.height - 120)
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.top
            anchors.topMargin: 70
            radius: Theme.rounding
            color: Theme.panel
            border.width: 1
            border.color: Theme.accent

            Accessible.role: Accessible.Dialog
            Accessible.name: "Programs"
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
                        text: "+"
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
                        text: "which program"
                        color: Theme.dim
                        font.family: Theme.fontFamily
                        font.pixelSize: 13
                        renderType: Text.NativeRendering
                        Accessible.ignored: true
                    }
                    TextInput {
                        id: query
                        objectName: "pickerQuery"
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
                        Accessible.name: "program query"
                        Keys.onPressed: function (event) {
                            if (event.key === Qt.Key_Escape) {
                                sheet.close()
                                event.accepted = true
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
                                sheet.take()
                                event.accepted = true
                            }
                        }
                    }
                }

                ListView {
                    id: list
                    objectName: "pickerList"
                    width: parent.width
                    height: parent.height - 36
                    clip: true
                    model: sheet.shown
                    currentIndex: 0
                    onModelChanged: list.currentIndex = 0
                    Accessible.role: Accessible.List

                    delegate: Rectangle {
                        id: appRow
                        required property var modelData
                        required property int index
                        width: list.width
                        height: 40
                        radius: Theme.rounding
                        color: list.currentIndex === appRow.index ? Theme.fill(Theme.accent, 0.18)
                                                                  : "transparent"

                        Column {
                            anchors.left: parent.left
                            anchors.leftMargin: 9
                            anchors.right: marks.left
                            anchors.rightMargin: 8
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 1

                            Label {
                                width: parent.width
                                text: appRow.modelData.name === appRow.modelData.id
                                      ? appRow.modelData.id
                                      : appRow.modelData.name + "  ·  " + appRow.modelData.id
                            }
                            Label {
                                width: parent.width
                                visible: appRow.modelData.exe !== ""
                                quiet: true
                                color: appRow.modelData.disagrees ? Theme.urgent : Theme.dim
                                text: appRow.modelData.disagrees
                                      ? "that name is the launcher — inside it: "
                                        + appRow.modelData.exe
                                      : appRow.modelData.exe
                            }
                        }

                        Row {
                            id: marks
                            anchors.right: parent.right
                            anchors.rightMargin: 9
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 6

                            Label {
                                anchors.verticalCenter: parent.verticalCenter
                                visible: appRow.modelData.listed
                                quiet: true
                                color: Theme.accent
                                text: "already listed"
                            }
                            Label {
                                anchors.verticalCenter: parent.verticalCenter
                                visible: appRow.modelData.running
                                quiet: true
                                text: "open now"
                            }
                        }

                        MouseArea {
                            anchors.fill: parent
                            hoverEnabled: true
                            preventStealing: true
                            onEntered: list.currentIndex = appRow.index
                            onClicked: {
                                list.currentIndex = appRow.index
                                sheet.take()
                            }
                        }
                    }
                }
            }
        }
    }
}
