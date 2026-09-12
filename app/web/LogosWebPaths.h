#pragma once

#include <QByteArray>
#include <QString>

// WHAT A WEB MODULE'S PAGE IS SERVED ON, AND WHAT IT MAY REACH — the half of
// the Web container's load path that is the same on every platform.
//
// Three containers implement ADR 0004's loader: a QWebEngineUrlSchemeHandler on
// the desktop (app/web/LogosWebScheme.h), a WKURLSchemeHandler on iOS and
// `shouldInterceptRequest` on Android (mobile/webview/MobileWebBridge.h). What
// they have in common is not the browser: it is the URL SHAPE the shipped
// loader is written against — where the module's package is, where the app's
// bundled QML runtime is, and which file a request names — and a page that
// worked on one container and not another because the three disagreed about
// that would be the most expensive kind of bug this codebase can have.
//
// So the shape lives here, once, with no browser type in it, and each container
// is the transport for it.
namespace basecamp::web {

// THE SCHEME. `file://` is not an option and that is a property of the artifact,
// not a preference: a `ui_qml` module's `web` variant FETCHES its own QML
// document and loads the bundled runtime out of another directory, and a
// file:// page is a unique opaque origin that may do neither (ADR 0004's loader
// says so at length).
constexpr const char* kSchemeName = "logos";

// The single host under `logos:`. A fixed word rather than the module's name:
// module names are `[a-z0-9_]` and an underscore is not a legal host label, so
// a name-derived host would work for most modules and silently fail for the
// rest. One origin per module VIEW is what the container gives instead, by
// serving a different directory behind this host in every page.
constexpr const char* kModuleHost = "module";

// Where the app's bundled Qt-wasm QML runtime is served inside that origin.
// The loader reads it from `window.logosQmlRuntimeBase`.
constexpr const char* kRuntimePathPrefix = "/logos-runtime/";

// The reserved path the page's own CHANNEL lives under, used by the containers
// that have no message bridge of their own (iOS, Android). Reserved rather than
// merely chosen: it is checked BEFORE the document roots, so a package that
// ships a directory of this name cannot shadow the channel, and the roots
// cannot serve a file out of it.
constexpr const char* kControlPathPrefix = "/__logos/";

// What a file's extension is served as. The MIME type is load-bearing rather
// than cosmetic: `application/wasm` is what lets WebAssembly.instantiateStreaming
// take the 26 MB runtime image without buffering it first, and `text/javascript`
// is what lets `<script type="module">` import the shipped loader at all.
QByteArray mimeTypeFor(const QString& path);

// `root` + `relative`, or an empty string when the result would not be a file
// under `root`. THE ONE PLACE TRAVERSAL IS DECIDED, for every container.
QString resolveUnder(const QString& root, const QString& relative);

// The file a `logos://module/<path>` request names, or an empty string when it
// names none. `path` is the URL's path component.
//
// Everything it will ever answer is under the two directories: `lgx::resolveMain`
// makes the package directory the package at install time, so the document root
// needs no second policy, and a request that escapes either root by any spelling
// is refused rather than clamped.
QString resolveDocument(const QString& moduleDir, const QString& runtimeDir,
                        const QString& path);

} // namespace basecamp::web
