#include "webview/LiveRuntimeBudget.h"

#include "webview/AppMemory.h"

#include <QStringList>
#include <QtGlobal>

#include <algorithm>

namespace basecamp::web {
namespace {

// The share of a device's memory this app may put into UI PAGES. See the
// header: a fraction rather than a number, because what is being defended
// scales with the device.
constexpr qint64 kRuntimeShareDivisor = 6;

// ...and the share the app as a WHOLE may weigh, which is the number
// observe() is read against.
constexpr qint64 kAppCeilingDivisor = 3;

// THE COUNT THIS RUN ASKED FOR, or 0 when it asked for nothing usable and the
// policy should answer instead.
//
// The flag is read first because it is the spelling a phone has: an APK's
// process inherits nothing a developer typed, while both launchers forward
// arguments. A flag that PARSED is the run's answer either way, so
// `--web-budget 0` falls back to the policy rather than to an environment the
// run did not mean to consult.
int statedRuntimeCount(const QStringList& args)
{
    const int flag = args.indexOf(QStringLiteral("--web-budget"));
    if (flag >= 0 && flag + 1 < args.size()) {
        bool isNumber = false;
        const int asked = args.at(flag + 1).toInt(&isNumber);
        if (isNumber) return asked > 0 ? asked : 0;
    }
    bool stated = false;
    const int asked = qEnvironmentVariableIntValue("LOGOS_WEB_RUNTIME_BUDGET", &stated);
    return stated && asked > 0 ? asked : 0;
}

} // namespace

int LiveRuntimeBudget::runtimesForDeviceMemory(qint64 deviceMemoryBytes,
                                              qint64 runtimeFootprintBytes)
{
    // A DEVICE THAT WILL NOT SAY GETS THE PHONE'S ANSWER. deviceMemoryBytes()
    // answers -1 where the platform does not report it, and the safe reading of
    // "I do not know how much memory this thing has" is the conservative
    // budget, not none at all.
    if (deviceMemoryBytes <= 0 || runtimeFootprintBytes <= 0) return 1;
    const qint64 forRuntimes = deviceMemoryBytes / kRuntimeShareDivisor;
    return int(std::clamp<qint64>(forRuntimes / runtimeFootprintBytes, 1, kMaxLiveRuntimes));
}

qint64 LiveRuntimeBudget::ceilingForDeviceMemory(qint64 deviceMemoryBytes)
{
    if (deviceMemoryBytes <= 0) return 0;
    return deviceMemoryBytes / kAppCeilingDivisor;
}

LiveRuntimeBudget LiveRuntimeBudget::forThisDevice(const QStringList& args)
{
    const qint64 device = deviceMemoryBytes();

    // THE RUN'S OWN NUMBER WINS, and the policy is what happens when nobody
    // states one.
    const int stated = statedRuntimeCount(args);
    const int runtimes = stated > 0 ? stated : runtimesForDeviceMemory(device);
    return LiveRuntimeBudget(runtimes, kDeviceRuntimeBytes, ceilingForDeviceMemory(device));
}

LiveRuntimeBudget::LiveRuntimeBudget(int maxLiveRuntimes, qint64 runtimeFootprintBytes,
                                     qint64 appCeilingBytes)
    // ONE IS THE FLOOR. A budget of zero would have show() evict the very page
    // it was asked to show, so a host that miscomputed its budget from a device
    // with no memory reading would render nothing at all rather than one module.
    : m_maxLive(std::max(1, maxLiveRuntimes))
    , m_allowance(m_maxLive)
    , m_footprintBytes(std::max<qint64>(0, runtimeFootprintBytes))
    , m_ceilingBytes(std::max<qint64>(0, appCeilingBytes))
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

    // THE ALLOWANCE, NOT THE DEVICE'S COUNT. They are the same number until a
    // measurement or a memory warning tightens it, and after one of those the
    // app must not grow straight back to the size that caused it.
    return trimToAllowance();
}

QStringList LiveRuntimeBudget::observe(qint64 appResidentBytes)
{
    // NOTHING TO WEIGH AGAINST, or nothing weighed: either way this is not a
    // reason to take a page away. A host that states its own count and no
    // ceiling counts, exactly as this class did before #153.
    if (m_ceilingBytes <= 0 || appResidentBytes < 0) return {};

    if (appResidentBytes > m_ceilingBytes) {
        // ONE PAGE PER OBSERVATION. The reading is the host's and not the
        // page's, so the container cannot know which page grew; giving one up
        // and looking again converges on the same answer without spending three
        // cold starts on a spike that was about to pass.
        m_allowance = std::max(1, std::min(m_allowance, int(m_live.size()) - 1));
        return trimToAllowance();
    }

    // ...AND IT GROWS BACK, but only from well under the ceiling. See
    // relaxBelowBytes().
    if (appResidentBytes <= relaxBelowBytes()) m_allowance = m_maxLive;
    return {};
}

QStringList LiveRuntimeBudget::shedUnderPressure()
{
    m_allowance = 1;
    return trimToAllowance();
}

QStringList LiveRuntimeBudget::trimToAllowance()
{
    QStringList evicted;
    while (m_live.size() > std::max(1, m_allowance)) evicted.append(m_live.takeLast());
    return evicted;
}

void LiveRuntimeBudget::forget(const QString& module)
{
    m_live.removeAll(module);
    if (m_visible == module) m_visible.clear();
}

} // namespace basecamp::web
