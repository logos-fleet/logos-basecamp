#include "web/LogosWebScheme.h"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
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

QByteArray LogosWebSchemeHandler::mimeTypeFor(const QString& path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == QLatin1String("html") || suffix == QLatin1String("htm"))
        return QByteArrayLiteral("text/html");
    if (suffix == QLatin1String("js") || suffix == QLatin1String("mjs"))
        return QByteArrayLiteral("text/javascript");
    if (suffix == QLatin1String("wasm"))
        return QByteArrayLiteral("application/wasm");
    if (suffix == QLatin1String("json"))
        return QByteArrayLiteral("application/json");
    if (suffix == QLatin1String("css"))
        return QByteArrayLiteral("text/css");
    if (suffix == QLatin1String("svg"))
        return QByteArrayLiteral("image/svg+xml");
    if (suffix == QLatin1String("png"))
        return QByteArrayLiteral("image/png");
    // A module's QML document is FETCHED AS TEXT and compiled by the runtime
    // (ADR 0004), so it is served as text rather than as an unknown type a
    // browser would offer to download.
    if (suffix == QLatin1String("qml"))
        return QByteArrayLiteral("text/plain");
    return QByteArrayLiteral("application/octet-stream");
}

QString LogosWebSchemeHandler::resolveUnder(const QString& root, const QString& relative)
{
    if (root.isEmpty()) return {};
    const QString canonicalRoot = QFileInfo(root).canonicalFilePath();
    if (canonicalRoot.isEmpty()) return {};

    const QString candidate = QDir(canonicalRoot).filePath(relative);
    const QString canonical = QFileInfo(candidate).canonicalFilePath();
    if (canonical.isEmpty()) return {};

    // Canonical on both sides, so `..`, a symlink out of the package and a
    // percent-encoded separator are all the same question and get the same
    // answer.
    if (canonical != canonicalRoot
        && !canonical.startsWith(canonicalRoot + QDir::separator()))
        return {};
    return canonical;
}

void LogosWebSchemeHandler::requestStarted(QWebEngineUrlRequestJob* job)
{
    const QUrl url = job->requestUrl();
    if (url.host() != QLatin1String(kModuleHost)) {
        job->fail(QWebEngineUrlRequestJob::UrlNotFound);
        return;
    }

    QString path = url.path();
    if (path.isEmpty() || path == QLatin1String("/"))
        path = QStringLiteral("/index.html");

    QString root = m_moduleDir;
    QString relative;
    if (path.startsWith(QLatin1String(kRuntimePathPrefix))) {
        root = m_runtimeDir;
        relative = path.mid(static_cast<int>(qstrlen(kRuntimePathPrefix)));
    } else {
        relative = path.mid(1);
    }

    const QString resolved = resolveUnder(root, relative);
    if (resolved.isEmpty() || !QFileInfo(resolved).isFile()) {
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
