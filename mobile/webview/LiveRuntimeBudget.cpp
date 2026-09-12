#include "webview/LiveRuntimeBudget.h"

#include <algorithm>

namespace basecamp::web {

LiveRuntimeBudget::LiveRuntimeBudget(int maxLiveRuntimes, qint64 runtimeFootprintBytes)
    // ONE IS THE FLOOR. A budget of zero would have show() evict the very page
    // it was asked to show, so a host that miscomputed its budget from a device
    // with no memory reading would render nothing at all rather than one module.
    : m_maxLive(std::max(1, maxLiveRuntimes))
    , m_footprintBytes(std::max<qint64>(0, runtimeFootprintBytes))
{
}

QStringList LiveRuntimeBudget::show(const QString& module)
{
    m_visible = module;

    // Move-to-front, whether or not it was there: this is the only place
    // recency is recorded, and a module that is shown again is the newest even
    // when nothing about the live set changed.
    m_live.removeAll(module);
    m_live.prepend(module);

    QStringList evicted;
    while (m_live.size() > m_maxLive) evicted.append(m_live.takeLast());
    return evicted;
}

void LiveRuntimeBudget::forget(const QString& module)
{
    m_live.removeAll(module);
    if (m_visible == module) m_visible.clear();
}

} // namespace basecamp::web
