// Does a QtQuick Popup reach the screen from inside a QQuickWidget?
//
// logos-workspace#187: on the venue's physical iPad Air (4th gen) chat_ui's "+"
// menu and its New DM dialog have real geometry, take focus and raise the iOS
// keyboard, and NOTHING of either is on the screen. Every other pass in this
// host presses a control and reads a property afterwards, which is exactly the
// kind of evidence that stays green while the user sees nothing -- so this one
// reads PIXELS.
//
// It carries its own popup rather than driving the app's. A synthetic Popup in
// the Shell's own scene needs no module, no network and no conversation: it is
// the mechanism under test with everything else taken away, so a run costs a
// cold start rather than the two minutes chat's group commit takes, and it
// asks the same question of EVERY scene the Shell owns -- the Shell's own
// chrome (which #187 asks about in as many words) as well as a mounted app's.
//
// Two verdicts, and the second is not implied by the first:
//
//   1. is the popup in the surface's RENDERED FRAME? QQuickWidget::grab()
//      re-renders the scene, so a popup missing there is missing from the
//      scene graph -- the popup went somewhere else (a popup WINDOW, which a
//      QQuickWidget has no way to show) or is not drawn at all.
//   2. is that frame on the DISPLAY? Nothing in the process can answer that,
//      so the pass holds the popup up and prints where it is in screen
//      coordinates, for `simctl io screenshot` / `devicectl device capture
//      screenshot` on the host to photograph.
#pragma once

#include "ShellSceneDriver.h"

#include <QString>

class BundledSetShellHost;
class QQuickWidget;
class QWidget;

class ShellPopupDriver : public ShellSceneDriver
{
    Q_OBJECT
public:
    ShellPopupDriver(BundledSetShellHost* host, QWidget* shellWidget,
                     QObject* parent = nullptr);

    // Every scene the Shell owns, and a mounted app's too when one is up.
    void run();

private:
    // One scene, every shape. Returns false when the scene could not be probed
    // at all (no QML engine, no root object) -- which is a different answer
    // from "the popup did not paint".
    bool probe(QQuickWidget* surface);

    // One scene, one shape: put it in, open it, and say whether the frame
    // changed where it landed.
    bool probeShape(QQuickWidget* surface, const QString& scene, const QString& shape,
                    const char* qml);

    BundledSetShellHost* m_host;  // not owned
};
