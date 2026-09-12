#pragma once

#include "webview/MobileWebModuleView.h"

#include <QString>

namespace basecamp::web {

// THE ANDROID HALF OF THE WEB CONTAINER: android.webkit.WebView behind the
// `logos:` scheme, through co.logos.webview.LogosWebPage.
//
// The same four things the iOS half provides (see IosWebPage.h), over Android's
// one interception seam. Two Android facts shape it and both are why the shared
// bridge looks the way it does:
//
//   * WebResourceRequest HAS NO BODY. A frame from the page therefore rides
//     percent-encoded in the query, chunked -- which is also what iOS needs,
//     because WebKit hands a scheme handler a nil HTTPBody.
//   * THERE IS NO USER-SCRIPT API. `evaluateJavascript` runs after the page's
//     own first script, which is too late for a loader that reads
//     `window.logosQmlRuntimeBase` at the top of it. So the container serves an
//     entry document with the shim already in its head
//     (MobileWebBridge::setInjectsShimIntoHtml).
PlatformPageFactory androidPlatformPageFactory();

// Where this app keeps its bundled Qt-wasm QML runtime, or an empty string when
// it ships none.
//
//   LOGOS_QML_RUNTIME_DIR       an explicit override
//   <files>/logos-runtime       where the app unpacks it from its assets
QString androidQmlRuntimeDir();

// The directory the app's Downloaded `web` modules were unpacked into, or empty
// when this build ships none. See unpackAndroidWebAssets.
QString androidWebModulesDir();

// COPY THE APK'S `assets/logos-web` OUT, ONCE. An APK asset is not a file --
// Qt reads it through a virtual file engine that cannot answer
// canonicalFilePath(), which is how LogosWebPaths refuses a traversal -- so the
// runtime and the modules are unpacked into the app's data directory, where the
// same rule that guards the desktop container guards them.
//
// `stamp` identifies this build's assets: an unchanged one does not unpack
// again, a new app build does. Call before the Web container is installed.
void unpackAndroidWebAssets(const QString& stamp);

} // namespace basecamp::web
