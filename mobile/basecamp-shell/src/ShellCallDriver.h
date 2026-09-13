// CALLING A MODULE ON A DEVICE, from the app's own command line.
//
// The other drivers exercise a SCENE -- a tab, a tile, a page. This one
// exercises a module that has none, and that is the whole gap it fills. A
// `core` module has no UI by definition, and a `web` variant of one is a
// headless page in the container: on a desktop `logoscore call` reaches it, and
// on a phone there is no such thing as a second process. The only thing that
// can reach the core is the app, and the only way into the app is its
// arguments.
//
//   --call keystore_module.new_account(hunter2)
//   --call keystore_module.list_accounts
//
// WHAT IT PROVES THAT NOTHING ELSE HERE CAN. Run twice across an app restart it
// is a persistence test: a `web` module's store lives in its page, the page dies
// with the process, and "the key is still there" can only be asked by a second
// launch. Nothing inside one run can tell a durable write from one that merely
// has not been lost yet.
//
// It loads what it names, through the ordinary core path, exactly as the Modules
// tab does -- so a module that the app ships, or that the App Manager installed,
// is reached the same way.
#pragma once

#include "appmanager/ModuleCallScript.h"

#include <QObject>
#include <QString>

class ICoreRuntime;
class LogosAPIClient;

class ShellCallDriver : public QObject
{
    Q_OBJECT
public:
    ShellCallDriver(ICoreRuntime* core, const basecamp::appmanager::ModuleCallScript& script,
                    QObject* parent = nullptr);
    ~ShellCallDriver() override;

    // Whether this launch asked for anything at all.
    bool hasWork() const;

    // Load every module named, then make every call in order. Each answer is a
    // console line; the verdict is one more at the end.
    //
    // AFTER THE FIRST FRAME, like the other drivers, and for a reason of its
    // own: a `web` module's page is a webview that has to be mounted, load its
    // document and bring up a wasm image before it can answer anything, and all
    // of that happens on the event loop this would otherwise be blocking.
    void run();

signals:
    void log(const QString& line);

private:
    // "Ensure loaded", and then ensure REACHABLE. A `web` module is published
    // by its page rather than by the load, so the core can report it loaded a
    // beat before the first call would succeed.
    bool ensureLoaded(const QString& name);
    bool awaitReachable(LogosAPIClient* client, const QString& name);

    ICoreRuntime* m_core;  // not owned
    basecamp::appmanager::ModuleCallScript m_script;
};
