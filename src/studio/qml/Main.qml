pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Window
import omahouse

// The studio window.
//
// One rule shapes the whole file: **everything is on the keyboard, and the mouse
// does exactly the same thing.** Not "the important things are on both" -- every
// one of them, by construction rather than by discipline.
//
// The construction is `commands`. It is one array of rows, each with an id, a
// key, a label and whether it is usable right now. `handleKey` looks a keystroke
// up in it; the chip bar draws it; the palette under `:` lists it; the sheet
// under `?` prints it. All four go through `perform`. So an action cannot be
// added to a button without a key, or to a key without a button, because there
// is nowhere to add it that is not both.
//
// The keyboard hub is the same idea. `root` holds the focus whenever the window
// is not in a field or a sheet, and every key is decided in `handleKey` rather
// than spread across the three views -- which is how a key ends up working in
// one of them and not the others. The views own no keys at all.
//
// Two faces, and nobody chooses. `House.face` is `operator` for somebody in
// wheel and `subject` for everybody else, and the subject face is the same
// window with `commands` empty: the same rows, the same keys to walk them, and
// nothing to press. A read-only window built out of a second set of components
// would be a second window to keep in step.
Window {
    id: win
    width: 1100
    height: 700
    minimumWidth: 820
    minimumHeight: 460
    visible: true
    title: "omahouse"
    color: Theme.background
    onClosing: Qt.quit()

    // ------------------------------------------------------------------ state
    readonly property bool operating: House.face === "operator"
    readonly property var people: House.people
    property int view: 1
    property int cursorPeople: 0
    property int cursorPrograms: 0
    property int cursorToday: 0
    property string filter: ""
    property bool filtering: false
    /// What `minutes`, `grant` and `drop` are about, kept from the moment the
    /// prompt opens: the cursor is free to move while a sheet is up, and an
    /// answer that read the cursor back would land on whatever row is under it
    /// by then.
    property string target: ""
    property string targetKind: ""

    readonly property bool blocked: helpSheet.visible || commandPalette.visible
                                    || prompt.visible || confirm.visible || picker.visible
                                    || win.filtering

    // Where a cursor really is, as opposed to where it was last put.
    //
    // The three of them are kept as plain numbers and read back through this,
    // because the lists under them change while nobody is touching the keyboard:
    // a profile is taken off the books, a filter is typed, a budget is added.
    // A cursor left pointing past the end of a shorter list is a window with no
    // row highlighted and a `+` that does nothing, and the person pressing it
    // has no way to know why.
    function clamp(at, count) {
        return count === 0 ? -1 : Math.max(0, Math.min(at, count - 1))
    }

    readonly property int peopleCursor: win.clamp(win.cursorPeople, win.peopleRows.length)
    readonly property int programCursor: win.clamp(win.cursorPrograms, win.programRows.length)
    readonly property int todayCursor: win.clamp(win.cursorToday, win.todayRows.length)

    // The profile the window is about follows the cursor of the first view, and
    // is not a selection kept beside it. One less thing that can be out of step
    // with what is on screen; and on the subject face, where the list is their
    // own profile and nothing else, it needs no special case at all.
    // `undefined` is coerced to `null` here and in `currentRow`. `rows` and
    // `cursor` are two bindings over one list, and for the instant between them
    // the cursor can be an index the list no longer has -- so a `row !== null`
    // guard written the obvious way would let an `undefined` straight through
    // into a property read.
    readonly property var person: win.peopleCursor < 0
        ? null : (win.peopleRows[win.peopleCursor] || null)
    readonly property string subject: win.person === null ? "" : win.person.user

    readonly property var allPrograms: win.subject === "" ? []
        : (House.snapshot.programs[win.subject] || [])
    readonly property var allToday: win.subject === "" ? []
        : (House.snapshot.today[win.subject] || [])
    readonly property var catalogue: win.subject === "" ? []
        : (House.snapshot.catalog[win.subject] || [])

    function matches(row) {
        const needle = win.filter.trim().toLowerCase()
        if (needle === "")
            return true
        const parts = [row.id || "", row.name || "", row.user || "", row.text || ""]
        for (let i = 0; i < parts.length; i++) {
            if (String(parts[i]).toLowerCase().indexOf(needle) >= 0)
                return true
        }
        return false
    }

    readonly property var peopleRows: win.people.filter(win.matches)
    readonly property var programRows: win.allPrograms.filter(win.matches)
    readonly property var todayRows: win.allToday.filter(win.matches)

    readonly property var rows: win.view === 1 ? win.peopleRows
                              : win.view === 2 ? win.programRows
                                               : win.todayRows
    readonly property int cursor: win.view === 1 ? win.peopleCursor
                                 : win.view === 2 ? win.programCursor
                                                  : win.todayCursor
    readonly property var currentRow: win.cursor < 0 || win.cursor >= win.rows.length
        ? null : (win.rows[win.cursor] || null)

    // --------------------------------------------------------------- commands
    //
    // The one table. Everything below draws from it and nothing anywhere else
    // decides what this window can do.
    readonly property var commands: {
        const rows = []
        const person = win.person
        const row = win.currentRow

        rows.push({ id: "open", key: "l", hint: "open",
                    label: "open this profile",
                    usable: win.view === 1 && win.person !== null })
        rows.push({ id: "back", key: "h", hint: "back",
                    label: "back to the people",
                    usable: win.view !== 1 })

        // The subject reads and nothing else. spec.md §8: the fiscalised account
        // is shown what is left today, and the decisions are the operator's.
        if (!win.operating)
            return rows

        if (win.view === 1) {
            rows.push({ id: "new", key: "n", hint: "new profile",
                        label: "put an account under rules", usable: true })
            rows.push({ id: "enforce", key: "e",
                        hint: person && person.enforce ? "watch only" : "close when out",
                        label: person && person.enforce
                               ? "watch only, and close nothing"
                               : "close programs when the time is up",
                        usable: person !== null })
            rows.push({ id: "policy", key: "d",
                        hint: person && person.allowlist ? "all but listed" : "only listed",
                        label: person && person.allowlist
                               ? "let everything run but the listed"
                               : "let only the listed programs run",
                        usable: person !== null })
            rows.push({ id: "forget", key: "x", hint: "off the books",
                        label: "take this account off the books",
                        usable: person !== null })
        } else if (win.view === 2) {
            rows.push({ id: "release", key: "a", hint: "release",
                        label: "release a program", usable: person !== null })
            rows.push({ id: "minutes", key: "m", hint: "minutes",
                        label: "minutes a day for this program",
                        usable: row !== null
                                && (row.hasBudget || row.released) })
            rows.push({ id: "grant", key: "+", hint: "more today",
                        label: "more time today for this program",
                        usable: row !== null && row.hasBudget })
            // Only for something that is on the list. Denying what is already
            // denied is a write that changes nothing, and a chip that is lit
            // for it is a chip that lies about what pressing it would do.
            rows.push({ id: "drop", key: "x", hint: "off the list",
                        label: "take this program off the list",
                        usable: row !== null && row.released })
        } else {
            rows.push({ id: "day", key: "s", hint: "the day",
                        label: "how long the whole day is",
                        usable: person !== null })
            rows.push({ id: "minutes", key: "m", hint: "minutes",
                        label: "minutes a day for this budget",
                        usable: row !== null && row.kind === "budget" })
            rows.push({ id: "grant", key: "+", hint: "more today",
                        label: "more time today for this budget",
                        usable: row !== null && row.kind === "budget" })
        }
        return rows
    }

    function commandFor(key) {
        for (let i = 0; i < win.commands.length; i++) {
            if (win.commands[i].key === key)
                return win.commands[i]
        }
        return null
    }

    // ----------------------------------------------------------------- keymap
    //
    // The sheet under `?` prints this. The second half of it is generated from
    // `commands`, so it cannot promise a key the window does not answer or omit
    // one it does; the first half is written by hand, and a test presses every
    // key on it and requires an effect.
    readonly property var keymap: {
        const move = [
            { key: "j k", label: "down, up" },
            { key: "↓ ↑", label: "down, up" },
            { key: "g G", label: "first, last row" },
            { key: "Home End", label: "first, last row" },
            { key: "PgDn PgUp", label: "page" }
        ]
        const go = [
            { key: "l", label: "open the profile under the cursor" },
            { key: "Enter", label: "open the profile under the cursor" },
            { key: "h", label: "back to the people" },
            { key: "Esc", label: "back, or clear the filter, or leave a control" },
            { key: "1", label: "the people under rules" },
            { key: "2", label: "the programs of the one under the cursor" },
            { key: "3", label: "their day: what is left, and what happened" }
        ]
        const window = [
            { key: "/", label: "filter this list" },
            { key: ":", label: "commands" },
            { key: "?", label: "this list" },
            { key: "Tab", label: "next control" },
            { key: "Space", label: "press the control the keyboard is on" }
        ]
        const here = []
        for (let i = 0; i < win.commands.length; i++) {
            const cmd = win.commands[i]
            if (cmd.id === "open" || cmd.id === "back")
                continue
            here.push({ key: cmd.key, label: cmd.label })
        }
        const groups = [{ title: "move", keys: move },
                        { title: "go", keys: go },
                        { title: "window", keys: window }]
        if (here.length > 0)
            groups.push({ title: "here, right now", keys: here })
        return groups
    }

    readonly property var hints: {
        if (win.filtering)
            return [{ key: "Esc", label: "clear" }, { key: "Enter", label: "keep" }]
        const list = [{ key: "?", label: "keys" },
                      { key: ":", label: "cmd" },
                      { key: "/", label: "filter" }]
        for (let i = 0; i < win.commands.length; i++) {
            const cmd = win.commands[i]
            if (cmd.usable)
                list.push({ key: cmd.key, label: cmd.hint })
        }
        list.push({ key: "j k", label: "move" })
        return list
    }

    // ------------------------------------------------------------------ focus
    //
    // One rule, and it is enforced here rather than trusted to callers: the list
    // takes the keyboard only when nothing is over it.
    //
    // A `forceActiveFocus` is not a request, it is the last word, and it also
    // destroys whatever binding was holding that property -- so a single stray
    // call while a sheet is up leaves the sheet drawn, with a text field in it,
    // and the keyboard behind it in the list, where the answer being typed is
    // read as commands. Refusing here is cheaper than finding every caller that
    // might one day run at the wrong moment.
    function takeFocus() {
        if (win.blocked)
            return
        root.forceActiveFocus()
    }

    function go(which) {
        win.view = which
        win.filter = ""
        win.filtering = false
        win.takeFocus()
    }

    function setCursor(value) {
        const last = win.rows.length - 1
        const at = last < 0 ? 0 : Math.max(0, Math.min(value, last))
        if (win.view === 1)
            win.cursorPeople = at
        else if (win.view === 2)
            win.cursorPrograms = at
        else
            win.cursorToday = at
        if (win.view === 1)
            peopleList.positionViewAtIndex(at, ListView.Contain)
        else if (win.view === 2)
            programList.positionViewAtIndex(at, ListView.Contain)
        else
            todayList.positionViewAtIndex(at, ListView.Contain)
    }

    function moveCursor(delta) {
        win.setCursor(win.cursor + delta)
    }

    function startFilter() {
        win.filtering = true
        filterField.text = win.filter
        filterField.focusEntry()
    }

    function finishFilter(clear) {
        if (clear)
            win.filter = ""
        win.filtering = false
        win.setCursor(win.cursor)
        win.takeFocus()
    }

    // ---------------------------------------------------------------- actions
    function perform(id) {
        Admin.clear()
        const person = win.person
        const row = win.currentRow

        if (id === "open") {
            if (win.person !== null)
                win.go(2)
            return
        }
        if (id === "back") {
            win.go(1)
            return
        }
        // Before the guard below, and the only one that is: putting the first
        // account under rules is the one thing an operator can do on a machine
        // where there is no profile to be standing on.
        if (id === "new") {
            prompt.ask("new", "Which account?",
                       "The user name on this machine. An account in wheel is refused: "
                       + "spec.md §1 makes the operator whoever is in wheel, and a profile "
                       + "for one of them is somebody fiscalising themselves by accident.",
                       "", "", false)
            return
        }

        // Everything below is about the profile under the cursor, and every one
        // of them is marked unusable in the table when there is none -- so this
        // is a second lock on a door already bolted, and it stays because the
        // cost of being wrong is a TypeError in a window somebody is using.
        if (person === null)
            return

        if (id === "enforce") {
            Admin.run(person.enforce ? "watch only" : "close when the time is up",
                      ["profile", "enforce", person.user, person.enforce ? "--off" : "--on"])
        } else if (id === "policy") {
            Admin.run(person.allowlist ? "everything but the listed" : "only the listed",
                      ["profile", "default", person.user,
                       person.allowlist ? "--allow" : "--deny"])
        } else if (id === "forget") {
            confirm.ask("forget", "Take " + person.user + " off the books?",
                        "The days already counted stay where they are, under "
                        + "/var/lib/omahouse, because a report outlives the rules it was "
                        + "collected under. The account itself is never touched.")
        } else if (id === "release") {
            picker.open()
        } else if (id === "minutes") {
            if (row === null)
                return
            win.target = row.id
            win.targetKind = row.kind
            prompt.ask("minutes", "Minutes a day for " + win.target,
                       "45m, 2h, 1h30m. It is the whole day's allowance for this one "
                       + "program; time an operator hands over today is on top of it.",
                       "", row.daily || "", false)
        } else if (id === "grant") {
            if (row === null)
                return
            win.target = row.kind === "budget" ? row.id : row.budgetId
            win.targetKind = row.kind
            prompt.ask("grant", "More time today for " + win.target,
                       "10m, 1h. It goes into today's ledger and expires with it, and it "
                       + "adds to the limit rather than replacing it.",
                       "+", "", false)
        } else if (id === "drop") {
            if (row === null)
                return
            win.target = row.id
            confirm.ask("drop", "Take " + row.id + " off the list?",
                        "Any limit written for it stays where it is: a program denied "
                        + "today may be allowed again tomorrow with the same number.")
        } else if (id === "day") {
            prompt.ask("day", "How long is " + person.user + "'s day?",
                       "2h, 90m. This is the budget whose selector is everything, and it "
                       + "ends the session when it runs out.",
                       "", person.hasSessionBudget ? (person.session.daily || "") : "", false)
        }
    }

    function answer(topic, text) {
        // First, and outside the guard below, for the reason `perform` gives:
        // the first account goes under rules on a machine with no profile to be
        // standing on.
        if (topic === "new") {
            Admin.run("put " + text + " under rules", ["profile", "add", text])
            return
        }
        const person = win.person
        if (person === null)
            return
        if (topic === "day") {
            Admin.run("the day's total", ["limit", person.user, "--session", text])
        } else if (topic === "minutes") {
            // The budget id is the CLI's handle, and a program that has none yet
            // gets its rule and its clock in one verb -- `allow --limit` is
            // sugar for exactly this, because they are one thought at the moment
            // somebody is configuring.
            const budget = win.targetKind === "budget" ? win.budgetNamed(win.target) : null
            const program = win.targetKind === "budget" ? null : win.programNamed(win.target)
            if (budget !== null)
                Admin.run("minutes a day", budget.session
                          ? ["limit", person.user, "--session", text]
                          : ["limit", person.user, "--budget", win.target + "=" + text])
            else if (program !== null && program.hasBudget)
                Admin.run("minutes a day",
                          ["limit", person.user, "--budget", program.budgetId + "=" + text])
            else
                Admin.run("minutes a day",
                          ["allow", person.user, win.target, "--limit", text])
        } else if (topic === "grant") {
            const budget = win.budgetNamed(win.target)
            const session = budget !== null && budget.session === true
            Admin.run("more time today", session
                      ? ["grant", person.user, "--session", text]
                      : ["grant", person.user, "--budget", win.target + "=" + text])
        } else if (topic === "release") {
            Admin.run("release " + win.target,
                      ["allow", person.user, win.target])
        } else if (topic === "releaseLimit") {
            Admin.run("release " + win.target, text === ""
                      ? ["allow", person.user, win.target]
                      : ["allow", person.user, win.target, "--limit", text])
        }
    }

    /// The budget or the program by that id, or null.
    ///
    /// Two functions and not one, and `targetKind` says which to ask. A budget
    /// and a program can carry the same id -- `allow --limit code` writes a
    /// budget called `code` about the program called `code`, which is the
    /// ordinary case -- and one lookup that searched both would answer with
    /// whichever list it happened to look in first.
    function budgetNamed(id) {
        for (let i = 0; i < win.allToday.length; i++) {
            if (win.allToday[i].kind === "budget" && win.allToday[i].id === id)
                return win.allToday[i]
        }
        return null
    }

    function programNamed(id) {
        for (let i = 0; i < win.allPrograms.length; i++) {
            if (win.allPrograms[i].id === id)
                return win.allPrograms[i]
        }
        return null
    }

    function settle(topic) {
        const person = win.person
        if (person === null)
            return
        if (topic === "forget")
            Admin.run("take " + person.user + " off the books",
                      ["profile", "remove", person.user])
        else if (topic === "drop")
            Admin.run("take " + win.target + " off the list",
                      ["deny", person.user, win.target])
    }

    // -------------------------------------------------------------- the keys
    function handleKey(event) {
        if (win.blocked)
            return

        const text = event.text

        // Escape comes first and from anywhere, including a chip reached by Tab.
        // It is the way back, and it has to work before any region has had a
        // chance to have an opinion.
        if (event.key === Qt.Key_Escape) {
            if (win.filter !== "")
                win.filter = ""
            else if (!root.activeFocus)
                win.takeFocus()
            else
                win.perform("back")
            event.accepted = true
            return
        }
        if ((event.modifiers & Qt.ControlModifier) || (event.modifiers & Qt.AltModifier))
            return

        if (text === "?") {
            helpSheet.open()
            event.accepted = true
            return
        }
        if (text === ":") {
            commandPalette.open()
            event.accepted = true
            return
        }
        if (event.key === Qt.Key_Slash) {
            win.startFilter()
            event.accepted = true
            return
        }
        if (text === "1" || text === "2" || text === "3") {
            win.go(parseInt(text))
            event.accepted = true
            return
        }

        if (text === "j" || event.key === Qt.Key_Down) {
            win.moveCursor(1)
            event.accepted = true
            return
        }
        if (text === "k" || event.key === Qt.Key_Up) {
            win.moveCursor(-1)
            event.accepted = true
            return
        }
        if (text === "g" || event.key === Qt.Key_Home) {
            win.setCursor(0)
            event.accepted = true
            return
        }
        if (text === "G" || event.key === Qt.Key_End) {
            win.setCursor(win.rows.length - 1)
            event.accepted = true
            return
        }
        if (event.key === Qt.Key_PageDown) {
            win.moveCursor(10)
            event.accepted = true
            return
        }
        if (event.key === Qt.Key_PageUp) {
            win.moveCursor(-10)
            event.accepted = true
            return
        }
        if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter
            || event.key === Qt.Key_Right) {
            win.perform("open")
            event.accepted = true
            return
        }
        if (event.key === Qt.Key_Left || event.key === Qt.Key_Backspace) {
            win.perform("back")
            event.accepted = true
            return
        }

        // And everything else is looked up in the one table, which is the same
        // table the chips are drawn from.
        const command = win.commandFor(text)
        if (command !== null) {
            if (command.usable)
                win.perform(command.id)
            else
                Admin.say(command.label + " — not here")
            event.accepted = true
        }
    }

    // A write lands, and the machine is read again at once rather than up to two
    // seconds later. Anything less and the operator's own action is the one
    // thing on this window that looks like it did not work.
    Connections {
        target: Admin
        function onDone(ok) {
            House.reload()
        }
    }

    Item {
        id: root
        anchors.fill: parent
        // The list has the keyboard unless a sheet is up, said once, as a fact
        // about the window rather than as a pair of calls at either end of
        // every sheet. A sheet whose field does not have the keyboard is a
        // sheet that reads the answer to its own question as commands for the
        // window behind it, and that failure is invisible except in what it
        // does.
        focus: !win.blocked

        Accessible.role: Accessible.Pane
        Accessible.name: win.title

        // In the Tab ring as well as the default holder of the focus, so Tab
        // walks out through the chips and comes back here rather than stranding
        // the keyboard on the last one.
        activeFocusOnTab: true

        Keys.onPressed: function (event) { win.handleKey(event) }
        Component.onCompleted: root.forceActiveFocus()

        Column {
            anchors.fill: parent
            spacing: 0

            // ------------------------------------------------------- header
            Rectangle {
                id: header
                width: parent.width
                height: 40
                color: Theme.panel

                Row {
                    anchors.left: parent.left
                    anchors.leftMargin: 12
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 10

                    Label {
                        anchors.verticalCenter: parent.verticalCenter
                        text: "omahouse"
                        font.bold: true
                    }
                    Label {
                        objectName: "faceLabel"
                        anchors.verticalCenter: parent.verticalCenter
                        quiet: true
                        // Said out loud, because it is the answer to "why can I
                        // not change anything here" and to "why did it ask for a
                        // password". Neither is a thing to leave somebody to
                        // work out from what does and does not happen.
                        text: House.user + " · " + House.face + " · " + House.faceReason
                              + (Admin.elevates ? " · writes through pkexec" : "")
                    }
                }

                Row {
                    anchors.right: parent.right
                    anchors.rightMargin: 12
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 6

                    Chip {
                        anchors.verticalCenter: parent.verticalCenter
                        objectName: "viewChip1"
                        label: "people"
                        key: "1"
                        quiet: true
                        on: win.view === 1
                        onClicked: win.go(1)
                    }
                    Chip {
                        anchors.verticalCenter: parent.verticalCenter
                        objectName: "viewChip2"
                        label: "programs"
                        key: "2"
                        quiet: true
                        on: win.view === 2
                        onClicked: win.go(2)
                    }
                    Chip {
                        anchors.verticalCenter: parent.verticalCenter
                        objectName: "viewChip3"
                        label: "today"
                        key: "3"
                        quiet: true
                        on: win.view === 3
                        onClicked: win.go(3)
                    }
                    Rule {
                        vertical: true
                        anchors.verticalCenter: parent.verticalCenter
                        height: 18
                    }
                    Chip {
                        anchors.verticalCenter: parent.verticalCenter
                        objectName: "filterChip"
                        label: "filter"
                        key: "/"
                        quiet: true
                        on: win.filter !== ""
                        onClicked: win.startFilter()
                    }
                    Chip {
                        anchors.verticalCenter: parent.verticalCenter
                        objectName: "paletteChip"
                        label: "commands"
                        key: ":"
                        quiet: true
                        onClicked: commandPalette.open()
                    }
                    Chip {
                        anchors.verticalCenter: parent.verticalCenter
                        objectName: "keysChip"
                        label: "keys"
                        key: "?"
                        quiet: true
                        onClicked: helpSheet.open()
                    }
                }
            }

            Rule {}

            // --------------------------------------------------- the chip bar
            //
            // The other door of the one table. Every chip here is a row of
            // `commands`, drawn with the key it answers to printed on its face.
            Rectangle {
                id: bar
                width: parent.width
                height: 38
                color: Theme.background

                Row {
                    id: chips
                    anchors.left: parent.left
                    anchors.leftMargin: 12
                    anchors.right: filterBox.left
                    anchors.rightMargin: 12
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 6
                    visible: !win.filtering
                    // Cut rather than drawn over the thing to its right. The
                    // window says who it is about on that side, and a chip
                    // printed across it would make two lines of nonsense out of
                    // two true ones. `:` lists whatever the bar could not fit.
                    clip: true

                    Repeater {
                        model: win.commands

                        Chip {
                            id: commandChip
                            required property var modelData
                            objectName: "command-" + commandChip.modelData.id
                            // `chips` and not `parent`: an item a Repeater is
                            // still building has no parent yet, and a binding
                            // through one is a warning on every rebuild of a
                            // table that is rebuilt whenever the cursor moves.
                            anchors.verticalCenter: chips.verticalCenter
                            // The short name here and the sentence in the
                            // palette and on the key sheet. A chip is read in a
                            // glance beside five others; a menu line is read on
                            // its own and can afford to say the whole thing.
                            label: commandChip.modelData.hint
                            key: commandChip.modelData.key
                            usable: commandChip.modelData.usable === true
                            onClicked: win.perform(commandChip.modelData.id)
                        }
                    }
                }

                Item {
                    id: filterBox
                    anchors.right: parent.right
                    anchors.rightMargin: 12
                    anchors.verticalCenter: parent.verticalCenter
                    width: win.filtering ? Math.min(360, bar.width - 40) : subjectLabel.implicitWidth
                    height: 26

                    Label {
                        id: subjectLabel
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.right: parent.right
                        visible: !win.filtering
                        quiet: true
                        text: win.filter !== "" ? "/" + win.filter
                            : win.subject === "" ? "nobody yet"
                                                 : "about " + win.subject
                    }

                    Entry {
                        id: filterField
                        objectName: "filterField"
                        anchors.fill: parent
                        visible: win.filtering
                        // Declared as well as forced, for the reason Prompt.qml
                        // gives: the field has the keyboard for exactly as long
                        // as the window is filtering, and not for as long as one
                        // imperative call happens to hold it.
                        focused: win.filtering
                        lead: "/"
                        placeholder: "filter"
                        onTextChanged: if (win.filtering) win.filter = filterField.text
                        onAccepted: win.finishFilter(false)
                        onCancelled: win.finishFilter(true)
                    }
                }
            }

            Rule {}

            // ------------------------------------------------------- the views
            Item {
                id: body
                width: parent.width
                height: parent.height - header.height - bar.height - status.height - 3

                // ------------------------------------------------ 1 · people
                Item {
                    anchors.fill: parent
                    visible: win.view === 1

                    Label {
                        anchors.centerIn: parent
                        visible: win.peopleRows.length === 0
                        quiet: true
                        width: parent.width - 80
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                        elide: Text.ElideNone
                        text: win.operating
                              ? (win.filter !== ""
                                 ? "no account here answers to that"
                                 : "nobody is under rules yet — press n, or click the chip")
                              : "nobody has put this account under rules"
                    }

                    ListView {
                        id: peopleList
                        objectName: "peopleList"
                        anchors.fill: parent
                        clip: true
                        model: win.peopleRows
                        currentIndex: win.peopleCursor
                        boundsBehavior: Flickable.StopAtBounds
                        Accessible.role: Accessible.List

                        delegate: Rectangle {
                            id: personRow
                            required property var modelData
                            required property int index
                            readonly property var who: personRow.modelData
                            width: peopleList.width
                            height: 56
                            color: peopleList.currentIndex === personRow.index
                                   ? Theme.fill(Theme.accent, 0.16) : "transparent"

                            Accessible.role: Accessible.ListItem
                            Accessible.name: personRow.who.name + ", " + personRow.who.policy
                                             + ", " + personRow.who.teeth

                            Rectangle {
                                width: 2
                                height: parent.height
                                color: peopleList.currentIndex === personRow.index
                                       ? Theme.accent : "transparent"
                            }

                            Column {
                                anchors.left: parent.left
                                anchors.leftMargin: 16
                                anchors.right: balance.left
                                anchors.rightMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 3

                                Row {
                                    spacing: 8
                                    Label {
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: personRow.who.name
                                        font.pixelSize: 13
                                    }
                                    Label {
                                        anchors.verticalCenter: parent.verticalCenter
                                        visible: personRow.who.name !== personRow.who.user
                                        quiet: true
                                        text: personRow.who.user
                                    }
                                    Label {
                                        anchors.verticalCenter: parent.verticalCenter
                                        visible: personRow.who.online
                                        quiet: true
                                        color: Theme.accent
                                        text: "logged in"
                                    }
                                    Label {
                                        anchors.verticalCenter: parent.verticalCenter
                                        visible: !personRow.who.exists
                                        quiet: true
                                        color: Theme.urgent
                                        text: "no such account on this machine"
                                    }
                                }
                                Label {
                                    quiet: true
                                    text: personRow.who.policy + " · " + personRow.who.teeth
                                          + " · " + personRow.who.programCount + " program"
                                          + (personRow.who.programCount === 1 ? "" : "s")
                                }
                            }

                            Column {
                                id: balance
                                anchors.right: parent.right
                                anchors.rightMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                width: 190
                                spacing: 4

                                Label {
                                    width: parent.width
                                    horizontalAlignment: Text.AlignRight
                                    color: personRow.who.hasSessionBudget
                                           && personRow.who.session.exhausted
                                           ? Theme.urgent : Theme.foreground
                                    text: !personRow.who.hasSessionBudget
                                          ? "no limit on the day"
                                          : !personRow.who.session.limited
                                            ? personRow.who.session.spent + " today"
                                            : personRow.who.session.left + " left of "
                                              + personRow.who.session.limit
                                }
                                Meter {
                                    width: parent.width
                                    limited: personRow.who.hasSessionBudget
                                             && personRow.who.session.limited
                                    spentSeconds: personRow.who.hasSessionBudget
                                                  ? personRow.who.session.spentSeconds : 0
                                    allowanceSeconds: personRow.who.hasSessionBudget
                                                      ? personRow.who.session.allowanceSeconds : 0
                                }
                            }

                            TapHandler {
                                onTapped: win.setCursor(personRow.index)
                                onDoubleTapped: {
                                    win.setCursor(personRow.index)
                                    win.perform("open")
                                }
                            }
                        }
                    }
                }

                // ---------------------------------------------- 2 · programs
                Item {
                    anchors.fill: parent
                    visible: win.view === 2

                    Label {
                        anchors.centerIn: parent
                        visible: win.programRows.length === 0
                        quiet: true
                        width: parent.width - 80
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                        elide: Text.ElideNone
                        text: win.subject === "" ? "nobody is under rules yet"
                            : win.filter !== "" ? "no program here answers to that"
                            : win.operating
                              ? "no program has been named yet — press a, or click the chip"
                              : "no program has been named yet"
                    }

                    ListView {
                        id: programList
                        objectName: "programList"
                        anchors.fill: parent
                        clip: true
                        model: win.programRows
                        currentIndex: win.programCursor
                        boundsBehavior: Flickable.StopAtBounds
                        Accessible.role: Accessible.List

                        delegate: Rectangle {
                            id: programRow
                            required property var modelData
                            required property int index
                            readonly property var app: programRow.modelData
                            width: programList.width
                            height: 56
                            color: programList.currentIndex === programRow.index
                                   ? Theme.fill(Theme.accent, 0.16) : "transparent"

                            Accessible.role: Accessible.ListItem
                            Accessible.name: programRow.app.id
                                             + (programRow.app.released ? ", released"
                                                                        : ", not released")

                            Rectangle {
                                width: 2
                                height: parent.height
                                color: programList.currentIndex === programRow.index
                                       ? Theme.accent : "transparent"
                            }

                            Column {
                                anchors.left: parent.left
                                anchors.leftMargin: 16
                                anchors.right: clock.left
                                anchors.rightMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 3

                                Row {
                                    spacing: 8
                                    Label {
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: programRow.app.name === ""
                                              ? programRow.app.id : programRow.app.name
                                        font.pixelSize: 13
                                    }
                                    Label {
                                        anchors.verticalCenter: parent.verticalCenter
                                        visible: programRow.app.name !== ""
                                                 && programRow.app.name !== programRow.app.id
                                        quiet: true
                                        text: programRow.app.id
                                    }
                                    Label {
                                        anchors.verticalCenter: parent.verticalCenter
                                        visible: !programRow.app.released
                                        quiet: true
                                        color: Theme.urgent
                                        text: "not released"
                                    }
                                    Label {
                                        anchors.verticalCenter: parent.verticalCenter
                                        visible: programRow.app.running
                                        quiet: true
                                        color: Theme.accent
                                        text: "open now · " + programRow.app.pids
                                              + " process"
                                              + (programRow.app.pids === 1 ? "" : "es")
                                    }
                                }
                                // The shim, said out loud. spec.md §5: an app
                                // launched through one takes its name, so a rule
                                // about `gtk-launch` is a rule about whatever it
                                // launches next -- and nobody can weigh that
                                // without being shown what is in there.
                                Label {
                                    objectName: "programExe"
                                    visible: programRow.app.disagrees
                                    quiet: true
                                    color: Theme.urgent
                                    text: "that name is not what is running: "
                                          + programRow.app.exe + " — "
                                          + programRow.app.exeCount + " of "
                                          + programRow.app.pids
                                }
                                Label {
                                    visible: !programRow.app.disagrees
                                    quiet: true
                                    text: programRow.app.limited
                                          ? programRow.app.ending + " when the time is up"
                                          : "no limit of its own — it spends the day's total"
                                }
                            }

                            Column {
                                id: clock
                                anchors.right: parent.right
                                anchors.rightMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                width: 190
                                spacing: 4

                                Label {
                                    width: parent.width
                                    horizontalAlignment: Text.AlignRight
                                    color: programRow.app.exhausted ? Theme.urgent
                                                                    : Theme.foreground
                                    // Nothing at all for a program with no
                                    // clock of its own. `0m today` there would
                                    // be a number about a counter that does not
                                    // exist -- what it spends is the day's, and
                                    // the line on the left says so.
                                    text: programRow.app.limited
                                          ? programRow.app.left + " left of "
                                            + programRow.app.limit
                                          : programRow.app.hasBudget
                                            ? programRow.app.spent + " today" : ""
                                }
                                Meter {
                                    width: parent.width
                                    limited: programRow.app.limited
                                    spentSeconds: programRow.app.spentSeconds
                                    allowanceSeconds: programRow.app.allowanceSeconds
                                }
                                Label {
                                    width: parent.width
                                    horizontalAlignment: Text.AlignRight
                                    visible: programRow.app.granted !== ""
                                    quiet: true
                                    color: Theme.accent
                                    text: "+" + programRow.app.granted + " handed over today"
                                }
                            }

                            TapHandler {
                                onTapped: win.setCursor(programRow.index)
                            }
                        }
                    }
                }

                // ------------------------------------------------- 3 · today
                Item {
                    anchors.fill: parent
                    visible: win.view === 3

                    Label {
                        anchors.centerIn: parent
                        visible: win.todayRows.length === 0
                        quiet: true
                        width: parent.width - 80
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                        elide: Text.ElideNone
                        text: win.subject === "" ? "nobody is under rules yet"
                            : win.filter !== "" ? "nothing here answers to that"
                            : win.operating
                              ? "no clock has been set yet — press s for the day's total"
                              : "nothing has been counted today"
                    }

                    ListView {
                        id: todayList
                        objectName: "todayList"
                        anchors.fill: parent
                        clip: true
                        model: win.todayRows
                        currentIndex: win.todayCursor
                        boundsBehavior: Flickable.StopAtBounds
                        Accessible.role: Accessible.List

                        delegate: Rectangle {
                            id: todayRow
                            required property var modelData
                            required property int index
                            readonly property var line: todayRow.modelData
                            readonly property bool isBudget: todayRow.line.kind === "budget"
                            width: todayList.width
                            height: todayRow.isBudget ? 56 : 26
                            color: todayList.currentIndex === todayRow.index
                                   ? Theme.fill(Theme.accent, 0.16) : "transparent"

                            Accessible.role: Accessible.ListItem
                            Accessible.name: todayRow.isBudget
                                             ? todayRow.line.name + ", " + todayRow.line.spent
                                               + " spent"
                                             : todayRow.line.at + " " + todayRow.line.text

                            Rectangle {
                                width: 2
                                height: parent.height
                                color: todayList.currentIndex === todayRow.index
                                       ? Theme.accent : "transparent"
                            }

                            // A budget.
                            Column {
                                visible: todayRow.isBudget
                                anchors.left: parent.left
                                anchors.leftMargin: 16
                                anchors.right: budgetClock.left
                                anchors.rightMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 3

                                Row {
                                    spacing: 8
                                    Label {
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: todayRow.line.name || ""
                                        font.pixelSize: 13
                                    }
                                    Label {
                                        anchors.verticalCenter: parent.verticalCenter
                                        quiet: true
                                        text: todayRow.line.id
                                    }
                                    Label {
                                        anchors.verticalCenter: parent.verticalCenter
                                        visible: todayRow.line.running === true
                                        quiet: true
                                        color: Theme.accent
                                        text: "running now"
                                    }
                                }
                                Label {
                                    quiet: true
                                    text: todayRow.line.limited
                                          ? todayRow.line.spent + " spent · "
                                            + todayRow.line.ending + " when it runs out"
                                          : todayRow.line.spent + " spent · no limit"
                                }
                            }

                            Column {
                                id: budgetClock
                                visible: todayRow.isBudget
                                anchors.right: parent.right
                                anchors.rightMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                width: 190
                                spacing: 4

                                Label {
                                    width: parent.width
                                    horizontalAlignment: Text.AlignRight
                                    color: todayRow.line.exhausted === true ? Theme.urgent
                                                                            : Theme.foreground
                                    font.pixelSize: todayRow.line.session === true ? 14 : 12
                                    text: todayRow.line.limited
                                          ? todayRow.line.left + " left of " + todayRow.line.limit
                                          : "no limit"
                                }
                                Meter {
                                    width: parent.width
                                    limited: todayRow.line.limited === true
                                    spentSeconds: todayRow.line.spentSeconds || 0
                                    allowanceSeconds: todayRow.line.allowanceSeconds || 0
                                }
                                Label {
                                    width: parent.width
                                    horizontalAlignment: Text.AlignRight
                                    visible: (todayRow.line.granted || "") !== ""
                                    quiet: true
                                    color: Theme.accent
                                    text: "+" + (todayRow.line.granted || "")
                                          + " handed over today"
                                }
                            }

                            // A line of the day's log.
                            Row {
                                visible: !todayRow.isBudget
                                anchors.left: parent.left
                                anchors.leftMargin: 16
                                anchors.right: parent.right
                                anchors.rightMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 10

                                Label {
                                    anchors.verticalCenter: parent.verticalCenter
                                    quiet: true
                                    text: todayRow.line.at || ""
                                }
                                Label {
                                    anchors.verticalCenter: parent.verticalCenter
                                    quiet: true
                                    color: todayRow.line.event === "denied" ? Theme.urgent
                                         : todayRow.line.event === "grant" ? Theme.accent
                                                                           : Theme.dim
                                    text: todayRow.line.text || ""
                                }
                            }

                            TapHandler {
                                onTapped: win.setCursor(todayRow.index)
                            }
                        }
                    }
                }
            }

            Rule {}

            StatusBar {
                id: status
                width: parent.width
                cursor: win.rows.length === 0 ? -1 : win.cursor
                total: win.rows.length
                note: win.person === null ? House.faceReason
                    : win.view === 1 ? win.people.length + " under rules"
                                     : win.person.name + " · " + win.person.policy
                blind: win.person === null ? 0 : win.person.blindProcesses
                message: House.error !== "" ? House.error : Admin.message
                alarm: House.error !== "" || Admin.failed
                hints: win.hints
            }
        }
    }

    // ----------------------------------------------------------- the sheets
    HelpSheet {
        id: helpSheet
        anchors.fill: parent
        groups: win.keymap
        returnFocus: win.takeFocus
    }

    CommandPalette {
        id: commandPalette
        anchors.fill: parent
        commands: win.commands
        returnFocus: win.takeFocus
        onChosen: function (id) {
            commandPalette.close()
            win.perform(id)
        }
    }

    Prompt {
        id: prompt
        anchors.fill: parent
        returnFocus: win.takeFocus
        onAnswered: function (topic, text) { win.answer(topic, text) }
    }

    Confirm {
        id: confirm
        anchors.fill: parent
        returnFocus: win.takeFocus
        onConfirmed: function (topic) { win.settle(topic) }
    }

    Picker {
        id: picker
        anchors.fill: parent
        programs: win.catalogue
        returnFocus: win.takeFocus
        onPicked: function (id) {
            win.target = id
            prompt.ask("releaseLimit", "How long a day for " + id + "?",
                       "45m, 2h, 1h30m — or leave it empty, and it runs with no clock of "
                       + "its own, spending the day's total like everything else.",
                       "", "", true)
        }
    }
}
