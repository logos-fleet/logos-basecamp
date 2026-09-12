#pragma once

#include "web/LogosWebPaths.h"   // the scheme, the host, the roots — shared with
                                 // the phone containers, which have no Chromium

#include <QByteArray>
#include <QObject>
#include <QString>

#include <QWebEngineUrlSchemeHandler>

class QWebEngineUrlRequestJob;

namespace basecamp::web {

// Register `logos:` with Chromium. MUST run before the QApplication exists —
// QWebEngineUrlScheme::registerScheme is ignored afterwards, and the failure
// mode is a page that loads nothing with no diagnostic. Idempotent.
void registerLogosWebScheme();

// Serves ONE module's package and the app's bundled QML runtime, on the desktop.
//
// The URL SHAPE is LogosWebPaths' — the same one the iOS and Android containers
// answer, because the loader that does the fetching is one shipped file and does
// not know which container it is in. What is desktop-specific is only how a
// request arrives and how a reply is handed back, which is what this class is.
//
// The desktop's channel is a QWebChannel (see WebModulePageView), so this
// handler serves DOCUMENTS ONLY and never sees a transport frame. The phone
// containers put the channel under `kControlPathPrefix` on this same scheme
// instead, because a WKScriptMessageHandler traps under Qt's separate main
// stack (the mobile round-trip spike) and Android's asset loader is the same
// seam by nature.
class LogosWebSchemeHandler : public QWebEngineUrlSchemeHandler {
    Q_OBJECT
public:
    LogosWebSchemeHandler(QString moduleDir, QString runtimeDir,
                          QObject* parent = nullptr);

    void requestStarted(QWebEngineUrlRequestJob* job) override;

private:
    QString m_moduleDir;
    QString m_runtimeDir;
};

} // namespace basecamp::web
