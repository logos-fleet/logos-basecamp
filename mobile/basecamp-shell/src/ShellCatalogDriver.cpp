#include "ShellCatalogDriver.h"

#include "BundledSetShellHost.h"
#include "ShellSections.h"
#include "appmanager/StoreAppManager.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QQuickItem>

using basecamp::appmanager::CatalogSource;
using basecamp::appmanager::TrustAnchor;

ShellCatalogDriver::ShellCatalogDriver(BundledSetShellHost* host, QWidget* shellWidget,
                                      const CatalogSource& source, QObject* parent)
    : ShellSceneDriver(shellWidget, parent)
    , m_host(host)
    , m_source(source)
{
}

bool ShellCatalogDriver::hasWork() const
{
    return !m_source.isEmpty();
}

void ShellCatalogDriver::configure()
{
    if (!hasWork())
        return;

    ShellModulesBackend* backend = m_host->backend();

    // EVERY REFUSAL FIRST. A mistyped flag that silently did nothing is
    // indistinguishable from a catalog that is down, and the console is the
    // whole diagnostic surface a Store shell has.
    for (const QString& refusal : m_source.refusals())
        emit log(QStringLiteral("catalog: %1").arg(refusal));

    // THE ANCHORS BEFORE THE REPOSITORY, and long before the install. The
    // Shell's policy is `require`: without a keyring entry the signer prompt
    // would come up saying the package is not installable, and the reason would
    // read as a fault of the package rather than of this device's trust.
    for (const TrustAnchor& anchor : m_source.trustAnchors())
        backend->trustSigner(anchor.name, anchor.did);

    if (!m_source.repositoryUrl().isEmpty())
        backend->addRepository(m_source.repositoryUrl());

    backend->refreshAppCatalog();
    reportCatalog();
}

void ShellCatalogDriver::reportCatalog()
{
    basecamp::appmanager::StoreAppManager* manager = m_host->backend()->appManager();
    const QVariantList rows = manager->catalogEntries();
    if (rows.isEmpty()) {
        const QString why = manager->catalogUnavailableReason();
        emit log(QStringLiteral("catalog: no rows%1")
                     .arg(why.isEmpty() ? QString() : QStringLiteral(" -- ") + why));
        return;
    }

    emit log(QStringLiteral("catalog: %1 row(s)").arg(rows.size()));
    for (const QVariant& row : rows) {
        const QVariantMap e = row.toMap();
        const QString name = e.value(QStringLiteral("name")).toString();
        const bool available = e.value(QStringLiteral("available")).toBool();
        const bool canInstall = e.value(QStringLiteral("canInstall")).toBool();
        const QString verdict =
            available ? QStringLiteral("installable here as the '%1' variant")
                            .arg(e.value(QStringLiteral("variant")).toString())
                      : e.value(QStringLiteral("unavailableReason")).toString();

        // AVAILABILITY AND THE INSTALL CONTROL ON ONE LINE, because the
        // criterion is about the two together: an unavailable row must carry a
        // reason AND no control, and a row that said one without the other
        // would read as fine in a log that printed only the other.
        emit log(QStringLiteral("  %1 %2 -- %3; install control: %4")
                     .arg(name,
                          e.value(QStringLiteral("version")).toString(),
                          verdict,
                          canInstall ? QStringLiteral("shown") : QStringLiteral("absent")));
        if (!available && canInstall)
            emit log(QStringLiteral("WRONG: %1 is unavailable and still offers an install")
                         .arg(name));
    }
}

void ShellCatalogDriver::openLinks()
{
    const QString packageName = m_source.installPackage();
    if (packageName.isEmpty())
        return;
    // A BEAT, ON THE APP, before anything leaves it: the page finishes painting
    // here, and this is the window a screenshot or a screen recording of the
    // run has.
    settle(4000);

    basecamp::appmanager::StoreAppManager* manager = m_host->backend()->appManager();
    const basecamp::appmanager::CatalogEntry e = manager->entry(packageName);

    // A refused link says so: `…Refusal` is non-empty exactly when the index
    // published one this shell will not hand to the platform, and a missing
    // affordance with no line in the console is the failure that looks like
    // nothing at all.
    if (!e.reportUrlRefusal.isEmpty())
        emit log(QStringLiteral("catalog: %1's report link refused: %2")
                     .arg(packageName, e.reportUrlRefusal));
    if (!e.universalLinkRefusal.isEmpty())
        emit log(QStringLiteral("catalog: %1's universal link refused: %2")
                     .arg(packageName, e.universalLinkRefusal));

    if (e.canReport()) {
        emit log(QStringLiteral("catalog: report link for %1 is %2; opening")
                     .arg(packageName, e.reportUrl.toString()));
        if (!manager->openReportLink(packageName))
            emit log(QStringLiteral("WRONG: the platform refused %1's report link")
                         .arg(packageName));
    } else {
        emit log(QStringLiteral("catalog: %1 publishes no report link").arg(packageName));
    }

    if (e.canOpenUniversalLink()) {
        emit log(QStringLiteral("catalog: universal link for %1 is %2; opening")
                     .arg(packageName, e.universalLink.toString()));
        if (!manager->openUniversalLink(packageName))
            emit log(QStringLiteral("WRONG: the platform refused %1's universal link")
                         .arg(packageName));
    } else {
        emit log(QStringLiteral("catalog: %1 publishes no universal link").arg(packageName));
    }
}

