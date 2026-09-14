#pragma once

#include <QList>
#include <QString>
#include <QStringList>

namespace basecamp::appmanager {

// WHAT A LAUNCH WAS ASKED TO ANSWER THE 4.7.3 PROMPT WITH.
//
// The consent criterion of a Store shell is four things in a row -- the first
// call from a Downloaded module prompts, a denial fails the call with a clear
// error, a grant lets it through, and the grant survives a relaunch -- and only
// the first of those can be shown by looking at a screen. The rest are answers a
// user gives and a call made again afterwards, so on a device they arrive the
// way `--install` and `--call` do: as arguments, read once, at startup.
//
//     --consent web_counter_b.package_manager=deny,grant
//     --consent web_counter_b.package_manager=expect-granted
//
// TWO LAUNCHES, AND THAT IS THE POINT. The first denies, watches the caller's
// next attempt fail, then grants and watches it succeed. The second launch --
// the same app, started again -- asserts the grant was already there before
// anything asked, which is the only way to test a store that is on disk.
//
//   deny             wait for the prompt for this pair, answer NO, and assert
//                    the caller's next attempt is refused with capability_module's
//                    own reason rather than a transport error
//   grant            record a grant for the pair and assert the next attempt
//                    succeeds. No prompt is waited for: capability_module does
//                    not re-announce a pair it has a decision for, so a user
//                    changing their mind is a decision and not a second dialog
//   dismiss          "not now": drop the prompt, record nothing, and assert the
//                    pair is still undecided
//   expect-granted   assert the pair is ALREADY granted at startup, that no
//                    prompt appears, and that the call goes through
//
// EVERY REFUSAL IS KEPT rather than dropped, for the reason CatalogSource and
// ModuleCallScript keep theirs: a phone's whole diagnostic surface is one
// console, and a mistyped flag that silently does nothing is indistinguishable
// from a gate that never fired.
class ConsentScript {
public:
    enum class Step {
        Deny,
        Grant,
        Dismiss,
        ExpectGranted,
    };

    // One pair, and the answers this launch gives about it, in order.
    struct Plan {
        QString      caller;
        QString      target;
        QList<Step>  steps;
        // The whole `<caller>.<target>=<steps>` as it was written, for the console.
        QString      source;
    };

    // Parse an argument list -- QCoreApplication::arguments(), program name
    // included. Anything unrecognised belongs to Qt or to the platform and is
    // left alone.
    static ConsentScript fromArguments(const QStringList& args);

    // The word a step is spelled with on the command line.
    static QString stepName(Step step);

    QList<Plan> plans() const { return m_plans; }

    // One sentence per refused argument, in the order they were met.
    QStringList refusals() const { return m_refusals; }

    bool isEmpty() const { return m_plans.isEmpty() && m_refusals.isEmpty(); }

private:
    QList<Plan> m_plans;
    QStringList m_refusals;
};

} // namespace basecamp::appmanager
