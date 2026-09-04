#pragma once

#include "Enforce.h"
#include "Notify.h"
#include "Policy.h"
#include "Proc.h"

#include <QDateTime>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

namespace omahouse {

// The loop of docs/design.md §5: watch, count, say, and -- since stage 7 -- act.
//
// The steps are the ones the spec numbers. Find the users with a profile who
// have a session; list their app scopes; hand the scopes, the profile, the day's
// ledger and the time to `evaluate`; write the ledger back; and carry out the
// decisions. The clock enters here, in the caller, and nowhere else -- that is
// the whole reason `evaluate` takes a `now` -- and so does every file and every
// process.
//
// All three decisions are carried out now. `Warn` is the notification of §6;
// `Close` is SIGTERM into the scope and then its `cgroup.kill`; `Logout` is the
// name in `/etc/omahouse/blocked` and then `loginctl terminate-user`, which are
// one action and never two -- §2, and poc/findings.md round 2, which measured a
// termination on its own being undone by the tty1 autologin in the same breath.
//
// What keeps this safe to have in a build that also runs on a development
// machine is `Enforce.h`: every act is asked for permission first, and the
// permission is about which tree the paths are in rather than about a flag.

/// The two lines of one notification.
struct Words {
    QString summary;
    QString body;
};

/// What to say about one decision, in words.
///
/// The core hands back a `Reason` and a number of seconds and writes no
/// sentence, which is deliberate: `Denied` and `Warning` are the same
/// instruction with two different things to tell somebody, and the language a
/// household reads is not a thing to bury in a pure function. This is where the
/// sentence is written, and the profile is here too -- what a budget does when
/// it runs out, and whether the teeth are in at all, change what is true enough
/// to say.
///
/// `app` is the id of the scope a `Denied` is about, which the caller resolves,
/// because the decision names the unit and a person reads the id.
Words wordsFor(const Profile &profile, const Decision &decision, const QString &app,
               const QDateTime &now);

/// The name of a decision kind, for a log line and for `--json`.
QString decisionKindName(Decision::Kind kind);
QString decisionReasonName(Decision::Reason reason);

/// One thing the teeth did, or one thing they were refused.
struct Done {
    enum class What {
        /// SIGTERM into every process of one scope. The polite half of closing:
        /// an editor writes its buffers and a browser does not come back with a
        /// crash bar.
        Terminate,
        /// `echo 1 > <scope>/cgroup.kill`. The whole scope at once.
        Kill,
        /// The name written into `/etc/omahouse/blocked`.
        Block,
        /// And taken out of it again -- at the turn of the day, on a grant, when
        /// enforcement goes off, when the profile is removed.
        Unblock,
        /// `loginctl terminate-user`.
        EndSession,
    };

