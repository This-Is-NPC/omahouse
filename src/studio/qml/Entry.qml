pragma ComponentBehavior: Bound

import QtQuick
import omahouse

// The one text box in the window. The filter bar and every prompt are this,
// rather than bare TextInputs styled separately and free to drift.
//
// The border is the focus ring: for a field, the ring and the frame are the same
// edge, and drawing both would be saying it twice.
Rectangle {
    id: box

    property alias text: entry.text
    property string lead: ""
    property string placeholder: ""
    readonly property alias editing: entry.activeFocus
    /// Whether this is the field its focus scope hands the keyboard to. The
    /// declarative half of `focusEntry`, and the half that does not race.
    property alias focused: entry.focus
    signal accepted(string text)
    signal cancelled

    function focusEntry() {
        entry.forceActiveFocus()
        entry.selectAll()
    }

    implicitHeight: 26
    radius: Theme.rounding
    color: Theme.sunken
    border.width: 1
    border.color: entry.activeFocus ? Theme.accent : Theme.fill(Theme.foreground, 0.18)

    Behavior on border.color { ColorAnimation { duration: 90 } }

    Text {
        id: leadMark
        visible: box.lead !== ""
        anchors.left: parent.left
        anchors.leftMargin: 9
        anchors.verticalCenter: parent.verticalCenter
        text: box.lead
        color: Theme.accent
        font.family: Theme.fontFamily
        font.pixelSize: 12
        renderType: Text.NativeRendering
    }

    TextInput {
        id: entry
        objectName: "entry"
        anchors.fill: parent
        anchors.leftMargin: box.lead === "" ? 9 : leadMark.width + 15
        anchors.rightMargin: 9
        verticalAlignment: TextInput.AlignVCenter
        clip: true
        color: Theme.foreground
        selectionColor: Theme.fill(Theme.accent, 0.35)
        selectedTextColor: Theme.foreground
        font.family: Theme.fontFamily
        font.pixelSize: 12
        renderType: Text.NativeRendering

        Accessible.role: Accessible.EditableText
        Accessible.name: box.placeholder

        Text {
            anchors.verticalCenter: parent.verticalCenter
            visible: entry.text === "" && !entry.activeFocus
            text: box.placeholder
            color: Theme.dim
            font.family: Theme.fontFamily
            font.pixelSize: 12
            renderType: Text.NativeRendering
        }

        Keys.onPressed: function (event) {
            if (event.key === Qt.Key_Escape) {
                box.cancelled()
                event.accepted = true
            } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                box.accepted(entry.text)
                event.accepted = true
            }
        }
    }
}
