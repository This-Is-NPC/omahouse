pragma ComponentBehavior: Bound

import QtQuick
import omahouse

// A control, in omarchy's shape: a translucent wash of a role over the surface
// with a hairline border rather than a second opaque colour.
//
// It carries its key on its face. Every action in this window is one row of one
// table -- the window's `commands` -- drawn here as a chip and read there by the
// keyboard, so a chip without a key would be an action the keyboard cannot
// reach and a key without a chip an action the mouse cannot. Printing the key
// where the button is is what makes that visible instead of merely true.
//
// Uses FocusableControl for tab focus, keyboard activation, pointer handling,
// and the inner focus ring.
FocusableControl {
    id: chip

    property string label: ""
    property string key: ""
    property bool on: false
    property bool quiet: false
    property color role: Theme.accent
    signal clicked

    opacity: usable ? 1 : 0.35
    onActivated: chip.clicked()

    Accessible.role: Accessible.Button
    Accessible.name: chip.key === "" ? chip.label : chip.label + ", key " + chip.key
    Accessible.checkable: chip.on
    Accessible.checked: chip.on

    implicitWidth: row.implicitWidth + (chip.quiet ? 14 : 16)
    implicitHeight: 22
    width: implicitWidth
    height: implicitHeight
    radius: Theme.rounding

    color: !chip.usable ? "transparent"
         : chip.on ? Theme.fill(chip.role, chip.quiet ? 0.16 : 0.18)
         : chip.hovered ? chip.hoverChromeFill
         : chip.quiet ? "transparent"
                      : Theme.fill(Theme.foreground, 0.04)

    border.width: 1
    border.color: chip.on ? (chip.quiet ? Theme.fill(chip.role, 0.55) : chip.role)
                : chip.hovered ? chip.hoverChromeBorder
                : chip.quiet ? "transparent"
                             : Theme.fill(Theme.foreground, 0.18)

    Row {
        id: row
        anchors.centerIn: parent
        spacing: 6

        KeyCap {
            anchors.verticalCenter: parent.verticalCenter
            visible: chip.key !== ""
            key: chip.key
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: chip.label
            color: Theme.foreground
            // Dimmed at rest so a row of chips reads as one line of text with
            // one word lit, rather than as six equal buttons.
            opacity: chip.on ? 1 : (chip.hovered || chip.activeFocus ? 0.95 : 0.66)
            font.family: Theme.fontFamily
            font.pixelSize: 11
            renderType: Text.NativeRendering
            Behavior on opacity { NumberAnimation { duration: 90 } }
        }
    }
}
