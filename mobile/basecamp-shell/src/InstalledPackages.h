// WHAT THE PACKAGES ON THIS DEVICE DECLARE, with nothing loaded.
//
// A Store shell reads a module's shape from one of two places. A Bundled member
// is in the manifest the build compiled in, so the Shell has known its type
// since before the process started. A module the CORE discovered -- one the user
// installed, or one out of the app's own `web-modules` tree -- has no such
// entry, and for a while the only thing the Shell could read off it was that the
// Web container had opened a page for it.
//
// That is evidence with a lifetime: it exists while the module is running, and a
// Downloaded module runs exactly once, in the launch that installed it. A Store
// shell's cold start deliberately does not load what it discovers -- one `web`
// page is 290 MB of QML runtime and seconds of it, and the container's budget is
// ONE live runtime, so bringing every installed app up at startup would evict
// the app the user actually wanted. The consequence was #123: install an app,
// relaunch, and its tile was gone from the sidebar.
//
// The package itself is the evidence that does not need a page. package_manager
// wrote a manifest.json beside the module when it installed it and the app image
// carries one beside every shipped `web` module, and `type` in it is the SAME
// field the container reads to decide whether a page serves a UI
// (MobileWebModuleView's servesUiOf). So the Shell can draw the tile from the
// package, and load the module when the tile is pressed.
#pragma once

#include <QString>
#include <QStringList>

namespace basecamp::shell {

// Every package under these module directories whose manifest declares a USER
// INTERFACE, by the name the manifest gives it -- which is the name the core
// registers it under, and the only one that intersects `knownModules()`.
//
// `type` decides, and anything but `core` is a UI, including a manifest with no
// `type` at all: that is what every package written before a `core` `web`
// variant existed looks like, and all of them are views. A directory with no
// manifest.json, an unparseable one, and lgpm's own dot-prefixed staging trees
// are not packages -- the last of those can outlive a crash mid-swap holding a
// valid manifest, and package_manager skips it on the same rule.
//
// A directory that does not exist contributes nothing: a build that ships no
// `web-modules` tree, or a device on which nothing has been installed yet, is
// the ordinary case and not an error.
QStringList uiPackageNames(const QStringList& moduleDirs);

} // namespace basecamp::shell
