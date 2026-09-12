#pragma once

#include "webview/MobileWebModuleView.h"

#include <QString>

namespace basecamp::web {

// THE iOS HALF OF THE WEB CONTAINER: a WKWebView behind a WKURLSchemeHandler.
//
// Everything about what the page may fetch and how its frames cross is
// MobileWebBridge's and is tested on a desktop (tests/mobile_web_bridge_test.cpp).
// What is here is the four things liblogos cannot express and C++ cannot reach:
// a webview whose `logos:` requests arrive at the bridge, a script injected
// before the page's own, the load, and the report when WebContent dies.
//
// WHY A SCHEME HANDLER AND NOT A SCRIPT MESSAGE HANDLER. The obvious channel on
// iOS is WKScriptMessageHandler, and it does not work here: the mobile
// round-trip spike (2026-09-09) found it traps in JSC::sanitizeStackForVM when
// the page is entered under Qt's separate-main-stack entry. The scheme handler
// is the channel the Qt-wasm loader needs anyway, so it carries both.
PlatformPageFactory iosPlatformPageFactory();

// Where this app keeps its bundled Qt-wasm QML runtime, or an empty string when
// it ships none.
//
//   LOGOS_QML_RUNTIME_DIR       an explicit override, for a developer serving a
//                               runtime they just built
//   <Bundle>/logos-runtime      what the app build embeds
QString iosQmlRuntimeDir();

// The directory the app ships its Downloaded `web` modules in --
// <App>.app/web-modules, one subdirectory per module, as lgpm installs one.
// Empty when this build ships none.
QString iosWebModulesDir();

} // namespace basecamp::web
