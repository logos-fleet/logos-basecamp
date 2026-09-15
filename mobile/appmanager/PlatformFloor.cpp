#include "PlatformFloor.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

namespace basecamp::appmanager {

namespace {

QStringList stringsAt(const QJsonObject& object, const char* key)
{
    QStringList out;
    for (const QJsonValue& v : object.value(QLatin1String(key)).toArray()) {
        const QString s = v.toString();
        if (!s.isEmpty())
            out.append(s);
    }
    return out;
}

} // namespace

PlatformFloor::PlatformFloor(QStringList present, QStringList absent)
    : m_present(std::move(present))
    , m_absent(std::move(absent))
    , m_declared(true)
{
}

PlatformFloor PlatformFloor::fromBundledSetManifest(const QByteArray& manifestJson)
{
    const QJsonDocument doc = QJsonDocument::fromJson(manifestJson);
    return doc.isObject() ? fromBundledSetManifest(doc.object()) : PlatformFloor();
}

PlatformFloor PlatformFloor::fromBundledSetManifest(const QJsonObject& manifest)
{
    const QJsonValue floor = manifest.value(QLatin1String("platformFloor"));
    if (!floor.isObject())
        return {};
    const QJsonObject declared = floor.toObject();
    return PlatformFloor(stringsAt(declared, "present"), stringsAt(declared, "absent"));
}

QString PlatformFloor::missingFor(const QString& name,
                                  const QHash<QString, QStringList>& dependencies) const
{
    if (!m_declared || m_absent.isEmpty())
        return {};

    // Breadth-first over the declared graph, `seen` carrying the cycle guard: a
    // catalog is somebody else's data and a dependency loop in it must come out
    // as a verdict, not as a hang on the UI thread.
    QSet<QString> seen{name};
    QStringList frontier{name};
    while (!frontier.isEmpty()) {
        const QString current = frontier.takeFirst();
        for (const QString& dep : dependencies.value(current)) {
            if (m_absent.contains(dep))
                return dep;
            if (!seen.contains(dep)) {
                seen.insert(dep);
                frontier.append(dep);
            }
        }
    }
    return {};
}

QString PlatformFloor::reasonFor(const QString& missing)
{
    return QStringLiteral("requires %1, not in this build").arg(missing);
}

} // namespace basecamp::appmanager
