#include "ViewMountTeardown.h"

#include <QObject>

// The one implementation of the hook dance, borrowed rather than repeated:
// logos-plugin-qt owns it because logos_host and ui-host both drive it and two
// copies would drift (logos_plugin_unload.h). This is the third host.
#include "logos_plugin_unload.h"

namespace basecamp::mobile {

void finishViewMount(QObject* plugin, int graceMs)
{
    logos::runPluginAboutToUnload(plugin, graceMs);
}

void destroyViewMount(const ViewMountParts& parts)
{
    // THE PLUGIN BEFORE ITS TRANSPORT, which is the inverse of the order the
    // mount was built in. A view that never got the hook -- a mount that failed
    // half-way, a host that tore itself down without asking -- still has a
    // destructor, and whatever it says there still has to go somewhere
    // (logos-workspace#212). It is also what the desktop does: ui-host deletes
    // the plugin object and only then returns through its QRemoteObjectHost.
    //
    // Safe on the QtRO side, not merely tolerated: a source is a CHILD of the
    // object it remotes (QRemoteObjectSourceBase's ctor is `QObject(obj)`), so
    // destroying the backend unregisters the source from the host and sends the
    // removal to the replica rather than leaving the host holding a dangling
    // object.
    delete parts.plugin;
    // The node before the host, unchanged: the replica the node owns is the
    // client end of the pair, and it goes before the source end it reads.
    delete parts.node;
    delete parts.host;
    // The identity last. It is per-MOUNT, and the bridges that mirror tokens
    // from it hold it weakly for exactly that reason (logos-workspace#158) --
    // but nothing is served by freeing it while anything above could still ask.
    delete parts.api;
}

} // namespace basecamp::mobile
