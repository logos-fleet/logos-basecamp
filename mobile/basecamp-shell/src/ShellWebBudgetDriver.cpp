#include "ShellWebBudgetDriver.h"

#include "BundledSetShellHost.h"
#include "ShellSections.h"
#include "WebAppSurface.h"
#include "webview/AppMemory.h"
#include "webview/MobileWebContainerBackend.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QQuickItem>
#include <QWidget>

namespace {
// How long a `web` module gets to open its page: the same figure
// ShellWebAppDriver uses, and for the same reason -- a wasm image and the app's
// 26 MB QML runtime are seconds of work on a phone.
constexpr int kPageBudgetMs = 60000;
// How long the app is left alone after a page comes up before it is weighed.
// A page that has just loaded is still allocating; the figure this pass exists
// to report is the settled one, not the peak of the load.
constexpr int kSettleMs = 4000;

// A MEASURED FIGURE, or what to say where the platform will not report one.
// appResidentBytes() and deviceMemoryBytes() both answer -1 there, and every
// line this pass prints has to stay readable on such a device rather than
// claiming it weighed -0 MB.
QString reportedAs(qint64 bytes, const QString& whenUnknown)
{
    return bytes < 0 ? whenUnknown : basecamp::web::megabytes(bytes);
}
} // namespace

using basecamp::web::LiveRuntimeBudget;
using basecamp::web::MobileWebContainerBackend;
using basecamp::web::megabytes;

ShellWebBudgetDriver::ShellWebBudgetDriver(BundledSetShellHost* host, QWidget* shellWidget,
                                           QObject* parent)
    : ShellSceneDriver(shellWidget, parent)
    , m_host(host)
{
}

QStringList ShellWebBudgetDriver::tiledWebApps() const
{
    ShellModulesBackend* backend = m_host->backend();
    QStringList apps;
    for (const QVariant& tile : backend->launcherApps()) {
        const QString name = tile.toMap().value(QStringLiteral("name")).toString();
        if (backend->isWebContainerApp(name)) apps.append(name);
    }
    return apps;
}

QStringList ShellWebBudgetDriver::shippedWebModulesToLoad() const
{
    ShellModulesBackend* backend = m_host->backend();
    const QStringList bundled = backend->bundledSetNames();
    QStringList tree;
    for (const QString& name : backend->shippedModuleNames()) {
        if (bundled.contains(name)) continue;
        if (MobileWebContainerBackend::instance()->hasView(name)) continue;
        tree.append(name);
    }
    return tree;
}

void ShellWebBudgetDriver::loadShippedWebModules()
{
    const QStringList tree = shippedWebModulesToLoad();
    // NOTHING ASKED FOR IS NOTHING TO WAIT FOR. A build whose `web` half is all
    // inside the Bundled set would otherwise spend the whole page budget below
    // spinning an event loop over a load that was never started.
    if (tree.isEmpty()) return;

    ShellModulesBackend* backend = m_host->backend();
    for (const QString& name : tree) {
        emit log(QStringLiteral("web budget: loading the shipped `web` module %1").arg(name));
        backend->loadCoreModule(name);
    }
    // One wait for the lot: they load in parallel and the tiles appear as their
    // pages do.
    QElapsedTimer since;
    since.start();
    while (since.elapsed() < kPageBudgetMs && tiledWebApps().isEmpty())
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
}

bool ShellWebBudgetDriver::hasWork() const
{
    if (!tiledWebApps().isEmpty()) return true;
    return !m_host->backend()->shippedModuleNames().isEmpty();
}

