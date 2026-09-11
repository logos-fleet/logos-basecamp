#include "BundledSetCoreRuntime.h"

#include "BundledSetManifest.h"

#include <logos_core.h>
#include <logos_protocol.h>

#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QVariantMap>

#include <dlfcn.h>

namespace {

// Take ownership of a char** the C API returns with new[]/new[] and hand back a
// QStringList. Every logos_core_get_* list is allocated this way.
QStringList takeList(char** list)
{
    QStringList out;
    if (!list) return out;
    for (char** p = list; *p; ++p) {
        out.append(QString::fromUtf8(*p));
        delete[] *p;
    }
    delete[] list;
    return out;
}

LogosLoadDeps toLoadDeps(LoadPolicy policy)
{
    switch (policy) {
    case LoadPolicy::ModuleOnly:          return LOGOS_LOAD_MODULE_ONLY;
    case LoadPolicy::RequiredAndOptional: return LOGOS_LOAD_REQUIRED_AND_OPTIONAL;
    case LoadPolicy::RequiredDeps:        break;
    }
    return LOGOS_LOAD_REQUIRED_DEPS;
}

// The per-module manifest logos_core_add_bare_module wants, projected from the
// Bundled-set entry. The set manifest is the authority for name, version and
// dependencies -- they came out of the .lgx the catalog signed -- so this is a
// projection of it, not a second source.
QByteArray moduleManifest(const QJsonObject& entry)
{
    QJsonObject manifest;
    manifest["manifestVersion"] = QStringLiteral("1.0");
    manifest["name"] = entry["name"].toString();
    manifest["version"] = entry["version"].toString();
    manifest["description"] = QStringLiteral("Bundled with the app");
    manifest["author"] = QStringLiteral("Logos");
    manifest["type"] = entry["type"].toString();
    manifest["category"] = QStringLiteral("bundled");
    manifest["dependencies"] = entry["dependencies"].toArray();
    return QJsonDocument(manifest).toJson(QJsonDocument::Compact);
}

} // namespace

BundledSetCoreRuntime::BundledSetCoreRuntime(const ICoreRuntime::Config& config,
                                             QObject* parent,
                                             QString manifestJson,
                                             QString imageDir)
    : QObject(parent)
    , m_config(config)
    , m_imageDir(imageDir.isEmpty() ? imageDirFromRunningImage() : std::move(imageDir))
{
    const QByteArray raw = manifestJson.isEmpty()
        ? QByteArray(kBundledSetManifest)
        : manifestJson.toUtf8();
    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &parseError);
    // Recorded, not emitted: nothing can be connected to log() from inside the
    // constructor, so the one place this can be reported is start().
    if (doc.isObject())
        m_set = doc.object();
    else
        m_manifestError = QStringLiteral("manifest is not JSON (%1)").arg(parseError.errorString());
}

BundledSetCoreRuntime::~BundledSetCoreRuntime() = default;

QString BundledSetCoreRuntime::imageDirFromRunningImage()
{
    // Asked of the DYNAMIC LOADER rather than derived from
    // QCoreApplication::applicationDirPath(), because the two do not agree on
    // Android: applicationDirPath() is a path under /data/data, and the native
    // library directory -- the only place dlopen() may load from since API 29
    // -- is somewhere under /data/app entirely. dladdr answers with the place
    // the Logos code is ACTUALLY loaded from, which on both platforms is where
    // the Bundled set sits too: <App>.app on iOS (one static image), the native
    // library directory on Android.
    Dl_info info{};
    if (dladdr(reinterpret_cast<const void*>(&lp_protocol_version), &info) == 0 || !info.dli_fname)
        return {};
    return QFileInfo(QString::fromUtf8(info.dli_fname)).absolutePath();
}

void BundledSetCoreRuntime::start()
{
    if (m_started) return;
    m_started = true;

    logos_core_set_persistence_base_path(m_config.persistenceBasePath.c_str());
    for (const std::string& dir : m_config.modulesDirs)
        logos_core_add_modules_dir(dir.c_str());
    if (m_config.accessPolicyJson.has_value())
        logos_core_set_access_policy(m_config.accessPolicyJson->c_str());

    QElapsedTimer t;
    t.start();
    logos_core_start();
    m_startMs = t.elapsed();
    emit log(QStringLiteral("logos_core_start: %1 ms").arg(m_startMs));

    registerBundledSet();
}

