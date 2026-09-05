#pragma once

#include "Enforce.h"
#include "FocusFile.h"
#include "Notify.h"
#include "Policy.h"
#include "Presence.h"
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
        /// The domain written into the browser's `URLBlocklist`, because a site
        /// budget ran out. Its own word and not `Block`: one of them is a name
        /// in a file PAM reads and the other is a domain in a file Chromium
        /// reads, and a journal that called them the same thing would be a
        /// journal that could not tell an evening ending from a site closing.
        BlockSite,
        /// And out of it again, by the same path and for the same four reasons.
        UnblockSite,
    };

    What what = What::Terminate;
    /// The decision this came of. Absent for `Unblock` and `UnblockSite`, which
    /// are nobody's decision: they are the block no longer standing.
    Decision decision;
    /// The id of the scope, where there is one, and its unit either way.
    QString app;
    QString unit;
    /// The domain, for `BlockSite` and `UnblockSite`. Empty otherwise.
    QString site;
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
    /// Whether anybody is in front of the machine, and why -- `Presence.h`.
    ///
    /// Measured and reported, and it decides nothing. It does not change what an
    /// app is billed: docs/design.md §5 bills running time, that is published
    /// behaviour, and a day counted differently because a screen went dark is
    /// not this step's to hand out. The seconds land in the day's ledger beside
    /// the budgets, and `status` says the state out loud, and that is all -- what
    /// will use it is the time per site.
    Presence presence;
    /// The site the browser last said was in the front tab, or empty.
    ///
    /// Read out of the file the native messaging host writes -- docs/design.md
    /// §5.2 -- and carried here whether or not it was billed, because the line
    /// that says `youtube.com, not counted: screen-off` is the whole point of
    /// crossing the two. A browser reporting a site into an empty room is
    /// exactly what `.temp/spike-extension.md` §5 measured, and the journal is
    /// where somebody can see that omahouse knows the difference.
    QString site;
    /// Whether that site really gained the tick.
    ///
    /// False for a site reported with nobody in front of the screen, which is
    /// the crossing that keeps the number honest, and false for a cycle with no
    /// site at all.
    bool siteCounted = false;
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
    /// The `Block` decisions that stand right now: the sites whose budget is out
    /// of time today.
    ///
    /// Re-derived every cycle from today's ledger, exactly as `logouts` is, and
    /// for exactly the same payoff: nothing has to remember to let a site back
    /// through. The turn of the day resets the balance, so no site is out of
    /// time, so no domain is written -- and the same goes for a grant, for
    /// `enforce --off`, and for the profile being removed. None of those verbs
    /// has to know the browser's policy file exists.
    ///
    /// Asked even of a user with no session, for a weaker version of the reason
    /// `logouts` is: the block belongs to the day and not to whether anybody is
    /// at the keyboard, and a browser opened by somebody else on this machine is
    /// reading the same one file -- docs/design.md §11.
    QVector<Decision> blocks;
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
    /// The domains in the browser's `URLBlocklist` because a budget ran out,
    /// after this cycle reconciled the policy file, and a sentence when the file
    /// could not be written or the run was not allowed to touch it.
    ///
    /// The sites only. What the profiles' own web rules put in that file is
    /// `chromiumPolicyFor`'s business and does not change from one cycle to the
    /// next; what is worth reporting here is the part that is about today.
    QStringList blockedSites;
    QString blockedSitesError;
    /// The one look at the seat and the screens this cycle took, shared by every
    /// user in it. One machine, one seat, one set of monitors: asking per profile
    /// would be paying per profile for an answer that does not vary by profile.
    SeatReading seat;
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
    /// that only wants the accounting hands in. `presence` may be null too, and
    /// then every user's presence is `Unknown` and nothing about it is written:
    /// a loop that was never given eyes must say it cannot see, not that nobody
    /// is there. `focus` may be null, and then no day gains a site -- which is
    /// every machine with no browser extension on it, and is not a failure.
    Watch(const Proc *proc, Notifier *notifier, Enforcer *enforcer, PresenceSource *presence,
          FocusSource *focus, const Options &options);

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
    void observe(const Profile &profile, Watched *watched, const SeatReading &seat,
                 const QDateTime &now);
    /// The sequence of docs/design.md §5 for one scope, spread across ticks: SIGTERM
    /// the first time, `cgroup.kill` once the window has gone by.
    void closeScope(const Profile &profile, Watched *watched, const Decision &decision,
                    const AppScope &scope, const QDateTime &now);
    void reconcileBlocked(Cycle *cycle);
    /// The browser's managed policy, made to say what the profiles say **and**
    /// what today says -- docs/design.md §11 and §5.2 joined.
    ///
    /// The same shape as `reconcileBlocked`, and it is the same shape on
    /// purpose: the content of the file is the answer and never a change to it,
    /// so no verb has to remember to take a site back out.
    void reconcileWebPolicy(const QVector<Profile> &profiles, Cycle *cycle);
    void endSessions(Cycle *cycle);
    bool worthSaying(const Watched &watched);

    const Proc *m_proc;
    Notifier *m_notifier;
    Enforcer *m_enforcer;
    PresenceSource *m_presence;
    FocusSource *m_focus;
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