    What what = What::Terminate;
    /// The decision this came of. Absent for `Unblock`, which is nobody's
    /// decision: it is the block no longer standing.
    Decision decision;
    /// The id of the scope, where there is one, and its unit either way.
    QString app;
    QString unit;
    bool carriedOut = false;
    /// Why it was not done, when it was not. A refusal from `Enforce.h` and a
    /// failure of the act itself both land here, and the sentence says which.
    QString error;
};

QString doneWhatName(Done::What what);

/// One thing said, or one thing that would have been said.
struct Said {
    Decision decision;
    /// The id of the scope, where the decision is about one.
    QString app;
    Words words;
    /// Whether it really went out. False in a dry run, and false when the
    /// notification failed -- `error` tells them apart.
    bool sent = false;
    QString error;
};

/// What one cycle did about one user.
struct Watched {
    QString user;
    QString displayName;
    uid_t uid = 0;
    /// Whether the machine has such an account. A profile can outlive the
    /// account it was written for, and there is nothing to count for one that is
    /// not there.
    bool account = false;
    bool enabled = false;
    bool session = false;
    /// The ids of the live scopes, sorted and without repeats. Only the ones
    /// that have an id: a scope nothing could name has no word to put in a
    /// sentence, and an empty string in this list would print as a gap between
    /// two commas.
    QStringList apps;
    /// How many live scopes had no id at all. Counted rather than dropped,
    /// because they are counted: they spend the session budget like everything
    /// else, and a cycle that debited an hour while reporting `no apps` is the
    /// journal telling the operator the opposite of what the ledger says.
    int unnamedScopes = 0;
    /// The budgets that gained seconds this cycle, taken from the ledger before
    /// and after rather than worked out a second time.
    QStringList debited;
    /// The day, as it stands after the cycle.
    Ledger ledger;
    /// The `Warn` decisions, with their words, and whether they went out.
    QVector<Said> said;
    /// What the `Close` and `Logout` decisions came to.
    QVector<Done> done;
    /// The `Logout` decisions that stand right now, worked out from today's
    /// ledger every cycle and never remembered.
    ///
    /// That they are re-derived rather than remembered is what makes the name
    /// come out of `/etc/omahouse/blocked` on its own. The turn of the day
    /// resets the balance, so no logout stands, so the name is not written --
    /// and the same goes for an operator's grant, for `enforce --off`, and for
    /// the profile being removed altogether. None of those verbs has to know
    /// that the file exists.
    ///
    /// Asked even of a user with no session, because `terminate-user` is exactly
    /// what left them without one: a loop that stopped asking here would take
    /// the name back out on the very next cycle and let them straight back in.
    QVector<Decision> logouts;
    /// Whether the name really is in the file, after the cycle reconciled it.
    /// The session is only ended once this is true -- the lock goes on the door
    /// before anybody is put outside it.
    bool blocked = false;
    bool wrote = false;
    /// A ledger that would not be read or would not be written. Not fatal to the
    /// loop: one user's unreadable file is not a reason to stop counting the
    /// others.
    QString error;
    /// Whether this cycle is different enough from the last one to be worth a
    /// line. A two second loop that logs every tick fills a journal with the
    /// same sentence twelve hundred times an hour.
    bool worthSaying = false;
};

struct Cycle {
    QDateTime at;
    int tickSeconds = 0;
    bool dryRun = false;
    QVector<Watched> users;
    /// The names in `/etc/omahouse/blocked` after this cycle, and a sentence
    /// when the file could not be read or written. An unreadable `blocked` is
    /// not fatal to anything -- `onerr=succeed` means it is refusing nobody --
    /// but it is the one thing about it worth saying out loud.
    QStringList blocked;
    QString blockedError;
};

class Watch {
public:
    struct Options {
        /// Seconds between cycles, and the seconds each cycle debits. One
        /// number, because a loop that ticks every two seconds and debits three
        /// is a day that ends early.
        int tickSeconds = 2;
        /// Decide, and touch nothing: no ledger written, no notification sent.
        /// What makes the loop safe to run on a machine nobody meant to
        /// fiscalise.
        bool dryRun = false;
    };

    /// None of the pointers is owned. `enforcer` may be null, and then nothing
    /// is ever closed and nobody is ever logged out -- which is what a caller
    /// that only wants the accounting hands in.
    Watch(const Proc *proc, Notifier *notifier, Enforcer *enforcer, const Options &options);

    const Options &options() const { return m_options; }

    /// One turn of docs/design.md §5, for every profile handed in.
    ///
    /// The profiles come in by parameter and are not read here, so that the
    /// caller can read them again every cycle: `docs/design.md` §1 asks that an
    /// operator be able to hand over ten minutes with the game still running,
    /// and a daemon holding a copy of the rules from when it started cannot
    /// honour that.
    Cycle tick(const QVector<Profile> &profiles, const QDateTime &now);

private:
    void observe(const Profile &profile, Watched *watched, const QDateTime &now);
    /// The sequence of docs/design.md §5 for one scope, spread across ticks: SIGTERM
    /// the first time, `cgroup.kill` once the window has gone by.
    void closeScope(const Profile &profile, Watched *watched, const Decision &decision,
                    const AppScope &scope, const QDateTime &now);
    void reconcileBlocked(Cycle *cycle);
    void endSessions(Cycle *cycle);
    bool worthSaying(const Watched &watched);

    const Proc *m_proc;
    Notifier *m_notifier;
    Enforcer *m_enforcer;
    Options m_options;
    /// What each user's cycle looked like last time, so that a cycle that says
    /// the same thing says nothing at all.
    QHash<QString, QString> m_shape;
    /// When each scope was sent its SIGTERM, keyed by user and unit.
    ///
    /// docs/design.md §5 spells closing as a sequence -- SIGTERM, wait, `cgroup.kill`
    /// -- and a two second loop cannot wait inside a tick: twenty seconds of
    /// sleeping is twenty seconds of everybody else's day not being counted. So
    /// the wait is spread across ticks, and this is the only thing the loop
    /// remembers between them.
    ///
    /// In memory and not on disk, deliberately. A daemon restarted mid-wait
    /// sends a second SIGTERM and starts the wait again, which is a scope
    /// getting more time to exit and never less -- the failure that leaves the
    /// rules soft, again.
    QHash<QString, QDateTime> m_termed;
};

} // namespace omahouse
