#pragma once

#include <QString>
#include <QStringList>

#include <sys/types.h>

namespace omahouse {

// How a daemon that is root speaks to a session it is not in -- spec.md §6.
//
// There is no helper process inside the fiscalised session, and there must not
// be: whatever runs as the fiscalised user is something that user can kill, and
// a warning nobody can suppress is the whole point. So the daemon reaches into
// the session from outside, with the command poc/findings.md round 2 measured
// end to end against a real notification daemon:
//
//     systemd-run --uid=1001
//       --setenv=DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/1001/bus
//       notify-send "Faltam 5 minutos" "Minecraft fecha às 19:35"
//
// -- one command, wrapped here for the margin and never for the shell --
//
// `makoctl list` on the other side showed the notification. That is the shape,
// and it is kept exactly as it was measured.
//
// The core never comes near any of this: it hands back a Decision with a Reason
// and a number of seconds, and the words are written by the caller.

/// The program that puts a notification on a screen: `notify-send`, or
/// `$OMAHOUSE_NOTIFY_SEND`.
///
/// The variable is the same door `$OMAHOUSE_USERADD` is, and it is there for the
/// same reason: the end to end suite has to prove that the right command is
/// built without a notification appearing on the screen of whoever is running
/// the suite.
QString notifySendProgram();

/// The program that reaches into another user's session: `systemd-run`, or
/// `$OMAHOUSE_SYSTEMD_RUN`.
QString systemdRunProgram();

/// The command that says one thing to `target`'s session, program first.
///
/// Two shapes, and which one is right depends on who is speaking. A daemon
/// running as root goes through `systemd-run`, because it has no session bus of
/// its own and has to be told where the target's is. A `watch` running as the
/// very user it is watching already has one in its environment, so `notify-send`
/// by itself is the whole command -- and that is what makes stage 6 something
/// that can be tried on a development machine without root.
QStringList notifyCommand(uid_t target, uid_t self, const QString &summary, const QString &body);

/// Something that can say one thing to one session.
///
/// An interface and not a function, so that a test can watch what would have
/// been said without a notification daemon, a session bus or a screen. The
/// daemon of stage 7 grows two more of these -- closing a scope and ending a
/// session -- and they will want the same seam for the same reason.
class Notifier {
public:
    virtual ~Notifier();

    /// False with `error` set. A notification that could not be delivered is
    /// worth a line in the journal and is never worth stopping the loop: the
    /// counting is the part that must not miss a tick.
    virtual bool notify(uid_t uid, const QString &summary, const QString &body,
                        QString *error) = 0;
};

/// The one that really speaks.
class DesktopNotifier : public Notifier {
public:
    /// `self` is whoever is running this, and it decides which of the two shapes
    /// of `notifyCommand` is used. Defaults to the real uid rather than the
    /// effective one: the question is whose session bus is in this environment,
    /// and a `setuid` would not have moved it.
    explicit DesktopNotifier(uid_t self);
    DesktopNotifier();

    bool notify(uid_t uid, const QString &summary, const QString &body, QString *error) override;

private:
    uid_t m_self;
};

} // namespace omahouse
