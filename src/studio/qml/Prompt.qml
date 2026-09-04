pragma ComponentBehavior: Bound

import QtQuick
import omahouse

// One question, one answer. New profile, minutes a day, more time today, the
// day's total.
//
// It carries its two chips as well as its two keys, because the rule of this
// window is that nothing exists on only one of them: Enter and the `ok` chip run
// the same function, Escape and `cancel` the same one. The chips print their key
// on their face, so somebody using the mouse learns the keyboard by using the
// program.
//
// A prompt that will not accept an empty answer says so on its own button rather
// than by doing nothing when it is pressed.
Rectangle {
    id: sheet

    Accessible.ignored: true

    property string title: ""
    property string hint: ""
    property string lead: ""
    property bool allowEmpty: false
    property var returnFocus: function () {}
    /// What the answer is for. The window reads it back when the answer arrives,
    /// so one prompt serves every question instead of one prompt per question.
    property string topic: ""
    signal answered(string topic, string text)

    visible: false
    color: Theme.fill(Theme.background, 0.55)
    z: 90

    readonly property bool ready: sheet.allowEmpty || field.text.trim() !== ""

    function ask(topic, title, hint, lead, initial, allowEmpty) {
        sheet.topic = topic
        sheet.title = title
        sheet.hint = hint
        sheet.lead = lead
        sheet.allowEmpty = allowEmpty
        field.text = initial
        // The field is this scope's focus item again, every time it is asked.
        //
        // A FocusScope remembers which of its children last had the keyboard,
        // and clicking `ok` gives it to that chip -- so the next prompt would
        // open with the keyboard on the button and the answer typed into the
        // window behind. Set here as a property and not forced: the scope hands
        // the keyboard to its focus item whenever it becomes active, so the two
        // can happen in either order.
        field.focused = true
        sheet.visible = true
    }

    function accept() {
        if (!sheet.ready)
            return
        const answer = field.text.trim()
        const topic = sheet.topic
        sheet.close()
        sheet.answered(topic, answer)
    }

    function close() {
        sheet.visible = false
        sheet.returnFocus()
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
            field.focusEntry()
    }

    MouseArea {
        anchors.fill: parent
        onClicked: sheet.close()
    }

    // A focus scope, and the field takes the keyboard from it.
    //
    // Not a `forceActiveFocus()` at the end of `ask`: that is one imperative
    // call racing whatever else the same tick is doing with the focus, and it
    // loses often enough to matter. A prompt that is up without the keyboard
    // swallows the first letters of the answer and hands them to the window
    // behind it, where `m` is a command and `a` is another. Declared, the field
    // has the keyboard for exactly as long as the sheet is up, whatever order
    // the rest of the frame happens in.
    FocusScope {
        id: scope
        anchors.fill: parent
        focus: sheet.visible

        // Escape and Enter from anywhere inside, including from the two chips
        // once Tab has been used to reach them. A control that swallowed the
        // way out would be a place Tab can get into and nothing can get out of.
        Keys.onPressed: function (event) {
            if (event.key === Qt.Key_Escape) {
                sheet.close()
                event.accepted = true
            } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                sheet.accept()
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
            border.color: Theme.accent

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
                    visible: sheet.hint !== ""
                    quiet: true
                    text: sheet.hint
                    elide: Text.ElideNone
                    wrapMode: Text.WordWrap
                }
                Entry {
                    id: field
                    objectName: "promptField"
                    // The scope's own focus item, so it has the keyboard for
                    // exactly as long as the scope does.
                    focused: true
                    onEditingChanged: if (field.editing) field.focusEntry()
                    width: parent.width
                    lead: sheet.lead
                    placeholder: sheet.title
                    onAccepted: sheet.accept()
                    onCancelled: sheet.close()
                }
                Row {
                    anchors.right: parent.right
                    spacing: 8

                    Chip {
                        objectName: "promptCancel"
                        label: "cancel"
                        key: "Esc"
                        quiet: true
                        onClicked: sheet.close()
                    }
                    Chip {
                        objectName: "promptOk"
                        label: "ok"
                        key: "Enter"
                        on: sheet.ready
                        usable: sheet.ready
                        onClicked: sheet.accept()
                    }
                }
            }
        }
    }
}
