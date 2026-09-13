#pragma once

#include <QLatin1String>
#include <QSet>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

namespace basecamp::shell {

// A Bundled member of this type is the HOST's to instantiate, not the core's to
// load (ADR 0006). Every place that asks this question -- here and in
// ShellModulesBackend -- must agree, so there is one spelling of it.
inline const QLatin1String kViewModuleType("ui_qml");

// WHAT A STORE SHELL'S MODULES TAB AND SIDEBAR SHOW, as a pure function of the
// four facts that decide it.
//
// It used to be one fact. A phone's installed set was the Bundled-set manifest
// and could not be anything else (ADR 0003), so every row came from it, in its
// load order, and a row the manifest did not account for was a defect worth
// failing the acceptance pass on.
//
// A Downloaded module is the case that is not covered by that. It arrives after
// the build, the core discovers it in a directory the user's install wrote, and
// the manifest has never heard of it -- so "everything the core knows that the
// manifest does not" is the other half of the list, and the two halves differ
// in what they mean:
//
//                        Bundled                     Downloaded
//   where it came from   the app image -- the        a catalog, at run time
//                        manifest, or the shipped
//                        `web-modules` tree
//   installType          embedded                    downloaded
//   who loads a view     the HOST (ADR 0006: a       the CORE -- a `web`
//                        ui_qml framework is         variant runs in the Web
//                        instantiated in-process)    container, which is a
//                                                    core container
//   a missing member     its image is wrong; the     nothing: `known` is how
//                        closure was verified at     it got into this list
//                        build time
//
// Splitting it out of ShellModulesBackend is what makes the rule testable at
// all: on a device "the tile is missing" and "the module did not install" look
// identical, and the second is three steps away from the first.
struct ModuleFacts {
    // The Bundled-set manifest, in load order. Each entry carries at least
    // `name`, `type` and `version`.
    QVariantList  bundledSet;
    // What the core has discovered -- the Bundled members it registered PLUS
    // whatever was installed into a scanned directory.
    QStringList   known;
    QStringList   loaded;
    // Modules THIS BUILD SHIPS that the Bundled-set manifest does not name:
    // the app's own `web-modules` tree. The manifest is the native Bare
    // frameworks and has never carried these, but they came in with the app
    // image all the same -- so they are `embedded`, and calling them
    // Downloaded would tell the user the build installed itself something.
    QStringList   shipped;
    // Bundled `ui_qml` members whose framework the host has open and whose QML
    // is on screen. The only thing that makes such a row read as loaded.
    QSet<QString> mountedViews;
    // Downloaded modules the Web container has a page open for. This is what
    // makes one an APP: the Shell has no manifest for it to read a type off,
    // and a `web` variant of a HEADLESS module has no UI to mount.
    QSet<QString> openPages;
};

// Every name in the Bundled-set manifest, in its order.
QStringList bundledNames(const QVariantList& bundledSet);

// Names the core knows that neither the manifest nor the shipped tree accounts
// for, in discovery order. Empty on a build that has never installed anything.
QStringList downloadedModules(const ModuleFacts& facts);

// One row per module in ModuleInstanceModel's shape: the Bundled set first, in
// the manifest's order, then the Downloaded modules. The manifest's order is
// the closure's LOAD order, so a Downloaded row spliced into it by name would
// read as part of that closure.
QVariantList moduleRows(const ModuleFacts& facts);

// The sidebar's tiles, in UIPluginManager::buildAppRow's shape: the Bundled
// set's `ui_qml` members, then every module whose UI is a page in the Web
// container.
QVariantList launcherApps(const ModuleFacts& facts);

// Whether this module's UI is a PAGE in the Web container rather than a
// framework the host instantiates -- which is what decides how it is put on
// screen (BundledSetShellHost::mountApp), and is the same question `launcherApps`
// answers to give it a tile. The two must not be able to disagree: a tile whose
// module the host then refuses to mount is a button that does nothing.
//
// Shipped or Downloaded makes no difference to it. Both are discovered by the
// core rather than declared by the manifest, both run in the container, and
// only where they CAME FROM tells them apart (see `shipped`).
bool isWebContainerApp(const ModuleFacts& facts, const QString& name);

} // namespace basecamp::shell
