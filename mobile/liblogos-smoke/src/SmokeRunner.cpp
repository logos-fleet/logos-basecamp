#include "SmokeRunner.h"

#include <logos_core.h>
#include <logos_mode.h>
#include <logos_protocol.h>

#include <QDir>
#include <QElapsedTimer>
#include <QStandardPaths>

SmokeRunner::SmokeRunner(QObject* parent)
    : QObject(parent)
{
}

qint64 SmokeRunner::run(int argc, char* argv[])
{
    // In-process transport for every LogosAPI in this image: no QtRemoteObjects
    // registry, no local sockets. A phone has no second process to talk to.
    LogosModeConfig::setMode(LogosMode::Local);
    // ...and again through the C ABI, because LogosModeConfig's storage is an
    // inline static -- ONE COPY PER IMAGE. Where liblogos_protocol is a shared
    // library (Android) the call above sets only the app's copy and every
    // LogosAPI inside the protocol image stays in Remote mode. Measured on an
    // SM-G990B: right after "LogosModeConfig: Mode set to Local" the protocol
    // image logged "RemoteTransportHost: Created registry host with URL:
    // local:logos_..." and dialled QtRO sockets in-process. lp_set_mode runs
    // inside the protocol image, so it sets the copy that matters. On iOS
    // there is one static image and the two calls are the same store.
    lp_set_mode("local");

    // Everything the core writes stays under the app sandbox. AppDataLocation
    // is Library/Application Support/<org>/<app> on iOS and
    // /data/data/<pkg>/files on Android; neither is reachable from outside.
    m_baseDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/logos";
    const QString modulesDir = m_baseDir + "/modules";
    const QString persistDir = m_baseDir + "/persistence";
    QDir().mkpath(modulesDir);
    QDir().mkpath(persistDir);
    emit log(QStringLiteral("base dir: %1").arg(m_baseDir));
    emit log(QStringLiteral("protocol: %1 (abi major %2)")
                 .arg(QString::fromUtf8(lp_protocol_version()))
                 .arg(lp_protocol_abi_major()));

    QElapsedTimer t;
    t.start();
    logos_core_init(argc, argv);
    logos_core_set_persistence_base_path(persistDir.toUtf8().constData());
    logos_core_add_modules_dir(modulesDir.toUtf8().constData());
    logos_core_start();
    const qint64 ms = t.elapsed();
    m_started = true;
    emit log(QStringLiteral("logos_core_start: %1 ms").arg(ms));

    char* info = logos_core_get_modules_info();
    emit log(QStringLiteral("modules_info: %1")
                 .arg(info ? QString::fromUtf8(info) : QStringLiteral("<null>")));
    delete[] info; // new[] in ModuleManager::getModulesInfoCStr

    char** known = logos_core_get_known_modules();
    int n = 0;
    if (known) {
        for (char** p = known; *p; ++p) {
            emit log(QStringLiteral("known: %1").arg(QString::fromUtf8(*p)));
            delete[] *p;
            ++n;
        }
        delete[] known;
    }
    emit log(QStringLiteral("known modules: %1 (an empty modules dir is the point)").arg(n));
    return ms;
}

void SmokeRunner::stop()
{
    if (!m_started) return;
    m_started = false;
    logos_core_cleanup();
    emit log(QStringLiteral("logos_core_cleanup: done"));
}
