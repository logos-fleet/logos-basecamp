#pragma once

#include "webview/MobileWebModuleView.h"

#include <QObject>
#include <QString>
#include <QStringList>

namespace basecamp::web {

// THE WEB CONTAINER'S BRING-UP PROBE: one page, on the device, round-tripping a
// frame.
//
// It exists because of what the desktop cannot answer. `nix build
// .#mobile-bridge-test` runs the same fixture in Chromium and proves the shim,
// the chunked sender and the long poll; what it cannot prove is that a
// WKWebView or an android.webkit.WebView delivers a request to its interceptor
// AT ALL when the page is entered under Qt's separate-main-stack entry — which
// is exactly where the mobile round-trip spike found WKScriptMessageHandler
// trapping, and the reason the channel is a URL scheme in the first place.
//
// So this is the smallest thing that settles it: a fixture page, a real
// webview, a frame each way, on the console. NO WASM AND NO QML RUNTIME — a 26
// MB image would only make the failure slower to reach, and what is in doubt is
// the channel, not the runtime that rides on it.
class WebPageProbe : public QObject {
    Q_OBJECT
public:
    // `platform` is the same factory the container is installed with, and
    // `shimInDocument` the same choice — a probe that ran a different
    // configuration would prove the wrong thing.
    WebPageProbe(PlatformPageFactory platform, bool shimInDocument,
                 WebOrigin origin = {}, QObject* parent = nullptr);

    // Bring the page up and wait for the round trip. Returns false, having said
    // why, when any step does not happen inside `timeoutMs`.
    bool run(int timeoutMs = 20000);

signals:
    void log(const QString& line);

private:
    // The fixture page, written into the app's own sandbox. Returns its
    // directory, or an empty string.
    QString writeFixture();

    // Every line the page's console produced, appended ON THE MAIN THREAD.
    // A member rather than a local in run(): Android delivers a page's log from
    // a Chromium background thread, so the append is posted, and a post that
    // outlives run() must not name a local. Cleared at the start of each run.
    QStringList m_console;
    // ...and every frame the page sent back, on the same thread and for the
    // same reason.
    QStringList m_fromPage;

    PlatformPageFactory m_platform;
    bool m_shimInDocument;
    WebOrigin m_origin;
};

} // namespace basecamp::web
