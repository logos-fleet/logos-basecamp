#include "ShellCatalogPageDriver.h"

#include "BundledSetShellHost.h"
#include "ShellModulesBackend.h"
#include "appmanager/StoreAppManager.h"

#include <QQuickItem>
#include <QVariantMap>

namespace {

const QLatin1String kSectionButton("sidebar.section.app_manager");
const QLatin1String kPage("storeCatalogView");
const QLatin1String kNoCatalog("storeCatalog.unavailable");
const QLatin1String kRowPrefix("storeCatalog.row.");
const QLatin1String kInstallPrefix("storeCatalog.install.");
const QLatin1String kReasonPrefix("storeCatalog.reason.");

QString textOf(QQuickItem* item)
{
    return item ? item->property("text").toString() : QString();
}

} // namespace

ShellCatalogPageDriver::ShellCatalogPageDriver(BundledSetShellHost* host, QWidget* shellWidget,
                                               QObject* parent)
    : ShellSceneDriver(shellWidget, parent)
    , m_host(host)
{
}

bool ShellCatalogPageDriver::run()
{
    QQuickItem* button = waitFor(kSectionButton, 5000);
    if (!button) {
        dumpNames(QStringLiteral("the Applications section button is not in the sidebar"));
        emit log(QStringLiteral("WRONG: no %1 in the sidebar; the catalog page cannot be "
                                "opened at all").arg(kSectionButton));
        return false;
    }
    if (!tap(button)) {
        emit log(QStringLiteral("WRONG: the Applications section button could not be "
                                "pressed where it is drawn"));
        return false;
    }

    QQuickItem* page = waitFor(kPage, 5000);
    if (!page || !page->isVisible()) {
        dumpNames(QStringLiteral("the Applications section drew no catalog page"));
        emit log(QStringLiteral("WRONG: the Applications section is open and the catalog "
                                "page is not on it -- a Store shell's App Manager is the "
                                "only place its catalog is shown"));
        return false;
    }
    emit log(QStringLiteral("shell: Applications -> the catalog page is on screen"));

    const bool ok = checkRows();
    // HELD, so the page is on screen long enough to be seen. A screenshot or a
    // screen recording is the evidence this pass exists to make possible, and
    // the next driver takes the window back within a frame or two.
    settle(2000);
    return ok;
}

bool ShellCatalogPageDriver::checkRows()
{
    basecamp::appmanager::StoreAppManager* manager = m_host->backend()->appManager();
    if (!manager) {
        emit log(QStringLiteral("WRONG: this shell has no App Manager at all"));
        return false;
    }

    QVariantList entries = manager->catalogEntries();
    if (entries.isEmpty()) {
        // ONE RE-READ. The repository registry is persisted under the app's data
        // directory, so a launch that names no `--repository` still has the one
        // an earlier launch added -- and the App Manager's own refresh happens
        // at startup, which on a cold start is before the network is up.
        manager->refreshCatalog();
        settle(1500);
        entries = manager->catalogEntries();
    }

    if (entries.isEmpty()) {
        // A LEGITIMATE STATE, not a failure: a Store shell's Bundled set is data
        // (ADR 0007) and the smallest useful shell carries no package modules at
        // all. What is NOT legitimate is an empty page, which reads as a catalog
        // with nothing in it.
        QQuickItem* message = waitFor(kNoCatalog, 2000);
        const QString why = manager->catalogUnavailableReason();
        if (why.isEmpty()) {
            emit log(QStringLiteral("CATALOG PAGE: this launch is pointed at no "
                                    "repository, so there is nothing to list"));
            return true;
        }
        if (!message || !message->isVisible() || textOf(message) != why) {
            emit log(QStringLiteral("WRONG: there is no catalog (\"%1\") and the page "
                                    "does not say so -- an empty list reads as a catalog "
                                    "with nothing in it").arg(why));
            return false;
        }
        emit log(QStringLiteral("CATALOG PAGE OK: no catalog in this build, and the page "
                                "says \"%1\"").arg(why));
        return true;
    }

    QStringList refusals;
    int offered = 0;
    for (const QVariant& value : entries) {
        const QVariantMap entry = value.toMap();
        const QString name = entry.value(QStringLiteral("name")).toString();
        const bool canInstall = entry.value(QStringLiteral("canInstall")).toBool();
        const bool available = entry.value(QStringLiteral("available")).toBool();
        const QString reason = entry.value(QStringLiteral("unavailableReason")).toString();

        QQuickItem* row = waitFor(kRowPrefix + name, kRowTimeoutMs);
        if (!row) {
            dumpNames(QStringLiteral("the catalog page draws no row for %1").arg(name));
            emit log(QStringLiteral("WRONG: the catalog lists %1 and the page has no row "
                                    "for it").arg(name));
            return false;
        }

        QQuickItem* install = find(kInstallPrefix + name);
        if (canInstall) {
            if (!install) {
                emit log(QStringLiteral("WRONG: %1 may be installed here and the row "
                                        "offers no way to").arg(name));
                return false;
            }
            // NOT PRESSED. The control's job is to start a download and an
            // install; what is asserted here is that a finger could start one.
            scrollIntoView(install);
            if (!pressWouldReach(install))
                return false;
            ++offered;
            continue;
        }

        if (install) {
            emit log(QStringLiteral("WRONG: %1 cannot be installed here (%2) and the row "
                                    "offers an install control anyway -- it would succeed "
                                    "and the module would die at its first call")
                         .arg(name, reason.isEmpty() ? QStringLiteral("no reason given")
                                                     : reason));
            return false;
        }
        if (available)
            continue;  // unavailable only because it is already installed

        QQuickItem* shown = find(kReasonPrefix + name);
        if (!shown || !shown->isVisible()) {
            emit log(QStringLiteral("WRONG: %1 is listed unavailable (\"%2\") and the row "
                                    "does not say why -- a row that refuses silently is "
                                    "the failure this page exists to prevent")
                         .arg(name, reason));
            return false;
        }
        if (textOf(shown) != reason) {
            emit log(QStringLiteral("WRONG: %1's row says \"%2\" where the App Manager "
                                    "refused it with \"%3\"")
                         .arg(name, textOf(shown), reason));
            return false;
        }
        refusals << QStringLiteral("%1 -- %2").arg(name, reason);
    }

    emit log(QStringLiteral("CATALOG PAGE OK: %1 row(s) on screen, %2 offered for install, "
                            "%3 refused")
                 .arg(entries.size()).arg(offered).arg(refusals.size()));
    for (const QString& refusal : refusals)
        emit log(QStringLiteral("  refused on screen: %1").arg(refusal));
    return true;
}
