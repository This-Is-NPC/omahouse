#pragma once

#include <QString>
#include <QStringList>

#include <sys/types.h>

namespace omahouse {

// Whether anybody is in front of the machine, asked of the machine and never of
// a browser.
//
// The whole reason this file exists is one measurement. `.temp/spike-extension.md`
// §5 put a Chromium extension on real Omarchy and asked `chrome.idle` every
// twenty seconds for thirty minutes with nobody at the keyboard: it answered
// `active` every single time, including the last twenty-five minutes with the
// monitor physically off. It does not merely fail to notice idleness -- it
// reports presence that is not there. Any time-per-site meter that trusts the
// browser runs all night beside a sleeping child.
//
// So presence is omahouse's own question, and it is asked of the compositor's
// side of the world. `.temp/poc-presence.md` is the measurement that chose how.
//
// -- what is read, and what is deliberately not -------------------------------
//
// Two facts, and neither of them comes from the fiscalised user's compositor:
//
//   the screen   `/sys/class/drm/<connector>/dpms`, which the kernel writes and
//                only root's own drivers can. Hyprland turning a monitor off is
//                an atomic modeset, and `drm_atomic_helper_update_legacy_modeset_state`
//                carries it into that attribute: measured flipping to `Off` in
//                the same second the display went dark, both on the idle path
//                and on an explicit dpms dispatch.
//   the seat     `loginctl show-seat <seat> -p ActiveSession`, then that
//                session's `User`. logind is root's own service and the daemon
//                already shells out to `loginctl` to end a session, so this is
//                not a new door -- it is the one already in the wall.
//
// What is **not** read is the user's compositor. Root could connect to
// `$XDG_RUNTIME_DIR/hypr/$HIS/.socket.sock` and ask `hyprctl monitors`, and the
// measurement shows it works and costs 0.17 ms. It is refused anyway, and the
// reason is not performance: that socket lives in a directory the fiscalised
// user owns. A child who kills her own Hyprland and puts her own program on that
// path is a child feeding bytes to a JSON parser running as root. The two things
// read here are a kernel attribute and root's own logind, and neither can be
// forged by the person being measured. That is worth more than a reason field.
//
// The cost of refusing it is named rather than hidden, and it is the `Locked`
// reason below.

/// Present or absent, and why.
struct Presence {
    enum class Reason {
        /// The seat is showing this user's session and the screen is lit.
        Using,
        /// Every connected screen is off. On Omarchy this is where an absence
        /// lands: the shell's idle service locks at five minutes and the display
        /// goes dark a second later, and an explicit `dpms` dispatch gets here
        /// straight away.
        ScreenOff,
        /// The compositor holds a session lock while the screen is still lit.
        ///
        /// Measured, and measured to be invisible from here: Omarchy 4.0.2 has
        /// no `hyprlock` process to find -- the lock is an `ext-session-lock`
        /// held by `quickshell`, which is running whether or not anything is
        /// locked -- and logind's `LockedHint` stayed `no` right through it. The
        /// only thing that knows is the compositor, and reading the compositor
        /// is the door this file will not open.
        ///
        /// So the vocabulary carries it and `SeatPresenceSource` never returns
        /// it. What that costs is measured too: eight seconds, from the lock
        /// landing to the display going off, read as `Using`. A caller handed a
        /// reading from somewhere else -- a compositor that locks without going
        /// dark, a future source that has the fact for free -- gets the word for
        /// it without this file having to change.
        Locked,
        /// Logged in, but the seat is showing somebody else's session. Measured
        /// with a `chvt`: the screen stays `On` and only logind knows the
        /// difference, which is the whole reason the seat is read at all.
        OtherSession,
        /// No graphical session: no `app.slice` under `user@<uid>.service`.
        NoSession,
        /// Nothing could be read -- no seat, or no connected screen. Never
        /// `present`, and never `absent` either: it is the honest third answer,
        /// and a caller that needs a decision out of it has to choose one.
        Unknown,
    };

    bool present = false;
    Reason reason = Reason::Unknown;

