#include "web/LogosWebScheme.h"

#include <QFile>
#include <QUrl>
#include <QWebEngineUrlRequestJob>
#include <QWebEngineUrlScheme>

namespace basecamp::web {

void registerLogosWebScheme()
{
    // Chromium keeps its scheme registry for the life of the process and
    // answers a second registration of the same name with a warning, so ask
    // first. A host that calls this from main() and a test that calls it from
    // its own main() are both correct, and neither has to know about the other.
    if (!QWebEngineUrlScheme::schemeByName(kSchemeName).name().isEmpty())
        return;

    QWebEngineUrlScheme scheme(kSchemeName);
    scheme.setSyntax(QWebEngineUrlScheme::Syntax::Host);
    scheme.setDefaultPort(QWebEngineUrlScheme::PortUnspecified);
    // SecureScheme        — a wasm module's page is a secure context or it has
    //                       no WebAssembly.instantiateStreaming and no
    //                       MessageChannel semantics to rely on.
    // LocalAccessAllowed  — the loader fetches its own QML document.
    // CorsEnabled         — `<script type="module">` is fetched with CORS even
    //                       same-origin; without this the shipped
    //                       logos-view-loader.js is blocked and the page is
    //                       blank with one console line.
    // FetchApiAllowed     — the same import, and the QML fetch, go through it.
    scheme.setFlags(QWebEngineUrlScheme::SecureScheme
                    | QWebEngineUrlScheme::LocalAccessAllowed
                    | QWebEngineUrlScheme::CorsEnabled
                    | QWebEngineUrlScheme::FetchApiAllowed);
    QWebEngineUrlScheme::registerScheme(scheme);
}

LogosWebSchemeHandler::LogosWebSchemeHandler(QString moduleDir, QString runtimeDir,
                                             QObject* parent)
    : QWebEngineUrlSchemeHandler(parent)
    , m_moduleDir(std::move(moduleDir))
    , m_runtimeDir(std::move(runtimeDir))
{
}

void LogosWebSchemeHandler::requestStarted(QWebEngineUrlRequestJob* job)
{
    const QUrl url = job->requestUrl();
    if (url.host() != QLatin1String(kModuleHost)) {
        job->fail(QWebEngineUrlRequestJob::UrlNotFound);
        return;
    }

    const QString resolved = resolveDocument(m_moduleDir, m_runtimeDir, url.path());
    if (resolved.isEmpty()) {
        job->fail(QWebEngineUrlRequestJob::UrlNotFound);
        return;
    }

    // Parented to the job, which is what hands ownership of the reply device to
    // Chromium's request lifetime: a QFile on this stack would be closed before
    // the first byte of a 26 MB image was read.
    auto* file = new QFile(resolved, job);
    if (!file->open(QIODevice::ReadOnly)) {
        job->fail(QWebEngineUrlRequestJob::RequestFailed);
        return;
    }
    job->reply(mimeTypeFor(resolved), file);
}

} // namespace basecamp::web