void ShellWebBudgetDriver::weigh(const QString& occasion)
{
    const LiveRuntimeBudget& budget = MobileWebContainerBackend::instance()->budget();
    const qint64 ceiling = budget.appCeilingBytes();
    emit log(QStringLiteral("WEB BUDGET: %1 -- %2 live runtime(s), app %3, budget %4 "
                            "(%5 x %6), ceiling %7")
                 .arg(occasion,
                      QString::number(budget.live().size()),
                      reportedAs(basecamp::web::appResidentBytes(),
                                 QStringLiteral("not reported by this platform")),
                      megabytes(budget.budgetBytes()),
                      QString::number(budget.maxLiveRuntimes()),
                      megabytes(budget.runtimeFootprintBytes()),
                      ceiling > 0 ? megabytes(ceiling) : QStringLiteral("none")));
}

bool ShellWebBudgetDriver::openAndWeigh(const QString& app)
{
    auto* web = MobileWebContainerBackend::instance();

    m_host->setCurrentSectionIndex(ShellSection::Workspace);
    settle(200);
    QQuickItem* tile = waitFor(QStringLiteral("sidebar.app.%1").arg(app), 10000);
    if (!tile) {
        dumpNames(QStringLiteral("no sidebar tile for the web app '%1'").arg(app));
        return false;
    }
    scrollIntoView(tile);
    if (!tap(tile)) return false;

    QElapsedTimer sincePress;
    sincePress.start();
    while (sincePress.elapsed() < kPageBudgetMs) {
        WebAppSurface* surface = m_host->webSurface(app);
        if (surface && surface->onScreen() && web->frontmostModule() == app) break;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }
    if (web->frontmostModule() != app) {
        emit log(QStringLiteral("WRONG: pressing %1's tile did not bring its page up "
                                "('%2' is in front)").arg(app, web->frontmostModule()));
        return false;
    }
    // THE PAGE IS UP AND STILL GROWING. Weighed after it settles, because the
    // number this pass exists to report is what the app holds while the user
    // looks at the app -- not the peak of a load nobody sees.
    settle(kSettleMs);
    weigh(QStringLiteral("%1 open").arg(app));
    return true;
}

void ShellWebBudgetDriver::run()
{
    auto* web = MobileWebContainerBackend::instance();

    const qint64 deviceMemory = basecamp::web::deviceMemoryBytes();
    emit log(QStringLiteral("web budget: this device reports %1 of memory; the policy "
                            "affords %2 runtime(s) and the container is running with %3")
                 .arg(reportedAs(deviceMemory, QStringLiteral("no memory figure")),
                      QString::number(LiveRuntimeBudget::runtimesForDeviceMemory(deviceMemory)),
                      QString::number(web->budget().maxLiveRuntimes())));
    weigh(QStringLiteral("before any web app is open"));

    loadShippedWebModules();
    const QStringList apps = tiledWebApps();
    if (apps.isEmpty()) {
        emit log(QStringLiteral("web budget: no `web` module in this build has a UI page"));
        return;
    }

    // ONE AT A TIME, in the order the sidebar carries them, and the count of
    // live runtimes in each line is what makes the sequence readable: a budget
    // of one answers "1" three times (and the lines between show the evictions)
    // while a budget of three counts up.
    for (const QString& app : apps) {
        if (!openAndWeigh(app)) return;
    }

    // ...AND WHAT A MEMORY WARNING COSTS. The same handler the platform's own
    // notification reaches; what a device run reads off this is how many pages
    // were held when the OS asked and how much the app gave back.
    const qint64 before = basecamp::web::appResidentBytes();
    const QStringList shed = web->memoryWarning();
    settle(kSettleMs);
    const qint64 after = basecamp::web::appResidentBytes();
    emit log(QStringLiteral("WEB BUDGET: a memory warning shed %1 page(s) [%2]; the app "
                            "went from %3 to %4")
                 .arg(QString::number(shed.size()),
                      shed.isEmpty() ? QStringLiteral("nothing to give up")
                                     : shed.join(QStringLiteral(", ")),
                      reportedAs(before, QStringLiteral("?")),
                      reportedAs(after, QStringLiteral("?"))));
    emit log(QStringLiteral("SHELL WEIGHED ITS WEB RUNTIMES"));
}
