pragma ComponentBehavior: Bound

import QtQuick
import omahouse

// A hairline. Written once because the window needs several and several copies
// of the same literal drift apart the moment one of them is tuned.
Rectangle {
    property bool vertical: false
    width: vertical ? 1 : (parent ? parent.width : 0)
    height: vertical ? (parent ? parent.height : 0) : 1
    color: Theme.line
}
