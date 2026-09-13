#include "web/LogosWebPaths.h"

#include <QDir>
#include <QFileInfo>
#include <QLatin1String>

namespace basecamp::web {

WebOrigin WebOrigin::forModule(const QString& moduleName) const
{
    // The longest label a host may carry. A module name is far shorter in
    // practice; truncating rather than refusing keeps a pathological name on
    // an origin of its own instead of back in the shared one.
    constexpr int kMaxLabel = 63;

    QString label;
    for (const QChar c : moduleName) {
        if (c == QLatin1Char('_') || c == QLatin1Char('-'))
            label.append(QLatin1Char('-'));
        else if (c.unicode() < 128 && c.isLetterOrNumber())
            label.append(c.toLower());
    }
    if (label.size() > kMaxLabel)
        label.truncate(kMaxLabel);
    while (label.startsWith(QLatin1Char('-')))
        label.remove(0, 1);
    while (label.endsWith(QLatin1Char('-')))
        label.chop(1);
    if (label.isEmpty())
        return *this;

    return { scheme, label + QLatin1Char('.') + host };
}

QUrl WebOrigin::url(const QString& path) const
{
    QUrl out;
    out.setScheme(scheme);
    out.setHost(host);
    out.setPath(path);
    return out;
}

QByteArray mimeTypeFor(const QString& path)
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

QString resolveUnder(const QString& root, const QString& relative)
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

QString resolveDocument(const QString& moduleDir, const QString& runtimeDir,
                        const QString& path)
{
    QString requested = path;
    if (requested.isEmpty() || requested == QLatin1String("/"))
        requested = QStringLiteral("/index.html");

    // THE CHANNEL IS NOT A DOCUMENT. Checked here rather than only at the
    // container's door so that no container can be made to serve a file out of
    // the reserved prefix by a package that ships a directory of that name.
    if (requested.startsWith(QLatin1String(kControlPathPrefix)))
        return {};

    const QLatin1String runtimePrefix(kRuntimePathPrefix);
    QString root = moduleDir;
    QString relative;
    if (requested.startsWith(runtimePrefix)) {
        root = runtimeDir;
        relative = requested.mid(runtimePrefix.size());
    } else {
        relative = requested.mid(1);
    }

    const QString resolved = resolveUnder(root, relative);
    if (resolved.isEmpty() || !QFileInfo(resolved).isFile()) return {};
    return resolved;
}

} // namespace basecamp::web
