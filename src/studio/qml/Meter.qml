pragma ComponentBehavior: Bound

import QtQuick
import omahouse

// How much of a budget is gone, as a bar.
//
// The number beside it is the truth and this is the glance: a balance is the one
// thing on this window somebody reads from across a room, and "1h20m of 2h" is a
// sentence you have to do arithmetic on before you know whether to worry.
//
// A budget with no limit gets no bar at all rather than an empty one. An empty
// bar reads as "none of it used" and the honest answer is that there is nothing
// to fill.
Item {
    id: meter

    property int spentSeconds: 0
    property int allowanceSeconds: 0
    property bool limited: true
    property bool alarm: false

    readonly property real portion: !meter.limited || meter.allowanceSeconds <= 0
        ? 0
        : Math.max(0, Math.min(1, meter.spentSeconds / meter.allowanceSeconds))

    implicitHeight: 4
    visible: meter.limited && meter.allowanceSeconds > 0

    Accessible.role: Accessible.ProgressBar
    Accessible.name: Math.round(meter.portion * 100) + " per cent spent"

    Rectangle {
        anchors.fill: parent
        radius: height / 2
        color: Theme.fill(Theme.foreground, 0.10)
    }
    Rectangle {
        height: parent.height
        width: parent.width * meter.portion
        radius: height / 2
        color: meter.alarm ? Theme.urgent
             : meter.portion > 0.85 ? Theme.urgent
                                    : Theme.accent
        Behavior on width { NumberAnimation { duration: 220 } }
    }
}
