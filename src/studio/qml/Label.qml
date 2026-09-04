pragma ComponentBehavior: Bound

import QtQuick
import omahouse

// Text in the window's voice. `quiet` is the difference between a value and the
// word naming it, and it is the only type scale this window has.
Text {
    property bool quiet: false
    color: quiet ? Theme.dim : Theme.foreground
    font.family: Theme.fontFamily
    font.pixelSize: quiet ? 10 : 12
    elide: Text.ElideRight
    renderType: Text.NativeRendering
}
