#pragma once

#include "Notify.h"
#include "Policy.h"
#include "Proc.h"

#include <QDateTime>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

namespace omahouse {

// The loop of spec.md §5, and stage 6 of plan.md: watch, count, and say.
//
// The steps are the ones the spec numbers. Find the users with a profile who
// have a session; list their app scopes; hand the scopes, the profile, the day's
// ledger and the time to `evaluate`; write the ledger back; and carry out the
// decisions. The clock enters here, in the caller, and nowhere else -- that is
// the whole reason `evaluate` takes a `now` -- and so does every file and every
// process.
//
// This stage carries out `Warn` and only `Warn`. `Close` and `Logout` are the
// teeth of stage 7 and are exercised in the VM of testing.md, never on a
// development machine; a decision of either kind is recorded here and stepped
// over, which in ordinary use never even comes up, because a profile is born
// with `enforce: false` and `evaluate` emits neither without it.

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
    /// The `Close` and `Logout` decisions, which this stage does not carry out.
    QVector<Decision> notYet;
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

    /// Neither pointer is owned, and neither may be null.
    Watch(const Proc *proc, Notifier *notifier, const Options &options);

    const Options &options() const { return m_options; }

    /// One turn of spec.md §5, for every profile handed in.
    ///
    /// The profiles come in by parameter and are not read here, so that the
    /// caller can read them again every cycle: `spec.md` §1 asks that an
    /// operator be able to hand over ten minutes with the game still running,
    /// and a daemon holding a copy of the rules from when it started cannot
    /// honour that.
    Cycle tick(const QVector<Profile> &profiles, const QDateTime &now);

private:
    void observe(const Profile &profile, Watched *watched, const QDateTime &now);
    bool worthSaying(const Watched &watched);

    const Proc *m_proc;
    Notifier *m_notifier;
    Options m_options;
    /// What each user's cycle looked like last time, so that a cycle that says
    /// the same thing says nothing at all.
    QHash<QString, QString> m_shape;
};

} // namespace omahouse
