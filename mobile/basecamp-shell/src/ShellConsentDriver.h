// THE 4.7.3 CONSENT PROMPT, DRIVEN ON A DEVICE.
//
// capability_module refuses a call between a Downloaded module and anything
// else until the user has decided, announces `consentRequired`, and lets the
// caller's NEXT attempt carry the answer. Every part of that is unit-tested
// (appmanager/ConsentQueue, tests/consent_queue_test.cpp, capability_module's
// own tests) and none of it had ever run on a phone against a module the user
// actually installed, because until slice 29 landed there was no such module on
// a phone at all (logos-workspace#104).
//
// What only a device can answer is the last of the four:
//
//   1. the first call from the Downloaded module prompts
//   2. the call fails meanwhile, with the BROKER's reason and not a transport
//      error -- a call that died at MODULE_NOT_LOADED never reached the gate
//   3. a denial keeps it failing, and says why
//   4. a grant lets it through, AND SURVIVES A RELAUNCH -- which is
//      capability_module's on-disk store, and a second launch of the app is the
//      only thing that reads it
//
// So the run is two launches and the answers come off the command line
// (appmanager/ConsentScript.h):
//
//     --install web_counter_b --consent web_counter_b.package_manager=deny,grant
//     --consent web_counter_b.package_manager=expect-granted
//
// WHAT IT WATCHES. Two independent things, because each alone would be
// believable and wrong. capability_module's own `consentStatus` is the
// authority on what was decided -- but it is also the thing under test, so a
// driver that read only that would be asking the gate whether it had worked.
// The other is the CALLER's console: a `web` module draws into a canvas and
// what it says is the only thing outside it can read, so `logos-view:
// callModuleAsync -> ...` is the actual evidence that a call was refused and
// then, later, answered.
#pragma once

#include "appmanager/ConsentScript.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include <functional>

class ShellModulesBackend;

class ShellConsentDriver : public QObject
{
    Q_OBJECT
public:
    ShellConsentDriver(ShellModulesBackend* backend,
                       const basecamp::appmanager::ConsentScript& script,
                       QObject* parent = nullptr);

    // Whether this launch asked for anything at all.
    bool hasWork() const;

    // Walk every plan's steps in order. One console line per step, and one
    // verdict per plan: CONSENT OK or CONSENT FAILED with what was expected.
    //
    // AFTER THE CATALOG DRIVER, because the module whose consent is being
    // answered is usually the one it just installed, and after the first frame
    // like every other driver -- the page it is watching runs on this thread.
    void run();

signals:
    void log(const QString& line);

private:
    using Plan = basecamp::appmanager::ConsentScript::Plan;
    using Step = basecamp::appmanager::ConsentScript::Step;

    // Turn the loop for `ms`, or until `done` answers true. Returns what `done`
    // last answered. Pumping rather than sleeping is the whole job: the page
    // being watched runs on THIS thread, so a driver that blocked would be
    // waiting for something it was itself preventing.
    bool pumpUntil(int ms, const std::function<bool()>& done);

    // The prompt on screen for this pair, or an invalid map. Waits.
    QVariantMap awaitPrompt(const Plan& plan, int ms);

    // The caller's next `callModuleAsync` answer, as the page printed it --
    // everything after the arrow. Empty when none arrived in time.
    QString awaitCallResult(const Plan& plan, int fromLine, int ms);

    // capability_module's verdict on the pair, as one readable line.
    QString describeStatus(const Plan& plan) const;
    QString stateOf(const Plan& plan) const;

    // One step. False stops the plan: a script is a sequence, and continuing
    // past a failed step would assert against a state nothing produced.
    bool runStep(const Plan& plan, Step step);

    ShellModulesBackend* m_backend;   // not owned
    basecamp::appmanager::ConsentScript m_script;
    // Every line the backend has logged, so a step can ask what happened AFTER
    // it started rather than matching something from before.
    QStringList m_lines;
};
