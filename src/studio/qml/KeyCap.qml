pragma ComponentBehavior: Bound

import QtQuick
import omahouse

// One key, drawn as a key. A word set in inverse video reads as something you
// press; the same word in running text reads as something you are being told.
Rectangle {
    id: cap
    property string key: ""

    width: capText.implicitWidth + 9
    height: 15
    radius: Math.max(2, Theme.rounding - 2)
    color: Theme.fill(Theme.foreground, 0.10)
    border.width: 1
    border.color: Theme.fill(Theme.foreground, 0.16)

    Text {
        id: capText
        anchors.centerIn: parent
        text: cap.key
        color: Theme.foreground
        opacity: 0.8
        font.family: Theme.fontFamily
        font.pixelSize: 10
        renderType: Text.NativeRendering
    }
}
