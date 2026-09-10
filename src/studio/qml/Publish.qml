pragma ComponentBehavior: Bound
import QtQuick
import omahouse

Rectangle {
    id: sheet
    objectName: "publishSheet"
    visible: false
    z: 90
    color: Theme.fill(Theme.background, 0.55)
    Accessible.ignored: true
    property var rows: []
    property var selected: ({})
    property var returnFocus: function () {}
    property string personName: ""
    property bool waiting: false
    property bool finished: false
    property var output: []
    signal chosen(var machines)

    function open(machines, name) {
        sheet.rows = machines
        sheet.personName = name
        const selection = {}
        for (const row of machines)
            selection[row.machine] = row.state === "behind" || row.state === "never published"
        sheet.selected = selection
        sheet.waiting = false
        sheet.finished = false
        sheet.output = []
        list.currentIndex = 0
        sheet.visible = true
        Qt.callLater(function () { scope.forceActiveFocus() })
    }
    function close() {
        sheet.visible = false
        sheet.returnFocus()
    }
    function allowed(row) {
        return row && row.state !== "changed there" && row.state !== "not paired"
    }
    function toggle(index) {
        const row = sheet.rows[index]
        if (!sheet.allowed(row) || sheet.waiting || sheet.finished)
            return
        const selection = Object.assign({}, sheet.selected)
        selection[row.machine] = !selection[row.machine]
        sheet.selected = selection
    }
    readonly property var targets: sheet.rows.filter(function (row) {
        return sheet.allowed(row) && sheet.selected[row.machine]
    }).map(function (row) { return row.machine })
    function submit() {
        if (sheet.waiting || sheet.finished || sheet.targets.length === 0)
            return
        sheet.waiting = true
        sheet.chosen(sheet.targets)
    }
    function showResult() {
        if (!sheet.waiting)
            return
        sheet.waiting = false
        sheet.finished = true
        sheet.output = Admin.output.length ? Admin.output : [Admin.message]
    }
    MouseArea { anchors.fill: parent; onClicked: sheet.close() }
    FocusScope {
        id: scope
        anchors.fill: parent
        focus: sheet.visible
        Keys.onPressed: function (event) {
            if (event.key === Qt.Key_Escape) sheet.close()
            else if (event.key === Qt.Key_Down || event.key === Qt.Key_J)
                list.currentIndex = Math.min(sheet.rows.length - 1, list.currentIndex + 1)
            else if (event.key === Qt.Key_Up || event.key === Qt.Key_K)
                list.currentIndex = Math.max(0, list.currentIndex - 1)
            else if (event.key === Qt.Key_Space) sheet.toggle(list.currentIndex)
            else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                if (sheet.finished) sheet.close()
                else sheet.submit()
            } else return
            event.accepted = true
        }
        Rectangle {
            width: Math.min(680, parent.width - 40)
            height: Math.min(470, parent.height - 40)
            anchors.centerIn: parent
            color: Theme.panel
            border.color: Theme.fill(Theme.foreground, 0.2)
            radius: Theme.rounding
            MouseArea { anchors.fill: parent }
            Column {
                anchors.fill: parent
                anchors.margins: 24
                spacing: 14
                Label { text: "Publish · " + sheet.personName; font.pixelSize: 22 }
                Label {
                    width: parent.width
                    wrapMode: Text.WordWrap
                    quiet: true
                    text: sheet.finished ? "Publication result"
                          : sheet.waiting ? "Checking the household and publishing…"
                          : "Choose the computers that should receive these rules."
                }
                ListView {
                    id: list
                    objectName: "publishList"
                    visible: !sheet.finished
                    width: parent.width
                    height: parent.height - 125
                    clip: true
                    model: sheet.rows
                    delegate: Rectangle {
                        id: choice
                        required property var modelData
                        required property int index
                        width: list.width
                        height: 52
                        color: list.currentIndex === choice.index ? Theme.fill(Theme.accent, 0.16) : "transparent"
                        Label {
                            anchors.fill: parent
                            anchors.margins: 10
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideRight
                            quiet: !sheet.allowed(choice.modelData)
                            text: (sheet.selected[choice.modelData.machine] ? "[x] " : "[ ] ")
                                  + choice.modelData.machine + " · "
                                  + (choice.modelData.state === "changed there" ? "resolve first" : choice.modelData.state)
                        }
                        TapHandler {
                            onTapped: {
                                list.currentIndex = choice.index
                                sheet.toggle(choice.index)
                            }
                        }
                    }
                }
                Flickable {
                    visible: sheet.finished
                    width: parent.width
                    height: parent.height - 125
                    contentHeight: results.height
                    clip: true
                    Label {
                        id: results
                        width: parent.width
                        wrapMode: Text.WrapAnywhere
                        text: sheet.output.join("\n")
                    }
                }
                Row {
                    spacing: 14
                    Chip {
                        objectName: "publishConfirm"
                        key: "enter"
                        label: sheet.finished ? "close" : "publish"
                        usable: sheet.finished || (!sheet.waiting && sheet.targets.length > 0)
                        onActivated: { if (sheet.finished) sheet.close(); else sheet.submit() }
                    }
                    Label {
                        anchors.verticalCenter: parent.verticalCenter
                        quiet: true
                        text: sheet.finished ? "esc close"
                              : sheet.waiting ? "publication in progress · esc hide"
                              : "space select · esc cancel"
                    }
                }
            }
        }
    }
}
