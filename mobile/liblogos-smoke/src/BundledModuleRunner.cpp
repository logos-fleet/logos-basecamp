#include "BundledModuleRunner.h"

#include "BundledManifest.h"

#include <logos_api.h>
#include <logos_api_client.h>
#include <logos_call_error.h>
#include <logos_core.h>
#include <logos_mode.h>
#include <logos_protocol.h>

#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QVariant>

#include <dlfcn.h>

#if defined(Q_OS_ANDROID)
#  include <spdlog/sinks/android_sink.h>
#  include <spdlog/spdlog.h>
#  include <algorithm>
#  include <memory>
#endif

namespace {

#if defined(Q_OS_IOS)
// The bundle-relative path Xcode's "Embed Frameworks" phase writes, and the
// only directory an iOS app may carry a dylib in. The binary inside a flat
// framework is named after the bundle.
constexpr const char* kImageSuffix =
    "/Frameworks/bare_counter_bare.framework/bare_counter_bare";
#else
// androiddeployqt puts every QT_ANDROID_EXTRA_LIBS entry beside Qt's own
// libraries in the app's native library directory.
constexpr const char* kImageSuffix = "/libbare_counter_bare.so";
#endif

// The directory of the image that `lp_protocol_version` came out of.
//
// Asked of the DYNAMIC LOADER rather than derived from
// QCoreApplication::applicationDirPath(), because the two do not agree on
// Android: applicationDirPath() is a path under /data/data, and the native
// library directory -- the only place dlopen() may load from since API 29 --
// is somewhere under /data/app entirely. dladdr answers with the place the
// Logos code is ACTUALLY loaded from, which on both platforms is where the
// Bare module sits too: <App>.app on iOS (one static image), the native
// library directory on Android.
QString logosProtocolImagePath()
{
    Dl_info info{};
    if (dladdr(reinterpret_cast<const void*>(&lp_protocol_version), &info) == 0 || !info.dli_fname)
        return {};
    return QString::fromUtf8(info.dli_fname);
}

QString logosImageDir()
{
    const QString image = logosProtocolImagePath();
    if (image.isEmpty())
        return {};
    return QFileInfo(image).absolutePath();
}

#if defined(Q_OS_ANDROID)
// Put liblogos's own log where a phone can show it.
//
// The core reports the numbers this slice exists to produce -- the per-image
// dlopen time among them -- through spdlog, and liblogos's sink is stderr.
// On Android stderr goes nowhere at all, so those lines simply do not exist on
// the device. Adding a logcat sink to every registered channel is the whole
// fix, and it works because there is exactly ONE spdlog registry in the
// process: libspdlog.so is a shared library here and both the app and
// liblogos_core.so link it.
//
// Called before the module is loaded, so the load's own log is covered.
void routeCoreLogsToLogcat()
{
    static const auto sink = std::make_shared<spdlog::sinks::android_sink_mt>("logos-smoke");
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

BundledModuleRunner::BundledModuleRunner(QObject* parent)
    : QObject(parent)
{
}

BundledModuleRunner::~BundledModuleRunner() = default;

QString BundledModuleRunner::bundledImagePath()
{
    const QString dir = logosImageDir();
    if (dir.isEmpty())
        return {};
    return dir + QString::fromUtf8(kImageSuffix);
}

bool BundledModuleRunner::run()
{
    const QString imagePath = bundledImagePath();
    if (imagePath.isEmpty()) {
        emit log(QStringLiteral("bundled module: could not locate the Logos image directory"));
        return false;
    }
    emit log(QStringLiteral("bundled image: %1").arg(imagePath));
    if (!QFileInfo::exists(imagePath)) {
        emit log(QStringLiteral("bundled module: no image at that path (embed step did not run?)"));
        return false;
    }

#if defined(Q_OS_ANDROID)
    routeCoreLogsToLogcat();
#endif

    // NOTE for Android, where nothing appears here: the module's `lp_*` are
    // bound by a DT_NEEDED on liblogos_protocol.so that the APK build records
    // (nix/liblogos-smoke-android.nix). It is not something the app can arrange
    // at runtime -- bionic never puts an app's own libraries in the linker
    // namespace's global group, and re-opening the protocol image with
    // RTLD_GLOBAL does not promote it. Measured, both ways, on an SM-G990B.

    // An ASSERTION, not a switch: it says every module in this process must be
    // a Bare module, so a Qt plugin quietly finding its way in is a load error
    // rather than a second container starting a subprocess on a phone -- which
    // is a thing neither platform allows at all.
    logos_core_set_container_policy("inproc");

    char* registered = logos_core_add_bare_module(kBundledModuleManifest,
                                                  imagePath.toUtf8().constData());
    if (!registered) {
        emit log(QStringLiteral("bundled module: the core refused to register it"));
        return false;
    }
    m_moduleName = QString::fromUtf8(registered);
    delete[] registered;
    emit log(QStringLiteral("registered: %1").arg(m_moduleName));

    // The dlopen itself is timed and logged INSIDE the Native container
    // (InProcContainer::launch), which is the only place that can measure it
    // alone. This number is the whole load: registration checks, the container
    // handshake, publication on the in-process transport.
    QElapsedTimer t;
    t.start();
    if (logos_core_load_module(m_moduleName.toUtf8().constData(), LOGOS_LOAD_REQUIRED_DEPS) != 1) {
        // The core reports WHY through spdlog, and spdlog writes to stderr --
        // which on Android goes nowhere. So ask the loader the same question
        // directly and put its answer on screen: for a Bare module the
        // overwhelmingly likely failure is an `lp_*` that did not resolve, and
        // dlerror() names it.
        ::dlerror();
        void* probe = dlopen(imagePath.toUtf8().constData(), RTLD_NOW | RTLD_LOCAL);
        const char* why = ::dlerror();
        if (probe) {
            dlclose(probe);
            emit log(QStringLiteral("bundled module: load failed, but the image opens "
                                    "cleanly -- the refusal is the core's, not dyld's"));
        } else {
            emit log(QStringLiteral("bundled module: load failed -- %1")
                         .arg(why ? QString::fromUtf8(why) : QStringLiteral("dlopen gave no reason")));
        }
        return false;
    }
    m_loaded = true;
    emit log(QStringLiteral("loaded %1 in %2 ms (Native container)").arg(m_moduleName).arg(t.elapsed()));

    // The call. A LogosAPI of the host's own, exactly as any other consumer
    // would: nothing about the module being in-process changes the caller's
    // side of the seam.
    LogosAPI api(QStringLiteral("mobile_host"));
    LogosAPIClient* client = api.getClient(m_moduleName);
    if (!client) {
        emit log(QStringLiteral("bundled module: no client for %1").arg(m_moduleName));
        return false;
    }

    logos::CallError err;
    const QVariant result = client->invokeRemoteMethod(
        m_moduleName, QStringLiteral("add"), QVariantList{ QVariant(1), QVariant(2) },
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
    emit log(QStringLiteral("BUNDLED BARE MODULE OK"));
    return true;
}
