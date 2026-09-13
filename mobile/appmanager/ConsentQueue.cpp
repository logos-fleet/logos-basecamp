#include "ConsentQueue.h"

namespace basecamp::appmanager {

namespace {
const QLatin1String kDownloaded("downloaded");
} // namespace

QString ConsentQueue::Prompt::question() const
{
    // Which party is named, and why, is documented on the declaration.
    if (callerOrigin == kDownloaded) {
        return QStringLiteral("\u201c%1\u201d, which you installed, wants to use \u201c%2\u201d.")
            .arg(caller, target);
    }
    if (targetOrigin == kDownloaded) {
        return QStringLiteral("\u201c%1\u201d wants to use \u201c%2\u201d, which you installed.")
            .arg(caller, target);
    }
    // capability_module does not announce a Bundled/Bundled pair, so this is
    // reachable only from a payload that lost its origins. Ask the plain
    // question rather than claiming an origin the payload did not carry.
    return QStringLiteral("\u201c%1\u201d wants to use \u201c%2\u201d.").arg(caller, target);
}

QString ConsentQueue::key(const QString& caller, const QString& target)
{
    // A newline, not a separator a module name could contain: module names are
    // registry identifiers, and a key built with '-' would let
    // ("a", "b-c") and ("a-b", "c") collide into one question.
    return caller + QLatin1Char('\n') + target;
}

bool ConsentQueue::offer(const QVariantMap& payload)
{
    Prompt p;
    p.caller       = payload.value(QStringLiteral("caller")).toString();
    p.target       = payload.value(QStringLiteral("target")).toString();
    p.callerOrigin = payload.value(QStringLiteral("callerOrigin")).toString();
    p.targetOrigin = payload.value(QStringLiteral("targetOrigin")).toString();
    if (!p.isValid())
        return false;

    const QString k = key(p.caller, p.target);
    if (m_queuedKeys.contains(k))
        return false;

    m_queuedKeys.insert(k);
    m_pending.append(p);
    return true;
}

ConsentQueue::Prompt ConsentQueue::current() const
{
    return m_pending.isEmpty() ? Prompt{} : m_pending.first();
}

QVariantMap ConsentQueue::answer(bool granted)
{
    if (m_pending.isEmpty())
        return {};

    const Prompt p = m_pending.takeFirst();
    m_queuedKeys.remove(key(p.caller, p.target));

    const QVariantMap decision{
        {QStringLiteral("caller"), p.caller},
        {QStringLiteral("target"), p.target},
        {QStringLiteral("granted"), granted},
    };
    m_answered.append(decision);
    return decision;
}

void ConsentQueue::dismiss()
{
    if (m_pending.isEmpty())
        return;
    const Prompt p = m_pending.takeFirst();
    // The key goes too, so the NEXT announcement for this pair is not swallowed
    // here as a duplicate. capability_module announces an undecided pair once
    // per process run, so that next one arrives on a later launch (it does not
    // persist what it has announced) or after forgetConsent -- not on the
    // module's very next retry.
    m_queuedKeys.remove(key(p.caller, p.target));
}

} // namespace basecamp::appmanager
