#include "CatalogEntry.h"

namespace basecamp::appmanager {

namespace {

const QLatin1String kHttps("https");
const QLatin1String kHttp("http");

// A host the request cannot leave the device for. Loopback is what a local
// catalog release is served on while one is being built, and it is the only
// reason plain http is allowed at all.
bool isLoopback(const QString& host)
{
    return host == QLatin1String("localhost")
        || host == QLatin1String("127.0.0.1")
        || host == QLatin1String("[::1]")
        || host == QLatin1String("::1");
}

// One catalog link, as the entry carries it: the URL when this shell will open
// it, else a refusal saying why not.
//
// An ABSENT link is not a refusal worth reporting -- most of a catalog will
// carry none, and a Shell logging that per row would say nothing -- so it leaves
// both empty.
void resolveLink(const QString& raw, QUrl& url, QString& refusal)
{
    refusal.clear();
    if (raw.isEmpty())
        return;
    refusal = linkRefusal(raw);
    if (refusal.isEmpty())
        url = QUrl(raw, QUrl::StrictMode);
}

} // namespace

QString linkRefusal(const QString& raw)
{
    if (raw.isEmpty())
        return QStringLiteral("no link");

    // Strict parsing, so a URL the Shell would later "fix up" is refused here
    // rather than turning into something else on the way to the platform.
    const QUrl url(raw, QUrl::StrictMode);
    if (!url.isValid())
        return QStringLiteral("not a valid URL: %1").arg(url.errorString());
    if (url.isRelative())
        return QStringLiteral("relative URLs are not openable");

    const QString scheme = url.scheme();
    if (scheme != kHttps && scheme != kHttp)
        return QStringLiteral("refusing to open a '%1:' link from a catalog").arg(scheme);
    if (url.host().isEmpty())
        return QStringLiteral("no host");
    if (scheme == kHttp && !isLoopback(url.host())) {
        return QStringLiteral("refusing plain http to '%1'; a catalog link must be https "
                              "unless it is loopback").arg(url.host());
    }
    return {};
}

CatalogEntry entryFrom(const QVariantMap& annotatedRow,
                       const QHash<QString, QString>& installedVersions)
{
    CatalogEntry e;
    e.name          = annotatedRow.value(QStringLiteral("name")).toString();
    e.displayName   = annotatedRow.value(QStringLiteral("displayName")).toString();
    // A catalog need not carry a display name, and a row with no label is worse
    // than a row labelled by its module name.
    if (e.displayName.isEmpty())
        e.displayName = e.name;
    e.description   = annotatedRow.value(QStringLiteral("description")).toString();
    e.repositoryUrl = annotatedRow.value(QStringLiteral("repositoryUrl")).toString();

    // The version a row is ABOUT is the newest one, which is what an install
    // would fetch — versions[] is newest-first out of the downloader.
    const QVariantList versions = annotatedRow.value(QStringLiteral("versions")).toList();
    if (!versions.isEmpty()) {
        e.version = versions.first().toMap()
                        .value(QStringLiteral("manifest")).toMap()
                        .value(QStringLiteral("version")).toString();
    }

    // Availability is package_manager's verdict and is passed through. A row
    // with NO verdict is not available: an App Manager that read a missing
    // annotation as "fine" would offer an install for every native-only package
    // in the catalog, which is the exact failure the annotation exists for.
    const QVariantMap availability = annotatedRow.value(QStringLiteral("availability")).toMap();
    e.available = availability.value(QStringLiteral("available"), false).toBool();
    e.variant   = availability.value(QStringLiteral("variant")).toString();
    if (!e.available) {
        e.unavailableReason = availability.value(QStringLiteral("reason")).toString();
        if (e.unavailableReason.isEmpty()) {
            e.unavailableReason =
                QStringLiteral("this catalog did not say where '%1' runs").arg(e.name);
        }
    }

    const auto installed = installedVersions.constFind(e.name);
    if (installed != installedVersions.constEnd()) {
        e.installed = true;
        e.installedVersion = *installed;
    }

    resolveLink(annotatedRow.value(QStringLiteral("reportUrl")).toString(),
                e.reportUrl, e.reportUrlRefusal);
    resolveLink(annotatedRow.value(QStringLiteral("universalLink")).toString(),
                e.universalLink, e.universalLinkRefusal);

    return e;
}

} // namespace basecamp::appmanager
