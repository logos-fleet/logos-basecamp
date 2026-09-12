#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>

#include <QWebEngineUrlSchemeHandler>

class QWebEngineUrlRequestJob;

namespace basecamp::web {

// THE SCHEME A WEB MODULE'S PAGE IS SERVED ON.
//
// `file://` is not an option and that is a property of the artifact, not a
// preference: a `ui_qml` module's `web` variant FETCHES its own QML document and
// loads the bundled runtime out of another directory, and a file:// page is a
// unique opaque origin that may do neither (ADR 0004's loader says so at
// length). Every container therefore puts the package behind a scheme — a
// WKURLSchemeHandler on iOS, WebViewAssetLoader on Android, this on desktop.
//
// ONE ORIGIN PER MODULE VIEW, and the runtime lives INSIDE it under a reserved
// path rather than at a host of its own. Two origins would mean CORS on every
// fetch the loader makes for a 26 MB image, for no isolation: the two
// directories are already chosen by the container, and a page cannot name a
// third. The handler is per-profile and each module view has its own profile,
// so `logos://module/` resolves to a different directory in every page — which
// is the same structural identity the container gets from one channel per view
// (ADR 0005).
constexpr const char* kSchemeName = "logos";

// The single host under `logos:`. A fixed word rather than the module's name:
// module names are `[a-z0-9_]` and an underscore is not a legal host label, so
// a name-derived host would work for most modules and silently fail for the
// rest.
constexpr const char* kModuleHost = "module";

// Where the app's bundled Qt-wasm QML runtime is served inside that origin.
// The loader reads it from `window.logosQmlRuntimeBase`; this is the value the
// container sets there.
constexpr const char* kRuntimePathPrefix = "/logos-runtime/";

// Register `logos:` with Chromium. MUST run before the QApplication exists —
// QWebEngineUrlScheme::registerScheme is ignored afterwards, and the failure
// mode is a page that loads nothing with no diagnostic. Idempotent.
void registerLogosWebScheme();

// Serves ONE module's package and the app's bundled QML runtime.
//
// Everything it will ever answer is under the two directories it is constructed
// with: `lgx::resolveMain` makes the package directory the package at install
// time, so the document root needs no second policy, and a request that escapes
// either root by any spelling is refused rather than clamped.
class LogosWebSchemeHandler : public QWebEngineUrlSchemeHandler {
    Q_OBJECT
public:
    LogosWebSchemeHandler(QString moduleDir, QString runtimeDir,
                          QObject* parent = nullptr);

    void requestStarted(QWebEngineUrlRequestJob* job) override;

    // What a file's extension is served as. Exposed because the MIME type is
    // load-bearing rather than cosmetic: `application/wasm` is what lets
    // WebAssembly.instantiateStreaming take the 26 MB image without buffering
    // it first, and `text/javascript` is what lets `<script type="module">`
    // import the shipped loader at all.
    static QByteArray mimeTypeFor(const QString& path);

private:
    // `root` + `relative`, or an empty string when the result would not be
    // under `root`. The one place traversal is decided.
    static QString resolveUnder(const QString& root, const QString& relative);

    QString m_moduleDir;
    QString m_runtimeDir;
};

} // namespace basecamp::web
