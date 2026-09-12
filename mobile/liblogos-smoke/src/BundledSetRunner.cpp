#include "BundledSetRunner.h"

#include "BundledSetCoreRuntime.h"

#include <logos_api.h>
#include <logos_api_client.h>
#include <logos_call_error.h>

#include <QVariant>
#include <QVariantMap>

#if defined(Q_OS_ANDROID)
#  include "PlatformConsole.h"

#  include <spdlog/sinks/android_sink.h>
#  include <spdlog/spdlog.h>
#  include <algorithm>
#  include <memory>
#endif

namespace {

#if defined(Q_OS_ANDROID)
// Put liblogos's own log where a phone can show it.
//
// The core reports the numbers this slice exists to produce -- the per-image
// dlopen time among them -- through spdlog, and liblogos's sink is stderr.
// On Android stderr goes nowhere at all, so those lines simply do not exist on
// the device. Adding a logcat sink to every registered channel is the whole
// fix, and it works because there is exactly ONE spdlog registry in the
// process: libspdlog.so is a shared library here and both the app and
// liblogos_core.so link it. Under the tag the host's own lines carry, so
// `adb logcat -s logos-smoke` shows both interleaved (PlatformConsole.h).
//
// Called before the set is loaded, so each load's own log is covered.
void routeCoreLogsToLogcat()
{
    static const auto sink =
        std::make_shared<spdlog::sinks::android_sink_mt>(basecamp::mobile::kAndroidLogTag);
    const auto attach = [](const std::shared_ptr<spdlog::logger>& lg) {
        if (!lg) return;
        auto& sinks = lg->sinks();
        if (std::find(sinks.begin(), sinks.end(), sink) == sinks.end())
            sinks.push_back(sink);
    };
    spdlog::apply_all(attach);
    attach(spdlog::default_logger());
}
#endif

} // namespace

BundledSetRunner::BundledSetRunner(BundledSetCoreRuntime* core, QObject* parent)
    : QObject(parent)
    , m_core(core)
{
}

BundledSetRunner::~BundledSetRunner() = default;

bool BundledSetRunner::hasViewModule() const
{
    for (const QVariant& row : m_core->bundledSet())
        if (row.toMap().value("type").toString() == QLatin1String("ui_qml"))
            return true;
    return false;
}

bool BundledSetRunner::run()
{
#if defined(Q_OS_ANDROID)
    routeCoreLogsToLogcat();
#endif

    // NOTE for Android, where nothing appears here: a module's `lp_*` are bound
    // by a DT_NEEDED on liblogos_protocol.so that the APK build records
    // (nix/android-apps.nix). It is not something the app can arrange at
    // runtime -- bionic never puts an app's own libraries in the linker
    // namespace's global group, and re-opening the protocol image with
    // RTLD_GLOBAL does not promote it. Measured, both ways, on an SM-G990B.

    bool allOk = true;
    for (const QVariant& value : m_core->bundledSet()) {
        const QVariantMap row = value.toMap();
        const QString name = row.value("name").toString();
        if (row.value("type").toString() == QLatin1String("ui_qml")) {
            emit log(QStringLiteral("  %1: view module, loaded by the host").arg(name));
            continue;
        }
        if (!m_core->loadModule(name))
            allOk = false;
    }
    const QStringList loaded = m_core->loadedModules();
    emit log(QStringLiteral("bundled set loaded: %1")
                 .arg(loaded.isEmpty() ? QStringLiteral("(none)")
                                       : loaded.join(QStringLiteral(", "))));
    return allOk;
}

bool BundledSetRunner::callCounter()
{
    const QString counter = QStringLiteral("bare_counter");
    if (!m_core->loadedModules().contains(counter)) {
        emit log(QStringLiteral("no %1 in the set; nothing to call").arg(counter));
        return true;
    }

    // A LogosAPI of the host's own, exactly as any other consumer would:
    // nothing about a module being in-process changes the caller's side of the
    // seam.
    LogosAPI api(QStringLiteral("mobile_host"));
    LogosAPIClient* client = api.getClient(counter);
    if (!client) {
        emit log(QStringLiteral("no client for %1").arg(counter));
        return false;
    }

    logos::CallError err;
    const QVariant result = client->invokeRemoteMethod(
        counter, QStringLiteral("add"), QVariantList{ QVariant(1), QVariant(2) },
        Timeout(), &err);
    if (!err.ok()) {
        emit log(QStringLiteral("add(1, 2) failed: %1 (%2)")
                     .arg(QString::fromStdString(err.message))
                     .arg(QString::fromStdString(err.code)));
        return false;
    }

    bool converted = false;
    const qlonglong value = result.toLongLong(&converted);
    emit log(QStringLiteral("add(1, 2) = %1").arg(result.toString()));
    if (!converted || value != 3) {
        emit log(QStringLiteral("WRONG: add(1, 2) must be 3"));
        return false;
    }
    return true;
}
