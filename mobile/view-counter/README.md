# view_counter — the app's Bundled VIEW module

A `type: ui_qml` module: a Qt backend generated from `src/view_counter.rep`
plus `src/qml/Main.qml`. The smoke host carries logos-module-builder's `view`
output for it — one iOS embedded framework holding the compiled backend, the
typed source AND replica of the `.rep`, and the QML in the image's own `qrc`,
with Qt and `LogosAPI` bound upward into the app (ADR 0006).

`mobile/bare-counter` is the other half of the pair: a protocol-free module
with no Qt in it at all. This one is the opposite case — a module that is
nothing BUT Qt and still carries none of it.

## It does not hold the count

`increment()` and `add()` both call **bare_counter**, and the count the QML
renders is the one that module owns. That is the ordinary shape for a view —
the state belongs to a core module — and it is what makes this module's
declared dependencies real:

| dependency | why |
|---|---|
| `bare_counter` | the target of the call |
| `capability_module` | how the call is authorised. A module-to-module call mints its token through `capability_module`'s `requestModule` (logos-protocol's `LogosAPIClient`); the host's own calls skip that because the host is the trust root, a module's do not. |

So `--bundle view_counter` resolves a three-member closure out of the catalog,
and the failure of either dependency to arrive shows up in the view's `status`
property rather than as a silently wrong number.

The QML imports `QtQuick` and nothing else on purpose. A statically linked iOS
app has to import every QML plugin it uses at LINK time, and `QtQuick.Controls`
is a dozen more of them for a button this file can draw itself.
