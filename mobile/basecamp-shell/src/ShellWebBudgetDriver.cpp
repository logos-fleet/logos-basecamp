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
// Every reading in AppMemory.h answers -1 somewhere -- deviceAvailableBytes() on
// iOS, all of them off the two phones -- and every line this pass prints has to
// stay readable there rather than claiming it weighed -0 MB.
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

// A TILE IS NOT A PAGE (logos-workspace#230). `wallet_ui` ships its `web` assets
// and declares dependencies -- eth_rpc_module, keystore_module, uniswap_module,
// token_list_module -- that an image whose Bundled set does not carry them
// cannot satisfy, so the core refuses the load and the app keeps a launcher tile
// with nothing behind it.
//
// PRESSING IT ENDED THE PASS, and that is why this filter exists rather than a
// kinder failure inside openAndWeigh(): the press spent the whole 60 s page
// budget waiting for a page that was never coming and then returned false, so
// the memory-warning measurement at the end of run() -- the half #153 added and
// #230 needs -- was never reached on ANY Android run (Xiaomi 25028RN03Y and
// Samsung SM-G990B, 2026-09-17). Named and skipped, so the numbers that CAN be
// taken are taken.
QStringList ShellWebBudgetDriver::openableWebApps()
{
    auto* web = MobileWebContainerBackend::instance();
    QStringList openable;
    for (const QString& app : tiledWebApps()) {
        if (!web->hasView(app)) {
            emit log(QStringLiteral("web budget: skipping %1 -- it has a tile on this "
                                    "device but no `web` page behind it").arg(app));
            continue;
        }
        openable.append(app);
    }
    return openable;
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
    const QString unreported = QStringLiteral("not reported by this platform");
    const qint64 weighed = basecamp::web::budgetWeighedBytes();
    m_weighed.append(qMakePair(int(budget.live().size()), weighed));
    const QString appResident = reportedAs(basecamp::web::appResidentBytes(), unreported);
    // THE PAGE'S OWN COST IS ONLY IN THIS ONE, on Android (logos-workspace#230):
    // the app's figure is this process and the page lives in a Chromium renderer
    // that is not. A row of this pass that carried only the app's number
    // under-reported what a `web` runtime costs here by about 2.5x. See
    // AppMemory.h for why it is not the app's number and must not be read as one.
    const QString deviceFree = reportedAs(basecamp::web::deviceAvailableBytes(), unreported);
    // AND THE FIGURE THE BUDGET ACTUALLY READS, named as such (#244). It is one
    // of the two above -- the app's on iOS, the device's in-use figure on
    // Android -- and which one it is has to be legible from the log, because
    // the whole of #244 is that the wrong one was being read on one of the two
    // phones for a full landing without anything looking broken.
    emit log(QStringLiteral("WEB BUDGET: %1 -- %2 live runtime(s), app %3, device free %4, "
                            "weighed %5, budget %6 (one renderer + %7 x %8), ceiling %9")
                 .arg(occasion,
                      QString::number(budget.live().size()),
                      appResident,
                      deviceFree,
                      reportedAs(weighed, unreported),
                      megabytes(budget.budgetBytes()),
                      QString::number(budget.maxLiveRuntimes() - 1),
                      megabytes(LiveRuntimeBudget::kAdditionalRuntimeBytes),
                      ceiling > 0 ? megabytes(ceiling) : QStringLiteral("none")));
}

// HOW MUCH THE FIGURE IS ALLOWED TO WANDER BETWEEN TWO READINGS before a
// direction is claimed from it. On Android the figure is the whole device's, so
// every other process's allocations are in it; 32 MB is a tenth of what one
// renderer costs, and the drift measured across a settled pass on the venue's
// two Android devices was up to 62 MB on the 14.9 GB tablet -- which is why the
// only step this allowance guards is the one whose signal is 250 MB or more.
static constexpr qint64 kFigureNoiseBytes = 32LL * 1024 * 1024;

