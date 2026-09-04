pragma ComponentBehavior: Bound

import QtQuick
import omahouse

// Shared tab focus, keyboard activation, inner focus ring, and pointer shell.
// Host types set chrome colours and content; Escape is not accepted here.
//
// Carried over from omafiles, minus its tooltip: the controls in this window
// print their key on their face instead of hiding it behind a hover, because
// "where there is a button there is a key" is a promise better kept in sight
// than in a tip nobody with a keyboard will ever see.
Item {
    id: control

    property bool usable: true
    property bool tabFocusable: control.usable
    property bool handCursor: true
    property Item focusRingSource: control
    property Item pointerFocus: control

    readonly property color hoverChromeFill: Theme.fill(Theme.foreground, 0.08)
    readonly property color hoverChromeBorder: Theme.fill(Theme.foreground, 0.25)

    signal activated()

    default property alias content: contentLayer.data

    property alias color: chrome.color
    property alias radius: chrome.radius
    property alias border: chrome.border
    readonly property alias hovered: hover.hovered

    enabled: control.usable

    Accessible.focusable: control.tabFocusable
    activeFocusOnTab: control.tabFocusable

    onUsableChanged: if (!control.usable && control.activeFocus)
        nextItemInFocusChain(true).forceActiveFocus()

    Keys.onSpacePressed: function (event) {
        if (control.usable)
            control.activated()
        event.accepted = true
    }
    Keys.onReturnPressed: function (event) {
        if (control.usable)
            control.activated()
        event.accepted = true
    }
    Keys.onEnterPressed: function (event) {
        if (control.usable)
            control.activated()
        event.accepted = true
    }
    // Escape is deliberately not accepted: it travels up to the window, which
    // sends the keyboard back to the list. A control that swallowed it would be
    // a place Tab can get into and nothing can get out of.

    Rectangle {
        id: chrome
        anchors.fill: parent
        z: -1

        Behavior on color { ColorAnimation { duration: 90 } }
        Behavior on border.color { ColorAnimation { duration: 90 } }
    }

    Item {
        id: contentLayer
        anchors.fill: parent
    }

    // Inside the control, not around it: chips sit at the edge of clipped rows
    // where a ring drawn outside loses two of its sides.
    Rectangle {
        anchors.fill: parent
        anchors.margins: 2
        radius: Math.max(0, Theme.rounding - 1)
        color: "transparent"
        border.width: 1
        border.color: Qt.colorEqual(chrome.border.color, Theme.accent) ? Theme.foreground
                                                                       : Theme.accent
        visible: control.focusRingSource.activeFocus
        z: 2
    }

    HoverHandler {
        id: hover
        cursorShape: control.handCursor && control.usable ? Qt.PointingHandCursor : Qt.ArrowCursor
    }
    TapHandler {
        enabled: control.usable
        onTapped: {
            control.pointerFocus.forceActiveFocus()
            control.activated()
        }
    }
}
