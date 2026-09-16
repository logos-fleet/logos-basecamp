#include "AppNotices.h"

#include <utility>

namespace basecamp::shell {

AppNotices::AppNotices(RaiseFn raise, DropFn drop)
    : m_raise(std::move(raise))
    , m_drop(std::move(drop))
{
}

void AppNotices::unavailable(const QString& name, const QString& reason, bool mounted)
{
    if (name.isEmpty() || mounted)
        return;
    // REFRESHED, never taken down and put back: the tab is where the user is
    // looking, and a drop/raise pair would move it to the end of the strip and
    // steal the focus back on every retry.
    const bool fresh = !m_reasons.contains(name);
    m_reasons.insert(name, reason);
    if (fresh) m_order << name;
    if (m_raise) m_raise(name, reason);
}

void AppNotices::arrived(const QString& name)
{
    if (!m_reasons.contains(name))
        return;
    forget(name);
    if (m_drop) m_drop(name);
}

bool AppNotices::closed(const QString& name)
{
    if (!m_reasons.contains(name))
        return false;
    forget(name);
    if (m_drop) m_drop(name);
    return true;
}

void AppNotices::forget(const QString& name)
{
    m_reasons.remove(name);
    m_order.removeAll(name);
}

} // namespace basecamp::shell
