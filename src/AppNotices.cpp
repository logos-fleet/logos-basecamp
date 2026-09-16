#include "AppNotices.h"

#include <utility>

namespace basecamp::shell {

AppNotices::AppNotices(RaiseFn raise, DropFn drop)
    : m_raise(std::move(raise))
    , m_drop(std::move(drop))
{
}

void AppNotices::unavailable(const QString& name, const QString& reason, bool docked)
{
    if (name.isEmpty())
        return;
    // A tab under this name that is not one of ours is the app itself, already
    // on screen: leave it alone.
    if (docked && !m_reasons.contains(name))
        return;
    // REFRESHED, never taken down and put back: the tab is where the user is
    // looking, and a drop/raise pair would move it to the end of the strip and
    // steal the focus back on every retry.
    m_reasons.insert(name, reason);
    if (m_raise) m_raise(name, reason);
}

void AppNotices::arrived(const QString& name)
{
    takeDown(name);
}

bool AppNotices::closed(const QString& name)
{
    return takeDown(name);
}

bool AppNotices::takeDown(const QString& name)
{
    if (m_reasons.remove(name) == 0)
        return false;
    if (m_drop) m_drop(name);
    return true;
}

} // namespace basecamp::shell