    /// Whether anything was learned at all. `Unknown` is not absence.
    bool known() const { return reason != Reason::Unknown; }
};

/// The name of a reason, for the ledger and for `--json`. Stable, lower case
/// and hyphenated, because it is a key in a file people read.
QString presenceReasonName(Presence::Reason reason);
bool presenceReasonFromName(const QString &name, Presence::Reason *out);

/// The same thing in words, for `status`. The reason is a word and the sentence
/// is a sentence, and the two are kept apart here for the same reason `Policy`
/// keeps `Reason` and the notification apart: one of them is a key in a file.
QString presenceSentence(const Presence &presence);

// -- the facts ----------------------------------------------------------------

/// What the screens are doing, as far as the kernel will say.
enum class ScreenState {
    On,
    Off,
    /// No connected connector to ask. A machine with no screen on it, or a root
    /// pointed at a tree that has none.
    Unknown,
};

QString screenStateName(ScreenState state);

/// One look at the seat and the screen, taken once per cycle and shared by every
/// user in it.
///
/// Once per cycle and not once per user, because both facts are about the
/// machine: there is one seat and one set of screens, and asking `loginctl`
/// twice per user per tick would be paying per profile for an answer that does
/// not vary by profile.
struct SeatReading {
    /// Whether the seat could be read at all. False is a machine with no seat,
    /// or a `loginctl` that would not run, and it is what makes every verdict
    /// `Unknown` rather than a confident absence.
    bool read = false;
    /// Whether the seat has a session on it right now. False at the greeter
    /// between two logins.
    bool occupied = false;
    /// The uid of the session the seat is showing. Only meaningful with
    /// `occupied`.
    uid_t uid = 0;
    ScreenState screen = ScreenState::Unknown;
};

/// The verdict about one user, over one reading and nothing else.
///
/// Pure, and that is the point: no `/sys`, no fork, no clock, no session. Every
/// interesting combination -- the screen off with the session in front, the
/// screen on with somebody else's session in front, a seat that could not be
/// read at all -- is a line in a test that runs in microseconds on a machine
/// with no graphical session on it.
///
/// The order of the questions is the order of what is worth saying. No session
/// first, because there is nothing else to say about somebody who is not logged
/// in. Then the seat, because a screen that is lit for somebody else is not this
/// person's screen. Then the screen.
Presence presenceOf(uid_t uid, bool hasSession, const SeatReading &reading);

// -- reading the machine ------------------------------------------------------

/// `/sys/class/drm`, or `$OMAHOUSE_DRM_ROOT`.
///
/// The same door `$OMAHOUSE_CGROUP_ROOT` and `$OMAHOUSE_PROC_ROOT` are, and it
/// exists for the same reason: a suite that needs a real monitor to assert
/// anything about a monitor is a suite nobody runs. The end to end suite writes
/// a `card0-Virtual-1/` of its own in $TMPDIR with `status` and `dpms` in it.
QString drmRoot();
/// `/sys/class/drm` itself, whatever this run was pointed at.
QString systemDrmRoot();
/// Whether that root is still the machine's own, compared by value, exactly as
/// `Proc::cgroupRootIsTheSystems` does it.
bool drmRootIsTheSystems();

/// The connectors under `root` with something plugged into them, by name.
///
/// `status` is the filter and `enabled` deliberately is not. Measured: Hyprland
/// turning a monitor off leaves `status=connected` and moves **both**
/// `enabled` to `disabled` and `dpms` to `Off`, so a walk that only looked at
/// enabled connectors would find none and answer `Unknown` for the very state it
/// exists to catch. A laptop's eight unplugged DisplayPorts read
/// `status=disconnected dpms=Off` and would otherwise drown out the one screen
/// that is lit; the writeback connector reads `status=unknown` and is not a
/// screen at all. Both were measured on the development machine.
QStringList connectedScreens(const QString &root);

/// What those connectors add up to: `On` if any of them is on, `Off` if every
/// one of them is off, `Unknown` if there are none.
///
/// Any and not all, because two monitors with one of them asleep is somebody
/// looking at the other one.
ScreenState screenStateFromDrm(const QString &root);

/// `loginctl show-seat <seat> -p ActiveSession`, program first.
QStringList seatActiveSessionCommand(const QString &seat);
/// `loginctl show-session <id> -p User`, program first.
QStringList sessionUserCommand(const QString &session);

/// Parses one `Key=Value` block of `loginctl show-*` output. Empty for a key
/// that is not there, which is what an unoccupied seat's `ActiveSession` is.
QString showProperty(const QString &output, const QString &key);

/// Something that can say what the seat and the screens are doing.
///
/// An interface for the same reason `Notifier` and `Enforcer` are: the loop has
/// to be drivable through every state without a seat, a screen or a `loginctl`
/// on the machine running the suite. It is the injection point the task asked
/// for, and `Watch` holds a pointer to it exactly as it holds one to `Proc`.
class PresenceSource {
public:
    virtual ~PresenceSource();

    /// One look, for the whole cycle.
    virtual SeatReading readSeat() = 0;
};

/// The one that really looks.
class SeatPresenceSource : public PresenceSource {
public:
    /// `seat0` by default, because that is the seat a household machine has.
    /// A machine with more than one is a machine this does not claim to
    /// understand, and it says `Unknown` rather than guessing.
    explicit SeatPresenceSource(const QString &drmRoot = omahouse::drmRoot(),
                                const QString &seat = QStringLiteral("seat0"));

    QString drmRoot() const { return m_drmRoot; }
    QString seat() const { return m_seat; }

    SeatReading readSeat() override;

private:
    QString m_drmRoot;
    QString m_seat;
};

} // namespace omahouse
