// "Shows the Shell with Chat", as a round trip driven from inside the app.
//
// The Modules tab proves the app KNOWS its Bundled set. This proves the other
// half of a Store shell: that an app in that set can be opened and is on
// screen. It presses the sidebar tile a user would press, and then looks for
// the app's OWN handles in the rendered scene -- not for a widget the host
// just created, which would only prove the host created one.
//
// Nothing here names chat. The app is whichever `ui_qml` member the build
// resolved into the set, and the handles are read out of a list the driver is
// given; `--bundle view_counter` and `--bundle chat_ui` both run this.
#pragma once

#include "ShellSceneDriver.h"

#include <QStringList>

class BundledSetShellHost;

class ShellAppDriver : public ShellSceneDriver
{
    Q_OBJECT
public:
    ShellAppDriver(BundledSetShellHost* host, QWidget* shellWidget,
                   QObject* parent = nullptr);

    // Whether this Bundled set carries an app at all. `--bundle
    // capability_module,chat_module` is a perfectly good set with nothing to
    // open, and a driver that reported FAIL for it would be reporting on the
    // build rather than on the Shell.
    bool hasWork() const;

    // Opens the first app in the set from the sidebar and checks it rendered.
    // Runs after the first frame: a tile has no geometry before one.
    //
    // `expectLiveContent` is what separates "the QML rendered" from "the QML
    // is bound to the running core": when the Shell's own chat bring-up has
    // already left a conversation in the module, the app's list must show it.
    // A view can render its empty state perfectly with a null model, and the
    // models reach QML by a different route from the backend replica -- each
    // is its own remoted child source -- so nothing else here would catch a
    // model that never arrived.
    void run(bool expectLiveContent = false);

signals:
    // The app's QML is on screen, in milliseconds since the press. main.cpp
    // turns this into the cold-start marker.
    void appShown(const QString& name, qint64 elapsedMs);

private:
    // What this driver knows about one app's rendered view. One entry per
    // known app, because an app's own view is the only thing that can say it
    // rendered -- there is no generic "did the QML load" an objectName-based
    // driver can ask, and a check that accepted any scene at all would pass
    // on an empty one.
    struct KnownApp {
        // The handles the mounted app must show for it to count as rendered.
        QStringList handles;
        // The handle of the list whose `count` says the app has real data, or
        // empty if this app has no such list.
        QString contentList;
        // How often this app's backend calls the module it fronts on a timer,
        // or 0 for one that does not. The close-and-re-open check below waits
        // out one of these on the SECOND mount: a periodic call made through a
        // transport the first mount left behind is the shape of
        // logos-workspace#158, and a run that returns before the first tick
        // never makes it.
        int probeIntervalMs = 0;
    };
    static KnownApp knownApp(const QString& appName);

    BundledSetShellHost* m_host;  // not owned
};
