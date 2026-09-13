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
//     --call keystore_module.new_account(hunter2)
//     --call keystore_module.list_accounts
//     --call keystore_module.unlock(0xAb..,hunter2)
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
// EVERY REFUSAL IS KEPT rather than dropped, for the reason CatalogSource keeps
// its own: a phone's whole diagnostic surface is one console, and a mistyped
// flag that silently does nothing is indistinguishable from a module that did
// not answer.
class ModuleCallScript {
public:
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
