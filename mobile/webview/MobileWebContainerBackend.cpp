#include "webview/MobileWebContainerBackend.h"

#include "webview/AppMemory.h"

#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QMetaObject>
#include <QThread>
#include <QTimer>

#include <utility>

namespace basecamp::web {
namespace {

// WHAT THE CEILING IS A CEILING ON, in words, so a device log says which of the
// two frames its numbers are in (#244). AppMemory::budgetWeighedBytes() answers
// this process's footprint on iOS and how much of the DEVICE is in use on
// Android, and the two are nowhere near each other.
QString weighedFigureName()
{
#if defined(Q_OS_LINUX) || defined(Q_OS_ANDROID)
    return QStringLiteral("this device's memory in use");
#else
    return QStringLiteral("app memory");
#endif
}

} // namespace


namespace {

// A directory holding the runtime's glue script IS the runtime; anything else
// named as one is a misconfiguration, and saying so beats a page that comes up
// and then cannot find what it was promised.
bool holdsQmlRuntime(const QString& dir)
{
    return !dir.isEmpty()
           && QFileInfo(dir).isDir()
           && QFileInfo(QDir(dir).filePath(QStringLiteral("logos_qml_runtime.js"))).isFile();
}

} // namespace

QString bundledQmlRuntimeDir(const QString& platformDir)
{
    const QString fromEnv = qEnvironmentVariable("LOGOS_QML_RUNTIME_DIR");
    if (!fromEnv.isEmpty()) {
        if (holdsQmlRuntime(fromEnv)) return QDir(fromEnv).absolutePath();
        qWarning() << "LOGOS_QML_RUNTIME_DIR points at" << fromEnv
                   << "which holds no logos_qml_runtime.js; ignoring it";
    }
    return holdsQmlRuntime(platformDir) ? QDir(platformDir).absolutePath() : QString();
}

MobileWebContainerBackend* MobileWebContainerBackend::instance()
{
    static MobileWebContainerBackend backend;
    return &backend;
}

void MobileWebContainerBackend::armPollTimer()
{
    if (m_pollTimer) return;
    // A long poll the host never completes is a fetch the page waits on
    // forever; the page re-arms the moment one settles, so an idle module costs
    // one request every kPollParkMs and nothing in between.
    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(MobileWebBridge::kPollParkMs);
    connect(m_pollTimer, &QTimer::timeout, this, [this] {
        for (MobileWebModuleView* view : m_views) view->expireWaits();
        // AND THE APP IS WEIGHED ON THE SAME WAKE-UP (#153). A page that grows
        // after it was shown is noticed by nothing else in this class -- the
        // container is asleep between two taps -- and this timer is already
        // running whenever there are pages at all, so the alternative was a
        // second timer to do something that costs microseconds.
        //
        // budgetWeighedBytes() RATHER THAN appResidentBytes() (#244): on
        // Android the pages are in a renderer process this one cannot see, and
        // weighing this process was measured moving the WRONG WAY across an
        // eviction. See AppMemory.h.
        observeMemory(budgetWeighedBytes());
    });
    m_pollTimer->start();
}

MobileWebModuleView* MobileWebContainerBackend::createView(
    const LogosCore::WebModuleViewRequest& request, const QString& runtimeDir)
{
    const QString name = QString::fromStdString(request.moduleName);

    // THE MODULE'S OWN ORIGIN, not the container's. What a `web` variant keeps
    // across a page reload it keeps in IndexedDB, which is keyed by origin, so
    // one origin for every module is one store for every module. See
    // WebOrigin::forModule.
    auto* view = new MobileWebModuleView(request, runtimeDir, m_platform, m_shimInDocument,
                                        m_origin.forModule(name));
    if (!view->startupError().isEmpty()) {
        delete view;
        return nullptr;
    }

    // THE PAGE'S CONSOLE, ANNOUNCED. Wired before the registry entry and before
    // anyone is told the view exists, so a host that wants to wait for a line
    // the view will print cannot miss it: the webview's first request has not
    // been served yet -- both platforms load asynchronously.
    // QUEUED, for the reason WebPageProbe's sink is: Android calls
    // `shouldInterceptRequest` on a Chromium background thread, so a page's
    // console line arrives on it -- and a shell connected to this signal appends
    // to a widget. Qt Widgets from a second thread is an immediate SIGSEGV.
    view->setOnPageLog([this, name](const QString& level, const QString& message) {
        QMetaObject::invokeMethod(this, [this, name, level, message] {
            emit pageLog(name, level, message);
        }, Qt::QueuedConnection);
    });

    // WHERE THE HOST ALREADY SAID PAGES GO. A module is loaded whenever the core
    // discovers or the user installs one, which is not an order the Shell picks
    // -- so a page opened after the workspace was measured has to be told, and a
    // page opened before it is told by setContentRect().
    if (!m_contentRect.isEmpty()) view->setGeometry(m_contentRect);

    m_views.insert(name, view);
    // The view is the CONTAINER's, so the container's destruction is what takes
    // the entry out: anything else would leave the registry naming a page that
    // no longer exists, and the shell would mount a dangling handle.
    view->setOnDestroyed([this, name] { forget(name); });
    armPollTimer();
    emit viewOpened(name, view->nativeHandle());
    return view;
}

void MobileWebContainerBackend::forget(const QString& moduleName)
{
    if (!m_views.remove(moduleName)) return;
    m_budget.forget(moduleName);
    // A page that no longer exists is not in front of anything.
    if (m_frontmost == moduleName) m_frontmost.clear();
    // AND THE CONSOLE IS TOLD WHAT IS LEFT (#151). show() states the budget and
    // what is held against it; nothing stated it when a page went away, so the
    // last word about an unloaded module stayed `web_counter is visible; 1 live
    // runtime(s), 290 MB of 290 MB` -- true when it was printed, never taken
    // back, and read beside the Shell's `app web_counter is not mounted` as a
    // container that had lost track of its own pages. The books were right; the
    // account of them stopped at the last thing that went well.
    //
    // It is also the second half of the pair slice 28 asks for: the memory a
    // shell holds with a module's UI live, and what it returns to when that UI
    // is given up.
    qInfo().noquote() << QStringLiteral("Web container: %1's page is gone; %2")
                             .arg(moduleName, budgetLine());
    qInfo().noquote() << appMemoryLine(QStringLiteral("with %1's page gone").arg(moduleName));
    emit viewClosed(moduleName);
}

void MobileWebContainerBackend::install(const QString& runtimeDir,
                                        PlatformPageFactory platform,
                                        const LiveRuntimeBudget& budget,
                                        bool shimInDocument,
                                        WebOrigin origin)
{
    m_platform = std::move(platform);
    m_budget = budget;
    // A fresh install is a fresh host: the window it measured is not this one's.
    m_contentRect = QRect();
    m_shimInDocument = shimInDocument;
    m_origin = std::move(origin);

    if (runtimeDir.isEmpty()) {
        qWarning() << "Web container: this build ships no bundled QML runtime. A `web` "
                      "variant whose loader asks for one will load its page and fail "
                      "there.";
    } else {
        qInfo() << "Web container: bundled QML runtime at" << runtimeDir;
    }
    // THE WHOLE POLICY IN ONE LINE, because a device run is read against it:
    // how many pages this device affords, what they cost, and the figure they
    // are shed above. Before #153 the first number was 1 everywhere and the
    // last did not exist; before #244 the last could not be reached on Android.
    //
    // ONE RENDERER AND A DOCUMENT EACH AFTER IT, not a renderer each -- see
    // LiveRuntimeBudget::kAdditionalRuntimeBytes.
    qInfo().noquote()
        << QStringLiteral("Web container: live-runtime budget %1 (%2 runtime%3: one %4 "
                          "renderer + %5 each after it); %6")
               .arg(megabytes(m_budget.budgetBytes()),
                    QString::number(m_budget.maxLiveRuntimes()),
                    m_budget.maxLiveRuntimes() == 1 ? QString() : QStringLiteral("s"),
                    megabytes(m_budget.runtimeFootprintBytes()),
                    megabytes(LiveRuntimeBudget::kAdditionalRuntimeBytes),
                    m_budget.appCeilingBytes() > 0
                        ? QStringLiteral("a page is shed above %1 of %2")
                              .arg(megabytes(m_budget.appCeilingBytes()),
                                   weighedFigureName())
                        : QStringLiteral("no ceiling was stated, so nothing is shed on "
                                         "weight alone"));

    // THE OS'S OWN SIGNAL, subscribed to once. Installed here rather than by
    // each host because the container is the thing that can DO something about
    // it, and a host that forgot to subscribe would be a phone with no answer
    // to the one warning it gets before it is killed.
    if (!m_watchingPressure) {
        m_watchingPressure = watchAppMemoryPressure([this] { memoryWarning(); });
        if (!m_watchingPressure) {
            qInfo().noquote()
                << QStringLiteral("Web container: this platform sends no memory warning; "
                                  "growth is noticed by weighing the app instead");
        }
    }

    // The thread install() was called on — see the header: a webview may only be
    // built on the platform's UI thread, whatever thread the core loads from.
    QThread* const uiThread = QThread::currentThread();
    LogosCore::setWebModuleViewFactory(
        [this, runtimeDir, uiThread](const LogosCore::WebModuleViewRequest& request)
            -> std::unique_ptr<LogosCore::WebModuleView> {
            MobileWebModuleView* view = nullptr;
            if (QThread::currentThread() == uiThread) {
                view = createView(request, runtimeDir);
            } else {
                // BLOCKING, and it is the load path that makes it safe: the core
                // is asking this thread for a view and will not proceed without
                // one, so there is nothing for the UI thread to be waiting on in
                // return.
                QMetaObject::invokeMethod(
                    this, [this, &request, &runtimeDir, &view] {
                        view = createView(request, runtimeDir);
                    },
                    Qt::BlockingQueuedConnection);
            }
            return std::unique_ptr<LogosCore::WebModuleView>(view);
        });
}

void* MobileWebContainerBackend::nativeHandleFor(const QString& moduleName) const
{
    MobileWebModuleView* view = m_views.value(moduleName, nullptr);
    return view ? view->nativeHandle() : nullptr;
}

bool MobileWebContainerBackend::hasView(const QString& moduleName) const
{
    return m_views.contains(moduleName);
}

bool MobileWebContainerBackend::hasUiPage(const QString& moduleName) const
{
    MobileWebModuleView* view = m_views.value(moduleName, nullptr);
    return view && view->hasUi();
}

bool MobileWebContainerBackend::pageServesUi(const QString& moduleName) const
{
    MobileWebModuleView* view = m_views.value(moduleName, nullptr);
    return view && view->servesUi();
}

void MobileWebContainerBackend::observeFramesFrom(const QString& moduleName,
                                                 std::function<void(const QString&)> sink)
{
    if (MobileWebModuleView* view = m_views.value(moduleName, nullptr))
        view->observeFrames(std::move(sink));
}

bool MobileWebContainerBackend::sendFrameTo(const QString& moduleName, const QString& frame)
{
    MobileWebModuleView* view = m_views.value(moduleName, nullptr);
    return view && view->sendFrame(frame);
}

bool MobileWebContainerBackend::runJavaScriptIn(const QString& moduleName,
                                                const QString& script)
{
    MobileWebModuleView* view = m_views.value(moduleName, nullptr);
    return view && view->runJavaScript(script);
}

QStringList MobileWebContainerBackend::loadedModules() const
{
    QStringList names = m_views.keys();
    names.sort();
    return names;
}

QString MobileWebContainerBackend::appMemoryLine(const QString& occasion)
{
    const qint64 bytes = appResidentBytes();
    return bytes < 0
        ? QStringLiteral("Web container: app memory %1: this platform does not say")
              .arg(occasion)
        : QStringLiteral("Web container: app memory %1: %2").arg(occasion, megabytes(bytes));
}

QString MobileWebContainerBackend::budgetLine() const
{
    return QStringLiteral("%1 live runtime(s), %2 of %3")
        .arg(QString::number(m_budget.live().size()),
             megabytes(m_budget.projectedBytes()),
             megabytes(m_budget.budgetBytes()));
}

QStringList MobileWebContainerBackend::show(const QString& moduleName)
{
    // THE BOOKS MAY ONLY NAME A MODULE THIS CONTAINER HAS A PAGE FOR (#151).
    //
    // show() is an announcement from the shell -- "the user is looking at this
    // one" -- and an announcement about a module with no page is stale by
    // construction: the page went away between the shell deciding and the
    // container hearing. Written into the books anyway it cost twice. The
    // budget is ONE on a phone, so a module with no page took the live
    // runtime's slot and named the module that really was up for eviction: a
    // tab press on a dead app unloaded the app the user was looking at. And the
    // line below then said the dead one was visible and holding the runtime,
    // which is the contradiction #151 was reported as -- `web_counter is
    // visible; 1 live runtime(s)` from here, `app web_counter is not mounted`
    // from the Shell, both about a module whose page had been destroyed.
    //
    // Nothing is touched on the way out: the page that IS in front stays there,
    // because nothing on screen changed either.
    if (!m_views.contains(moduleName)) {
        qInfo().noquote()
            << QStringLiteral("Web container: %1 has no page; nothing to show and nothing "
                              "spent on it (%2)")
                   .arg(moduleName, budgetLine());
        return {};
    }

    const QStringList evicted = m_budget.show(moduleName);

    // THE SURFACE FOLLOWS THE BOOKS. A page is mounted behind the host's own
    // view, so a module the budget considers visible is still invisible until
    // its page is brought forward -- and the one that was in front has to go
    // back, or it would cover the new one whatever the budget thinks.
    for (auto it = m_views.constBegin(); it != m_views.constEnd(); ++it)
        it.value()->setFrontmost(it.key() == moduleName);
    m_frontmost = moduleName;

    qInfo().noquote() << QStringLiteral("Web container: %1 is visible; %2")
                             .arg(moduleName, budgetLine());
    qInfo().noquote() << appMemoryLine(QStringLiteral("with %1 visible").arg(moduleName));

    // WHY, NOT JUST THAT (#244). show() trims to the ALLOWANCE, which is the
    // device's count until a measurement or an OS warning tightens it -- so an
    // eviction here can have been caused by something that happened minutes
    // ago and several log lines up. A run that has just put the ceiling where
    // this device crosses it has to be able to read that off the line.
    applyEvictions(evicted,
                   m_budget.liveAllowance() < m_budget.maxLiveRuntimes()
                       ? QStringLiteral("over the allowance a measurement or a warning left")
                       : QStringLiteral("over budget"));

    // ...AND WHAT THE APP WEIGHS, which is the half #153 is about: the count
    // says how many pages MAY live and the measurement says whether this device
    // is still standing up under the ones that do. A page shown is the moment
    // the app is at its biggest, so it is the honest moment to read.
    const QStringList tooHeavy = observeMemory(budgetWeighedBytes());

    // ...AND THE MODULE THE USER CHOSE GETS ITS UI BACK. Last, so that the page
    // coming up is the only live runtime at the moment it starts: a restore
    // before the eviction would put two of them in the same webview process for
    // the length of a load, which is the thing the budget exists to prevent.
    MobileWebModuleView* shown = m_views.value(moduleName, nullptr);
    if (shown && !shown->hasUi() && shown->restoreUi()) {
        shown->setFrontmost(true);
        qInfo().noquote()
            << QStringLiteral("Web container: %1 was in the background; its UI page is "
                              "coming back").arg(moduleName);
    }
    return evicted + tooHeavy;
}

void MobileWebContainerBackend::applyEvictions(const QStringList& evicted,
                                               const QString& because)
{
    for (const QString& name : evicted) {
        MobileWebModuleView* view = m_views.value(name, nullptr);
        // THE MODULE STAYS, THE RUNTIME GOES. A variant with a headless entry
        // document is swapped onto it: same view, same bridge, same channel,
        // and the core is not told because from its side nothing happened.
        if (view && view->evictUi()) {
            qInfo().noquote()
                << QStringLiteral("Web container: %1 -- %2 gives up its UI page "
                                  "and keeps its Wasm host (%3 reclaimed); %4")
                       .arg(because, name, megabytes(m_budget.runtimeFootprintBytes()),
                            budgetLine());
            emit uiEvicted(name);
            continue;
        }
        qInfo().noquote()
            << QStringLiteral("Web container: %1 -- %2 gives up its UI page "
                              "(%3 reclaimed); its package ships no headless document, "
                              "so it has to be unloaded; %4")
                   .arg(because, name, megabytes(m_budget.runtimeFootprintBytes()),
                        budgetLine());
        emit uiEvictionRequired(name);
    }
}

QStringList MobileWebContainerBackend::observeMemory(qint64 weighedBytes)
{
    const int allowanceBefore = m_budget.liveAllowance();
    const QStringList evicted = m_budget.observe(weighedBytes);
    const int allowanceNow = m_budget.liveAllowance();

    // THE CROSSING IS ANNOUNCED EVEN WHEN NOTHING WAS EVICTED (#244). An
    // observation over the ceiling with ONE page live tightens the allowance
    // and takes nothing away -- one page is the floor -- and the eviction it
    // causes then happens at the next show(), several lines later. Logged only
    // here, the moment the ceiling was crossed was invisible, which is exactly
    // the failure this issue is about: a ceiling nobody ever saw trip reads the
    // same as one that never needed to.
    if (allowanceNow == allowanceBefore && evicted.isEmpty()) return {};

    // THE FIGURE, NOT "THE APP". What is weighed here is this process on iOS
    // and how much of the DEVICE is in use on Android, and a log line that
    // called the second one "the app" would be claiming the app had grown by
    // whatever some other process just allocated.
    qInfo().noquote()
        << QStringLiteral("Web container: the figure this platform weighs is %1 against a "
                          "%2 ceiling -- the allowance goes %3 -> %4 page(s), %5 to give "
                          "up now")
               .arg(megabytes(weighedBytes), megabytes(m_budget.appCeilingBytes()))
               .arg(allowanceBefore)
               .arg(allowanceNow)
               .arg(evicted.size());
    if (evicted.isEmpty()) return {};
    applyEvictions(evicted, QStringLiteral("over the app's memory ceiling"));
    return evicted;
}

QStringList MobileWebContainerBackend::memoryWarning()
{
    const QStringList evicted = m_budget.shedUnderPressure();
    // STATED WHETHER OR NOT THERE WAS ANYTHING TO DO. A warning the container
    // answered with nothing is the interesting case in a device log: it says
    // the pages were not what the OS was complaining about.
    qInfo().noquote()
        << QStringLiteral("Web container: the OS sent a memory warning (%1); %2")
               .arg(appMemoryLine(QStringLiteral("at the warning")), budgetLine());
    applyEvictions(evicted, QStringLiteral("the OS asked for memory back"));
    return evicted;
}

void MobileWebContainerBackend::setContentRect(const QRect& windowRect)
{
    if (m_contentRect == windowRect) return;
    m_contentRect = windowRect;
    for (auto it = m_views.constBegin(); it != m_views.constEnd(); ++it)
        it.value()->setGeometry(m_contentRect);
    qInfo().noquote()
        << (m_contentRect.isEmpty()
                ? QStringLiteral("Web container: a page may use the whole window again")
                : QStringLiteral("Web container: a page lives at %1,%2 %3x%4 -- the host's "
                                 "own chrome is outside it")
                      .arg(m_contentRect.x()).arg(m_contentRect.y())
                      .arg(m_contentRect.width()).arg(m_contentRect.height()));
}

void MobileWebContainerBackend::hideAll()
{
    for (auto it = m_views.constBegin(); it != m_views.constEnd(); ++it)
        it.value()->setFrontmost(false);
    m_frontmost.clear();
    qInfo().noquote()
        << QStringLiteral("Web container: no module is visible; %1 live runtime(s) held")
               .arg(m_budget.live().size());
}

} // namespace basecamp::web