// ...AND HOW FAR A PAGE MUST MOVE IT before the figure counts as weighing the
// pages at all. Per platform, because the two frames are charged differently:
//
//   * Android/Linux: the device's book carries the whole renderer. The first
//     page moved it +127, +215, +272 and +302 MB across four runs on a Xiaomi
//     25028RN03Y and +1031 MB on a Lenovo TB520FU (2026-09-17). It reads under
//     the renderer's 290 MB on the phone because MemAvailable counts page cache
//     the kernel gives up in the same seconds, so a third of a renderer is the
//     floor rather than a whole one.
//   * iOS/macOS: this process is charged only PART of the WebContent process.
//     Measured on a physical iPad Air 4 (#153), the first page moved
//     appResidentBytes() 178 -> 221 MB -- 43 MB, a seventh of the Android
//     signal. A floor set to Android's would report WRONG on every iOS run for
//     a reading that is correct there, so the floor here is the noise figure:
//     what the check still catches is a figure that does not move at all.
qint64 pageMovesAtLeastBytes()
{
#if defined(Q_OS_LINUX) || defined(Q_OS_ANDROID)
    return LiveRuntimeBudget::kDeviceRuntimeBytes / 3;
#else
    return kFigureNoiseBytes;
#endif
}

void ShellWebBudgetDriver::reportWhetherTheFigureMoved(qint64 afterShedBytes, int pagesShed)
{
    if (m_weighed.size() < 2) {
        emit log(QStringLiteral("web budget: only %1 reading(s) -- nothing to say about "
                                "whether the figure moves with the pages")
                     .arg(m_weighed.size()));
        return;
    }

    const qint64 atRest = m_weighed.first().second;
    const qint64 atMost = m_weighed.last().second;
    if (atRest < 0 || atMost < 0) {
        emit log(QStringLiteral("WRONG: this platform reports no figure for the budget to "
                                "weigh, so its ceiling can never trip"));
        return;
    }

    // ONE: THE PAGES HAVE TO BE IN IT AT ALL, by whatever this platform's
    // pages are expected to move it -- see pageMovesAtLeastBytes().
    const qint64 movedUp = atMost - atRest;
    const qint64 floor = pageMovesAtLeastBytes();
    if (movedUp < floor) {
        emit log(QStringLiteral("WRONG: %1 live runtime(s) moved the weighed figure by only "
                                "%2 (from %3 to %4) -- a page costs about %5, so this figure "
                                "is blind to them")
                     .arg(QString::number(m_weighed.last().first), megabytes(movedUp),
                          megabytes(atRest), megabytes(atMost),
                          megabytes(LiveRuntimeBudget::kDeviceRuntimeBytes)));
    }

    // TWO: THE STEPS, REPORTED RATHER THAN ASSERTED PAST THE FIRST ONE.
    //
    // #244 asks for the figure to move monotonically with the page count, and
    // the FIRST step is the one that can carry that: a renderer starting is
    // ~290 MB and dwarfs anything else on the device. Past it there is nothing
    // to be monotonic about -- #230 weighed every page of a build into ONE
    // renderer, so pages two and three cost ~5 MB each, which is under the
    // noise floor of a figure the whole device is in. Measured on the venue's
    // Lenovo TB520FU (14.9 GB) on 2026-09-17: going from one live runtime to
    // two took the figure DOWN 62 MB while nothing had shed anything, because
    // some other process gave memory back in the same four seconds. Asserting
    // there would be asserting a physical claim this container's own
    // measurements contradict, so the steps are printed and the verdict is
    // carried by the two checks that are not noise-bound.
    QStringList steps;
    for (int i = 1; i < m_weighed.size(); ++i) {
        if (m_weighed.at(i).first <= m_weighed.at(i - 1).first) continue;
        const qint64 step = m_weighed.at(i).second - m_weighed.at(i - 1).second;
        const QString entry = QStringLiteral("%1->%2 runtime(s) %3%4")
                                  .arg(QString::number(m_weighed.at(i - 1).first),
                                       QString::number(m_weighed.at(i).first),
                                       step < 0 ? QString() : QStringLiteral("+"),
                                       megabytes(step));
        steps << entry;
        if (i != 1) continue;
        // THE FIRST STEP IS ASSERTED. It is a whole renderer; a figure that a
        // renderer starting does not move up is blind whatever else it does.
        if (step < floor)
            emit log(QStringLiteral("WRONG: the first live runtime moved the weighed figure "
                                    "by only %1; this platform's pages should move it at "
                                    "least %2")
                         .arg(megabytes(step), megabytes(floor)));
    }
    emit log(QStringLiteral("web budget: the weighed figure, page by page: %1 (the pages "
                            "share one renderer, so only the first step is a page's real "
                            "cost -- see #244)")
                 .arg(steps.isEmpty() ? QStringLiteral("(no step to report)")
                                      : steps.join(QStringLiteral(", "))));

    // THREE: AND SHEDDING HAS TO TAKE IT BACK DOWN. This is the discriminating
    // one. #153's reading passed both of the above on a Xiaomi 25028RN03Y and
    // failed here by going UP 3 MB on a shed of two pages -- a container that
    // trusted it would learn that evicting costs memory.
    if (pagesShed <= 0) {
        emit log(QStringLiteral("web budget: the warning shed nothing, so this run says "
                                "nothing about the figure's sign on an eviction"));
    } else if (afterShedBytes < 0) {
        emit log(QStringLiteral("WRONG: no figure after the shed"));
    } else if (afterShedBytes >= atMost - kFigureNoiseBytes) {
        emit log(QStringLiteral("WRONG: shedding %1 page(s) did not take the weighed figure "
                                "down -- %2 before, %3 after")
                     .arg(QString::number(pagesShed), megabytes(atMost),
                          megabytes(afterShedBytes)));
    } else {
        emit log(QStringLiteral("WEB BUDGET OK: shedding %1 page(s) took the weighed figure "
                                "from %2 to %3, and %4 live runtime(s) had moved it up %5 "
                                "from %6 at rest")
                     .arg(QString::number(pagesShed), megabytes(atMost),
                          megabytes(afterShedBytes),
                          QString::number(m_weighed.last().first), megabytes(movedUp),
                          megabytes(atRest)));
    }

#if defined(Q_OS_DARWIN)
    // A WRONG HERE ON DARWIN IS A KNOWN, RECORDED GAP and not a fresh
    // regression, which is worth one line: #244 fixed Android on the
    // instruction that iOS was already correct, and the first iOS run of this
    // check falsified that instruction -- on an iPad Air 13-inch (M2)
    // simulator, 2026-09-17, shedding two pages left phys_footprint 1 MB
    // HIGHER (151 -> 152 MB). The physical-device measurement that settles it
    // is logos-workspace#254; without this line a later cycle spends itself
    // rediscovering it.
    emit log(QStringLiteral("web budget: on this platform a WRONG above is the open "
                            "question in logos-workspace#254 -- #244 left the iOS reading "
                            "alone on the premise that phys_footprint moves with the pages, "
                            "and a simulator says it does not"));
#endif
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
    // WHAT THIS APP CAN SEE OF THE PAGES' RENDERER (#244). Evidence for the
    // design, printed once per run: if an app CAN weigh its own WebView
    // renderer in-process then the device-frame reading is the wrong answer and
    // the renderer's own figure is the right one. Compared against the host's
    // `adb shell ps -A -o PID,RSS,NAME`.
    emit log(QStringLiteral("web budget: process visibility -- %1")
                 .arg(basecamp::web::processVisibilityReport()));
    weigh(QStringLiteral("before any web app is open"));

    loadShippedWebModules();
    const QStringList apps = openableWebApps();
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
    const qint64 weighedAfter = basecamp::web::budgetWeighedBytes();
    emit log(QStringLiteral("WEB BUDGET: a memory warning shed %1 page(s) [%2]; the app "
                            "went from %3 to %4 and the weighed figure to %5")
                 .arg(QString::number(shed.size()),
                      shed.isEmpty() ? QStringLiteral("nothing to give up")
                                     : shed.join(QStringLiteral(", ")),
                      reportedAs(before, QStringLiteral("?")),
                      reportedAs(after, QStringLiteral("?")),
                      reportedAs(weighedAfter, QStringLiteral("?"))));
    reportWhetherTheFigureMoved(weighedAfter, shed.size());
    emit log(QStringLiteral("SHELL WEIGHED ITS WEB RUNTIMES"));
}
