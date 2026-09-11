# view_counter — the app's Bundled VIEW module

A `type: ui_qml` module: a Qt backend generated from `src/view_counter.rep`
plus `src/qml/Main.qml`. The smoke host carries logos-module-builder's `view`
output for it — one iOS embedded framework holding the compiled backend, the
typed source AND replica of the `.rep`, and the QML in the image's own `qrc`,
with Qt and `LogosAPI` bound upward into the app (ADR 0006).

`mobile/bare-counter` is the other half of the pair: a protocol-free module
with no Qt in it at all. This one is the opposite case — a module that is
nothing BUT Qt and still carries none of it.

The QML imports `QtQuick` and nothing else on purpose. A statically linked iOS
app has to import every QML plugin it uses at LINK time, and `QtQuick.Controls`
is a dozen more of them for a button this file can draw itself.
