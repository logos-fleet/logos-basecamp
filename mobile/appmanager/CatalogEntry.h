#pragma once

#include <QHash>
#include <QString>
#include <QUrl>
#include <QVariantMap>

namespace basecamp::appmanager {

// ONE CATALOG ROW, AS THE APP MANAGER SEES IT.
//
// The App Manager on a Store shell shows a catalog it did not produce. Every
// row arrives as a JSON object from logos-package-downloader, annotated with an
// `availability` verdict by logos-package-manager, and three of its fields are
// things this shell will ACT on: an install control, a report link and a
// universal link. This is where a row stops being somebody else's JSON and
// becomes something the Shell is willing to act on.
//
// It decides two things and nothing else.
//
// WHETHER AN INSTALL MAY BE OFFERED. A Store shell installs `web` variants and
// nothing else — a phone may not download native code (ADR 0003) — so a catalog
// full of desktop packages is mostly uninstallable on one. The availability
// verdict is package_manager's (it owns the variant vocabulary); what this adds
// is that an unavailable row has NO install control rather than a disabled one,
// and carries the reason instead. A greyed-out button is not an answer to "why
// can I not install this"; "available on macOS and Linux, not in this build" is.
//
// WHICH LINKS MAY BE OPENED. A report link (guideline 4.7.1) and a universal
// link (4.7.4) come out of a third-party index, and "the Shell opens both" means
// the Shell hands a URL to the operating system. A catalog that wrote
// `file:///etc/passwd`, `javascript:…` or an app's own custom scheme there would
// be handing the platform something the index author chose, so the scheme is
// checked HERE, once, on the way in — not at every call site that might open one.
// A rejected link is absent, and says why, exactly like an unavailable install.
struct CatalogEntry {
    QString name;
    QString displayName;
    QString version;
    QString description;
    QString repositoryUrl;

    // package_manager's verdict, passed through. `unavailableReason` is the
    // sentence the entry shows, and is empty when available.
    bool    available = false;
    QString unavailableReason;
    // The variant an install would use. Empty when unavailable; the Shell names
    // it so "installs into the Web container" is something the user can read.
    QString variant;

    bool    installed = false;
    QString installedVersion;

    // Empty unless the row carried a link this shell is willing to open. The
    // `…Refusal` strings are non-empty exactly when a link was present and
    // refused, so a Shell can log a catalog that is doing something odd rather
    // than silently showing no report affordance.
    QUrl    reportUrl;
    QString reportUrlRefusal;
    QUrl    universalLink;
    QString universalLinkRefusal;

    // The one question the install control asks. An unavailable row is never
    // offered, and neither is one already installed at this version — the
    // upgrade path is a different control with a different confirmation.
    bool mayOfferInstall() const { return available && !installed; }

    bool canReport() const { return !reportUrl.isEmpty(); }
    bool canOpenUniversalLink() const { return !universalLink.isEmpty(); }
};

// Whether this shell will hand `raw` to the operating system, and why not.
//
// https always; http only for a loopback host, which is the local catalog
// release a developer serves while building one. Everything else is refused:
// a catalog is somebody else's file, and the set of schemes a phone will do
// something surprising with is open-ended (`file:`, `javascript:`, `data:`, an
// app's own custom scheme registered by whatever installed it).
//
// Returns an empty string when the URL is acceptable, else the reason.
QString linkRefusal(const QString& raw);

// Project one annotated catalog row. `installedVersions` maps a package name to
// the version installed on this device (from package_manager's
// getInstalledPackages), and decides the `installed` fields.
CatalogEntry entryFrom(const QVariantMap& annotatedRow,
                       const QHash<QString, QString>& installedVersions = {});

} // namespace basecamp::appmanager
