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
    refuseSubprocessArtifacts();
}

void BundledSetCoreRuntime::refuseSubprocessArtifacts()
{
    // WHAT THE CONTAINER POLICY USED TO SAY, said where it can be said
    // truthfully -- see the note at logos_core_set_container_policy in
    // registerBundledSet(). A phone has a Native container and a Web container
    // and no subprocess module host, so the artifact that must not appear is a
    // Qt PLUGIN: every module the core discovered has to be `bare` or `web`.
    //
    // AFTER discovery rather than as a policy, because the core is the one that
    // stamps the format: a package's format is read off what is on disk, and
    // this is the first moment anything can be asked about it.
    char* info = logos_core_get_modules_info();
    if (!info) return;
    const QJsonDocument doc = QJsonDocument::fromJson(QByteArray(info));
    delete[] info;

    QStringList wrong;
    for (const QJsonValue& value : doc.array()) {
        const QJsonObject entry = value.toObject();
        const QString format = entry["format"].toString();
        if (format != QLatin1String("bare") && format != QLatin1String("web"))
            wrong.append(QStringLiteral("%1 (%2)").arg(entry["name"].toString(), format));
    }
    if (wrong.isEmpty()) return;
    emit log(QStringLiteral("WRONG: %1 would need a subprocess module host, and a phone "
                            "has none").arg(wrong.join(QStringLiteral(", "))));
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

    // WHY NOT `inproc`, WHICH THIS USED TO ASSERT. A phone runs modules in TWO
    // containers -- Native for the Bundled set's Bare images, Web for a
    // Downloaded module's `web` variant -- and liblogos' policies name one
    // container each: `inproc` refuses a `web` variant by name ("the container
    // policy is 'inproc' but this module is a web variant"), and `web` refuses
    // every Bare image the same way. There is no policy for "the two containers
    // this process has", so asserting either one is asserting something false.
    //
    // THE ASSERTION IS KEPT, HERE, where it can be stated exactly: what a phone
    // must never load is a Qt PLUGIN, because that is the artifact that needs a
    // subprocess module host and neither platform allows one (ADR 0003, 0006).
    // Bare and web are both in-process as far as Logos is concerned. So the core
    // is left on `auto` -- the artifact decides its container -- and
    // refuseSubprocessArtifacts() reads the formats the core discovered and
    // says so if one of them is neither.
    logos_core_set_container_policy("auto");

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

        const QString imagePath = imagePathFor(entry);
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
    // handshake, publication on the transport.
    //
    // AND IT DOES NOT NAME A CONTAINER, because this function cannot know
    // which one ran: the core picks it from the module's FORMAT, and a phone
    // has two (a Bare framework goes to the Native container, a `web` variant
    // to the Web container, and a `web` module reported "Native container"
    // here for as long as the only `web` module in the tree was a counter
    // nobody read this line about).
    emit log(QStringLiteral("  %1 loaded in %2 ms").arg(name).arg(t.elapsed()));
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

QString BundledSetCoreRuntime::imagePathFor(const QJsonObject& entry) const
{
    const QString image = entry["image"].toString();
    if (image.isEmpty())
        return {};
#if defined(Q_OS_ANDROID)
    // The APK FLATTENS the set. `image` is the path INSIDE the set --
    // `lib/lib<stem>.so`, which is what a variant payload is laid out as -- but
    // gradle packages every member as `lib<stem>.so` in the app's native
    // library directory, and since API 29 that directory is the only place
    // Android will dlopen from at all. m_imageDir IS that directory, so it is
    // the set's `lib/` rather than its parent: keeping the prefix asks for
    // <nativeLibDir>/lib/lib<stem>.so, which is nowhere. Measured on an
    // SM-G990B, as five consecutive "no image at ..." lines and an empty set.
    return m_imageDir + QLatin1Char('/') + image.section(QLatin1Char('/'), -1);
#else
    // iOS PRESERVES it: `Frameworks/<stem>.framework/<stem>` is a real path
    // inside <App>.app, which is what m_imageDir points at.
    return m_imageDir + QLatin1Char('/') + image;
#endif
}

QString BundledSetCoreRuntime::imagePathOf(const QString& name) const
{
    for (const QJsonValue& value : m_set["modules"].toArray()) {
        const QJsonObject entry = value.toObject();
        if (entry["name"].toString() == name)
            return imagePathFor(entry);
    }
    return {};
}
