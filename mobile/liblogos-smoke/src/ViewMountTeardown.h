// HOW A NATIVE VIEW MOUNT IS CLOSED, AND IN WHAT ORDER.
//
// A mounted `ui_qml` app is four objects: the module's plugin (which owns the
// backend, and through it the typed wrappers the backend calls its modules
// with), the QtRO node and host that carry the QML<->backend replica, and the
// LogosAPI this mount speaks as. Closing the app destroys all four -- and
// closing an app is deliberately NOT an unload, so the modules behind it stay
// loaded and keep whatever the view opened in them.
//
// WHICH MAKES THE ORDER THE WHOLE SUBJECT. A view's last words go OUT: the
// session chat_ui opened in chat_module is closed by chat_ui telling
// chat_module to close it. Torn down in build order the plugin goes last, after
// its transport, and the call it makes on the way out reaches nothing --
// "consumer wrapper has no transport (null bridge)". The module then holds a
// session nobody will ever close, and the NEXT mount opens another
// (logos-workspace#212).
//
// TWO HALVES, because a close has two moments and they are not the same one.
//
//   finishViewMount    The view is TOLD, at the instant the user closes the
//                      app. Both SDK contexts publish `aboutToUnload()` for
//                      exactly this -- "fired when the host is about to tear
//                      this view down ... the destructor still runs afterwards,
//                      but by then the framework context is gone"
//                      (logos_ui_plugin_context.h) -- and ui-host has driven it
//                      on the desktop since logos-qt-sdk#38. A phone has no
//                      ui-host: this is that call site.
//
//   destroyViewMount   The objects go, later: the widget the Shell was handed
//                      still holds the module's QML scene, so both it and the
//                      runner behind it are deleted on a later turn of the loop
//                      (BundledSetShellHost::unmountApp). That delay is why the
//                      two are separate -- run inside the delete, the view's
//                      "close my session" can land AFTER the user has re-opened
//                      the app and the module has opened a new one, and would
//                      then close THAT.
//
// Both are pure functions of the pointers, so the order is a unit test rather
// than six minutes and a device (tests/view_mount_teardown_test.cpp).
#pragma once

class QObject;

namespace basecamp::mobile {

// One mount's objects. Named by what they ARE rather than by the order they
// were built in, because the order they must be DESTROYED in is not it.
//
// Every member may be null: a mount that failed half-way through has some of
// them and not others, and teardown is the same call either way.
struct ViewMountParts {
    // The view module's plugin object, from the framework's C edge. Owns the
    // backend, the typed `LogosModules` aggregate behind it, and the hook.
    QObject* plugin = nullptr;
    // QRemoteObjectNode -- the replica side of the in-process seam.
    QObject* node = nullptr;
    // QRemoteObjectHost -- the source side.
    QObject* host = nullptr;
    // LogosAPI -- this mount's identity, and one per mount (the reason
    // logos-workspace#158 existed at all).
    QObject* api = nullptr;
};

// Tell the view to finish, with the whole mount still up.
//
// This is where a view spends its one chance to call the modules behind it, so
// everything it needs to make a call with has to still be there -- which is the
// reason this is not folded into destroyViewMount below.
//
// `graceMs` bounds the wait for a view that answers Asynchronous; a view that
// answers Synchronous, or declares no hook at all, costs one meta-call. Callers
// must not call it twice for one mount: a second `aboutToUnload()` is a second
// teardown to the view, and there is no state here to remember the first.
void finishViewMount(QObject* plugin, int graceMs);

// Destroy the mount: the plugin FIRST, then the transport it spoke over.
void destroyViewMount(const ViewMountParts& parts);

} // namespace basecamp::mobile
