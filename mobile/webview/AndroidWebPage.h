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

} // namespace basecamp::web
