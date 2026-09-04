pragma ComponentBehavior: Bound

import QtQuick
import omahouse

// The bottom line: where the cursor is and what this window cannot see on the
// left, what to press on the right.
//
// Keys are drawn as keys rather than set as prose, and the list changes with
// what is happening instead of naming list keys while you type in a field.
//
// The blind spot has a permanent place here rather than a warning that appears
// when it is bad. spec.md §5 asks omahouse to report what it cannot account for,
// and the number is never zero on a live session -- the compositor's own
// processes are in it. Said plainly and always, it reads as the honest limit it
// is; said only sometimes, it would read as an alarm.
Rectangle {
    id: bar

    property int cursor: -1
    property int total: 0
    property string note: ""
    property int blind: 0
    property string message: ""
    property bool alarm: false
    property var hints: []

    readonly property string spoken: {
        const parts = []
        parts.push(bar.total === 0 || bar.cursor < 0 ? "0/" + bar.total
                                                     : (bar.cursor + 1) + "/" + bar.total)
        if (bar.note !== "")
            parts.push(bar.note)
        if (bar.blind > 0)
            parts.push(bar.blind + " processes it cannot see")
        if (bar.message !== "") {
            parts.push(bar.message)
        } else {
            for (let i = 0; i < bar.hints.length; i++)
                parts.push(bar.hints[i].key + " " + bar.hints[i].label)
        }
        return parts.join(", ")
    }

    height: 26
    color: Theme.panel

    Accessible.role: Accessible.StaticText
    Accessible.name: bar.spoken
    Accessible.description: bar.spoken

    Row {
        id: facts
        anchors.left: parent.left
        anchors.leftMargin: 12
        anchors.verticalCenter: parent.verticalCenter
        spacing: 14

        Label {
            quiet: true
            font.pixelSize: 11
            text: bar.total === 0 || bar.cursor < 0 ? "0/" + bar.total
                                                    : (bar.cursor + 1) + "/" + bar.total
        }
        Label {
            objectName: "statusNote"
            visible: bar.note !== ""
            quiet: true
            font.pixelSize: 11
            text: bar.note
        }
        Label {
            objectName: "statusBlind"
            visible: bar.blind > 0
            quiet: true
            font.pixelSize: 11
            text: bar.blind + " it cannot see"
        }
    }

    // The message takes the whole right side when there is one.
    Label {
        objectName: "statusMessage"
        visible: bar.message !== ""
        anchors.left: facts.right
        anchors.leftMargin: 16
        anchors.right: parent.right
        anchors.rightMargin: 12
        anchors.verticalCenter: parent.verticalCenter
        color: bar.alarm ? Theme.urgent : Theme.foreground
        font.pixelSize: 11
        text: bar.message
        horizontalAlignment: Text.AlignRight
    }

    Hints {
        visible: bar.message === ""
        anchors.left: facts.right
        anchors.leftMargin: 16
        anchors.right: parent.right
        anchors.rightMargin: 12
        anchors.verticalCenter: parent.verticalCenter
        model: bar.hints
    }
}
