// WHAT HAS TO BE RUNNING BEFORE A NATIVE VIEW IS MOUNTED.
//
// A `ui_qml` member of the Bundled set is instantiated by the HOST, in this
// process (ADR 0006), and the modules it calls are loaded by the CORE. Nothing
// used to join those two halves: mounting an app dlopened its framework,
// instantiated its backend and loaded its QML, while every module it declares
// sat registered and unloaded. chat_ui calls chat_module.init() from its own
// construction, so on a plain launch -- no `--drive` flags, i.e. exactly what a
// shipped app does -- it was told "No token found for module chat_module",
// reported the failure to a log nobody was reading, and went on to remote its
// three models and draw an ordinary conversation list over a dead backend
// (logos-workspace#205).
//
// THE WEB CONTAINER NEVER HAD THIS. A page's module is brought up on the tile
// press (BundledSetShellHost::mountWebApp), and the core loads a closure
// topologically, so a `web` app's whole chain is up before its page can call
// anything. This is the same rule for the native half.
//
// A PURE FUNCTION, over the facts the Shell already has and a `load` the caller
// supplies. On a device "the app came up dead" is six minutes and a hand-load
// from the evidence -- the Modules tab will load chat_module perfectly well and
// the app then works, which is what made this look like a chat defect for as
// long as it did. Here it is a unit test.
#pragma once

#include "ShellModuleRows.h"

#include <QString>
#include <QStringList>

#include <functional>

namespace basecamp::shell {

// Whether the mount may proceed, and what it took.
struct ViewMountVerdict {
    // Every declared dependency is running. The ONLY thing the host reads to
    // decide whether to instantiate the framework.
    bool        ready = false;
    // The ones this call brought up, in the order it did. Evidence, and what
    // the console line is made of.
    QStringList loaded;
    // Declared, and the core has never heard of them: not in the Bundled set
    // the build embedded and not in any directory it scanned. A build-time
    // closure makes this nearly impossible for a Bundled member and entirely
    // possible for a `web` dependency the image's other half was meant to carry
    // (#183), so it is a case with a name rather than a failed load.
    QStringList missing;
    // Here, and would not come up. A different sentence to whoever reads the
    // screen: the image is present and something about it is wrong.
    QStringList refused;
};

// The dependencies `name` declares, straight off the Bundled-set manifest, in
// declaration order. Empty for a member the manifest does not carry -- a
// Downloaded module is the core's and the Shell has no manifest to read.
QStringList declaredDependencies(const ModuleFacts& facts, const QString& name);

// Bring up everything `name` declares that is not running yet, in declaration
// order, and say whether the mount may go ahead.
//
// `load` brings ONE module up and returns whether it is running afterwards.
// Only the DIRECT dependencies are offered to it: the core loads a closure
// (LoadPolicy::RequiredDeps), so whatever sits under each name is its to walk
// -- which is also why this must not pre-expand the chain itself.
ViewMountVerdict openViewDependencies(const ModuleFacts& facts, const QString& name,
                                      const std::function<bool(const QString&)>& load);

} // namespace basecamp::shell