void BundledSetCoreRuntime::registerBundledSet()
{
    if (!m_manifestError.isEmpty()) {
        emit log(QStringLiteral("bundled set: %1").arg(m_manifestError));
        return;
    }

    const QJsonArray modules = m_set["modules"].toArray();
    emit log(QStringLiteral("bundled set: %1 module(s) for %2")
                 .arg(modules.size())
                 .arg(m_set["target"].toString()));

    if (m_imageDir.isEmpty()) {
        emit log(QStringLiteral("bundled set: could not locate the Logos image directory"));
        return;
    }

    // An ASSERTION, not a switch: it says every module in this process must be
    // a Bare module, so a Qt plugin quietly finding its way in is a load error
    // rather than a second container starting a subprocess on a phone -- which
    // is a thing neither platform allows at all.
    logos_core_set_container_policy("inproc");

    for (const QJsonValue& value : modules) {
        const QJsonObject entry = value.toObject();
        const QString name = entry["name"].toString();

        // A view module is loaded by the HOST, not by the core: its image is a
        // Qt-backed framework whose object this process instantiates and renders
        // in its own engine (ADR 0006). It is in the set and in the Modules
        // list, but it is not the core's to register.
        if (entry["type"].toString() == QLatin1String("ui_qml"))
            continue;
        if (m_registered.contains(name))
            continue;

        const QString imagePath = m_imageDir + QStringLiteral("/") + entry["image"].toString();
        if (!QFileInfo::exists(imagePath)) {
            emit log(QStringLiteral("  %1: no image at %2 (embed step did not run?)")
                         .arg(name, imagePath));
            continue;
        }

        char* registered = logos_core_add_bare_module(moduleManifest(entry).constData(),
                                                      imagePath.toUtf8().constData());
        if (!registered) {
            emit log(QStringLiteral("  %1: the core refused to register it").arg(name));
            continue;
        }
        m_registered.append(QString::fromUtf8(registered));
        delete[] registered;
    }
    emit log(QStringLiteral("bundled set registered: %1")
                 .arg(m_registered.isEmpty() ? QStringLiteral("(none)")
                                             : m_registered.join(QStringLiteral(", "))));
}

QStringList BundledSetCoreRuntime::knownModules() const
{
    return takeList(logos_core_get_known_modules());
}

QStringList BundledSetCoreRuntime::loadedModules() const
{
    return takeList(logos_core_get_loaded_modules());
}

bool BundledSetCoreRuntime::loadModule(const QString& name, LoadPolicy policy)
{
    // "Ensure loaded", not "load fresh" -- ICoreRuntime's contract.
    if (loadedModules().contains(name))
        return true;

    QElapsedTimer t;
    t.start();
    if (logos_core_load_module(name.toUtf8().constData(), toLoadDeps(policy)) != 1) {
        // The core reports WHY through spdlog, and spdlog writes to stderr --
        // which on Android goes nowhere. So ask the loader the same question
        // directly: for a Bare module the overwhelmingly likely failure is an
        // `lp_*` that did not resolve, and dlerror() names it.
        const QString imagePath = imagePathOf(name);
        if (!imagePath.isEmpty()) {
            ::dlerror();
            void* probe = dlopen(imagePath.toUtf8().constData(), RTLD_NOW | RTLD_LOCAL);
            const char* why = ::dlerror();
            if (probe) {
                dlclose(probe);
                emit log(QStringLiteral("  %1: load failed, but the image opens cleanly "
                                        "-- the refusal is the core's, not dyld's").arg(name));
            } else {
                emit log(QStringLiteral("  %1: load failed -- %2")
                             .arg(name, why ? QString::fromUtf8(why)
                                            : QStringLiteral("dlopen gave no reason")));
            }
        } else {
            emit log(QStringLiteral("  %1: load failed").arg(name));
        }
        return false;
    }
    // The dlopen itself is timed and logged INSIDE the Native container
    // (InProcContainer::launch), which is the only place that can measure it
    // alone. This number is the whole load: registration checks, the container
    // handshake, publication on the in-process transport.
    emit log(QStringLiteral("  %1 loaded in %2 ms (Native container)").arg(name).arg(t.elapsed()));
    return true;
}

bool BundledSetCoreRuntime::unloadModule(const QString& name, bool withDependents)
{
    const bool ok = logos_core_unload_module(name.toUtf8().constData(), withDependents) == 1;
    emit log(ok ? QStringLiteral("  %1 unloaded").arg(name)
                : QStringLiteral("  %1: unload failed").arg(name));
    return ok;
}

void BundledSetCoreRuntime::refreshModules()
{
    logos_core_refresh_modules();
    registerBundledSet();
}

QVariantList BundledSetCoreRuntime::allStats() const
{
    char* json = logos_core_get_module_stats();
    if (!json) return {};
    const QJsonDocument doc = QJsonDocument::fromJson(QByteArray(json));
    delete[] json;
    return doc.isArray() ? doc.array().toVariantList() : QVariantList{};
}

QVariantList BundledSetCoreRuntime::bundledSet() const
{
    QVariantList out;
    for (const QJsonValue& value : m_set["modules"].toArray()) {
        const QJsonObject entry = value.toObject();
        QVariantMap row;
        row["name"] = entry["name"].toString();
        row["version"] = entry["version"].toString();
        row["type"] = entry["type"].toString();
        row["image"] = entry["image"].toString();
        out.append(row);
    }
    return out;
}

QString BundledSetCoreRuntime::imagePathOf(const QString& name) const
{
    for (const QJsonValue& value : m_set["modules"].toArray()) {
        const QJsonObject entry = value.toObject();
        if (entry["name"].toString() == name)
            return m_imageDir + QStringLiteral("/") + entry["image"].toString();
    }
    return {};
}
