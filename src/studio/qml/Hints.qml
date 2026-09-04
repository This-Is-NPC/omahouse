pragma ComponentBehavior: Bound

import QtQuick
import omahouse

// The key hints along the bottom, the way a terminal program does it: the key,
// then the name of what it does.
//
// The row is right-aligned and grows leftwards, so the hints written first are
// the first to run out of room; put the least important there. One that will not
// fit is dropped whole rather than sliced through a word, and it is dropped with
// opacity rather than `visible`, because a Row closes the gap left by an
// invisible child: the next hint would slide in, fit, appear, push the first one
// back out, and the bar would flicker between two layouts.
Item {
    id: hints
    property var model: []

    implicitHeight: 16
    clip: true

    Row {
        id: strip
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        spacing: 11

        Repeater {
            model: hints.model

            Row {
                id: pair
                required property var modelData
                spacing: 5
                opacity: hints.width - strip.width + pair.x >= 0 ? 1 : 0
                Behavior on opacity { NumberAnimation { duration: 90 } }

                KeyCap {
                    anchors.verticalCenter: parent.verticalCenter
                    key: pair.modelData.key
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: pair.modelData.label
                    color: Theme.dim
                    font.family: Theme.fontFamily
                    font.pixelSize: 11
                    renderType: Text.NativeRendering
                }
            }
        }
    }
}