void ShellCatalogDriver::run()
{
    const QString name = m_source.installPackage();
    if (name.isEmpty()) {
        emit log(QStringLiteral("catalog: nothing to install"));
        return;
    }

    ShellModulesBackend* backend = m_host->backend();

    const bool wasKnown = backend->downloadedModules().contains(name);
    if (wasKnown) {
        // The app was relaunched on a device that already has it. Not a
        // failure, and not evidence either: say which it is.
        emit log(QStringLiteral("catalog: %1 is already installed on this device from an "
                                "earlier run").arg(name));
    }

    backend->installFromCatalog(name);

    if (!backend->downloadedModules().contains(name)) {
        emit log(QStringLiteral("CATALOG INSTALL FAILED: %1 is not a Downloaded module on "
                                "this device").arg(name));
        return;
    }
    emit log(QStringLiteral("catalog: %1 is installed and the core knows it").arg(name));

    // A `core` MODULE HAS NOTHING TO PUT ON SCREEN, and that is the whole of
    // the criterion for one: it came from a catalog, the core loaded it and it
    // is answering. Looking for a tile it must not have would report the
    // correct outcome as a failure. What it can DO is reached by calling it
    // (`--call`, ShellCallDriver), which is the next thing this launch does.
    if (backend->isHeadlessWebModule(name)) {
        emit log(QStringLiteral("CATALOG INSTALL OK: %1 came from the catalog and is "
                                "running headless in the Web container -- no UI, so no "
                                "tile").arg(name));
        return;
    }

    if (!openInstalledApp(name))
        return;
    emit log(QStringLiteral("CATALOG INSTALL OK: %1 came from the catalog and is on screen")
                 .arg(name));
}

void ShellCatalogDriver::settle(int ms)
{
    QElapsedTimer since;
    since.start();
    while (since.elapsed() < ms)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
}

bool ShellCatalogDriver::openInstalledApp(const QString& packageName)
{
    ShellModulesBackend* backend = m_host->backend();

    // A PAGE IS WHAT MAKES A DOWNLOADED MODULE AN APP. The Shell holds no
    // manifest for one, so a `web` variant of a headless module has no UI and
    // gets no tile -- and saying so is the honest outcome rather than hunting
    // for a tile that should not exist.
    if (!backend->isWebContainerApp(packageName)) {
        emit log(QStringLiteral("catalog: %1 has no UI page in the Web container, so it has "
                                "no tile").arg(packageName));
        return false;
    }

    m_host->setCurrentSectionIndex(ShellSection::Workspace);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

    const QString tileHandle = QStringLiteral("sidebar.app.%1").arg(packageName);
    QQuickItem* tile = waitFor(tileHandle, 10000);
    if (!tile) {
        dumpNames(QStringLiteral("no sidebar tile for the Downloaded module '%1'")
                      .arg(packageName));
        return false;
    }
    emit log(QStringLiteral("catalog: the sidebar carries a tile for %1").arg(packageName));

    scrollIntoView(tile);
    if (!tap(tile))
        return false;

    // Opening a Downloaded module is a Z-ORDER instruction, not a mount: the
    // page was created at the window's size when the core loaded the module, so
    // what is checked is that the Shell is now SHOWING it -- there is no widget
    // of the app's own to look for in the scene.
    for (int i = 0; i < 100 && backend->currentVisibleApp() != packageName; ++i)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    if (backend->currentVisibleApp() != packageName) {
        emit log(QStringLiteral("WRONG: pressing %1's tile left '%2' on screen")
                     .arg(packageName, backend->currentVisibleApp()));
        return false;
    }
    return true;
}
