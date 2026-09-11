// The smoke assertions over the app's Bundled SET.
//
// "Bundled" is the whole of what makes this different from installing a module:
// every image ships inside the app, in the only place its platform will dlopen
// from, and nothing about the set is written at runtime.
//
//   iOS      <App>.app/Frameworks/<stem>.framework/<stem>, copied in by Xcode
//            with Code Sign On Copy, so each carries the app's own signature.
//            dyld will not load an unsigned image on a device, which rules out
//            staging a copy in the sandbox.
//   Android  <nativeLibraryDir>/lib<stem>.so, unpacked from the APK. Since API
//            29 dlopen() of anything under the app's writable data directory is
//            a W^X violation, so the same rule applies for a different reason.
//
// WHICH modules are here is `ws build --bundle`'s answer, resolved from the
// catalog at build time and recorded in the manifest BundledSetCoreRuntime
// reads. Nothing in this file names one, except the counter it calls.
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

class BundledSetCoreRuntime;

class BundledSetRunner : public QObject
{
    Q_OBJECT
public:
    explicit BundledSetRunner(BundledSetCoreRuntime* core, QObject* parent = nullptr);
    ~BundledSetRunner() override;

    // Loads every non-view member of the Bundled set, in the manifest's order
    // (dependencies first). Returns true only if every one came up -- a Bundled
    // set is a closure, so a member that does not load is a module whose
    // dependent is about to fail in a less obvious place.
    //
    // Must run after the runtime's start().
    bool run();

    // The counter's round trip: add(1, 2) must be 3. Separate from run()
    // because loading the set and exercising one of its members are different
    // claims, and only the first is about the Bundled-set machinery. A no-op
    // returning true when the set carries no counter.
    bool callCounter();

    // Whether the set carries a ui_qml member for the host to render. A set is
    // whatever --bundle named, so "there is a view module" is a question about
    // the manifest and not an assumption this host may make.
    bool hasViewModule() const;

signals:
    void log(const QString& line);

private:
    BundledSetCoreRuntime* m_core; // not owned
};
