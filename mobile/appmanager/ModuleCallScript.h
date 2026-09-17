#pragma once

#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantList>

namespace basecamp::appmanager {

// ONE CALL A LAUNCH WAS ASKED TO MAKE.
struct ModuleCall {
    QString      module;
    QString      method;
    QVariantList args;
    // The whole `<module>.<method>(...)` as it was written, for the console.
    QString      source;
    // How long the driver waits for THIS call's answer. See kDefaultTimeoutMs.
    int          timeoutMs = 60000;
};

// THE ON-DEVICE `logoscore call`, parsed once from the app's own command line.
//
// WHY IT HAS TO EXIST. A `core` module has no UI, and on a desktop that is fine
// -- `logoscore call keystore_module list_accounts` is right there. A phone has
// no such thing: the only process that can reach the core is the app, and the
// only way into the app is its command line. So a headless module on a device
// was, until this, observable only through some OTHER module that happened to
// call it.
//
//     --call keystore_module.list_accounts
//     --call keystore_module.has_address(0xAb..)
//
// AND WHAT IT CANNOT REACH, which is worth knowing before a run is built around
// one. A --call arrives at a module as the HOST ANCHOR -- one undifferentiated
// credential covering the shells, `core_service` and every relayed CLI token --
// so a gated method refuses it however it is spelled. On keystore_module,
// reading is ungated and every mutation is Tier D, admitted to the configured
// custodian alone. Driving the module that HOLDS that role does not get round
// it either: a `ui_qml` module's `.rep` SLOTs are its VIEW's contract, published
// to the page's QML rather than as a LogosAPI module surface, so
// `--call wallet_ui.createAccount(...)` is accepted, answered with no value, and
// leaves nothing on the page's console.
//
// Repeatable, and run IN ORDER on one connection: "create a key, stop the app,
// start it again and list" is the shape most persistence questions have, and
// half of it is the ordering.
//
// ARGUMENTS ARE STRINGS UNLESS THEY SAY OTHERWISE, which is the opposite of
// what logoscore does and is deliberate. logoscore infers a type from the
// spelling, and a well-formed hex address is a NUMBER to it -- so every
// address-taking method on a keystore answers `null` with status ok
// (logos-fleet/logos-workspace#106). Inference cannot be made safe here either,
// because the same string is a legitimate value of three types. So the type is
// written down when it is not a string:
//
//     int:42   bool:true   json:{"chainId":1}   str:0x2a   0x2a (also a string)
//
// AND HOW LONG ONE MAY TAKE, which 60 000 ms was never a fact about.
//
//     --call-timeout 400000 --call railgun_module.live_send_probe(str:{})
//
// The budget was fixed at 60 s and a RAILGUN private send is 154 s on a
// simulator and 239 s on an iPad Air 4 (logos-fleet/logos-workspace#235), so a
// run that worked printed `CALL FAILED ... timed out after 60000ms` and the
// module went on to finish 94 s after the waiter had left. A waiter that
// reports a failure for work that succeeded is worse than one that waits: the
// console line is then the only evidence and the verdict line contradicts it.
//
// IT COVERS THE CALLS AFTER IT, not the whole line. One long operation on a
// script should not make a module that is simply not answering take the same
// four minutes to say so, so the budget is positional like the calls are and a
// later `--call-timeout` narrows it again.
//
// EVERY REFUSAL IS KEPT rather than dropped, for the reason CatalogSource keeps
// its own: a phone's whole diagnostic surface is one console, and a mistyped
// flag that silently does nothing is indistinguishable from a module that did
// not answer.
class ModuleCallScript {
public:
    // What a call waits by default, and what it waited always before #235.
    // Generous for a read and far short of a private send, which is why it is
    // now a floor rather than a ceiling.
    static constexpr int kDefaultTimeoutMs = 60000;

    // Parse an argument list -- QCoreApplication::arguments(), program name
    // included. Anything unrecognised belongs to Qt or to the platform and is
    // left alone.
    static ModuleCallScript fromArguments(const QStringList& args);

    QList<ModuleCall> calls() const { return m_calls; }

    // One sentence per refused argument, in the order they were met.
    QStringList refusals() const { return m_refusals; }

    // Every module named, once each, in first-mention order -- what the driver
    // has to get loaded before it can call anything.
    QStringList modules() const;

    bool isEmpty() const { return m_calls.isEmpty() && m_refusals.isEmpty(); }

private:
    QList<ModuleCall> m_calls;
    QStringList       m_refusals;
};

} // namespace basecamp::appmanager
