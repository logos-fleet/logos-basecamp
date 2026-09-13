#pragma once

#include <QList>
#include <QSet>
#include <QString>
#include <QVariantMap>

namespace basecamp::appmanager {

// THE SHELL'S HALF OF PER-MODULE CONSENT (App Store guideline 4.7.3).
//
// capability_module refuses a call between a Downloaded module and anything else
// until the user has decided, and announces it with `consentRequired`. It cannot
// wait for an answer — it sits on the dispatch path of every cross-module call
// in the process, so blocking one on a dialog would hold a module thread for as
// long as a person takes to read it. So the shape is: the call fails now, the
// Shell prompts, and the caller's next attempt is decided.
//
// That leaves the Shell holding a problem capability_module deliberately did not
// solve: a refused caller RETRIES. The SDK caches nothing on a failure, so a
// module in a retry loop produces one announcement (capability_module dedupes by
// pair) but a user who dismisses a dialog, or a second module asking about the
// same target, still arrives here. This class is what makes that a queue with
// one dialog on screen rather than a stack of them.
//
// Four rules, each of which is a way the naive version is wrong:
//
//   ONE PROMPT AT A TIME. A phone shows one modal. offer() while one is current
//   queues rather than replaces — replacing means the user answers a question
//   about a pair they never saw.
//
//   ONE ENTRY PER PAIR. Two announcements for the same (caller, target) are the
//   same question; the second must not become a second dialog the user has to
//   dismiss after answering the first.
//
//   AN ANSWER POPS, AND SAYS WHAT TO SEND. answer() returns the arguments for
//   capability_module.decideConsent rather than calling it, because the module
//   client belongs to the host and a queue that made RPCs could not be tested
//   without one.
//
//   AN UNANSWERED PAIR IS NOT REMEMBERED. dismiss() drops the prompt without
//   recording anything, so the pair stays undecided in capability_module too —
//   a Shell that recorded a dismissal as a denial would turn "not now" into
//   "never", which is the one answer the user did not give.
class ConsentQueue {
public:
    // One pending question, as the dialog needs it.
    struct Prompt {
        QString caller;        // the module asking
        QString target;        // the module it wants to reach
        QString callerOrigin;  // "bundled" | "downloaded"
        QString targetOrigin;

        bool isValid() const { return !caller.isEmpty() && !target.isEmpty(); }

        // What the dialog says. The DOWNLOADED party is named as the thing the
        // user chose to install, because that is the decision they are being
        // asked to stand behind; when both are Downloaded the caller is named,
        // since it is the one reaching out.
        QString question() const;
    };

    // A `consentRequired` payload, already parsed from its JSON. Ignored when it
    // names no pair, and ignored when this pair is already queued or on screen.
    // Returns true when it became a new pending prompt.
    bool offer(const QVariantMap& payload);

    bool hasPending() const { return !m_pending.isEmpty(); }
    int  pendingCount() const { return int(m_pending.size()); }

    // The prompt on screen. Invalid when nothing is pending.
    Prompt current() const;

    // Record the user's answer to the CURRENT prompt and move on. The returned
    // map is what to pass to capability_module.decideConsent:
    // { caller, target, granted }. Empty when nothing was pending.
    QVariantMap answer(bool granted);

    // "Not now": drop the current prompt without deciding. Nothing is sent, so
    // the pair stays undecided in capability_module and the calls it gates keep
    // failing — which is the honest outcome of a question the user did not
    // answer. It is not re-asked on the module's next retry (capability_module
    // announces an undecided pair once per process run); the question comes back
    // on a later launch, or after forgetConsent.
    void dismiss();

    // Every pair already answered in this session, so a Shell can show them and
    // so a test can assert an answer was not double-counted.
    QList<QVariantMap> answered() const { return m_answered; }

private:
    static QString key(const QString& caller, const QString& target);

    QList<Prompt>       m_pending;
    QSet<QString>       m_queuedKeys;
    QList<QVariantMap>  m_answered;
};

} // namespace basecamp::appmanager
