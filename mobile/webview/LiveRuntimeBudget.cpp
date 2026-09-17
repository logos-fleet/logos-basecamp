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
// observe() is read against WHERE THE PAGES ARE IN THE APP (iOS, macOS).
constexpr qint64 kAppCeilingDivisor = 3;

// ...and the share of a device that stands in for the OS's own low-memory line
// where the platform does not publish one. See
// LiveRuntimeBudget::ceilingForDeviceInUse.
constexpr qint64 kAssumedLowMemoryDivisor = 8;

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

// THE CEILING THIS RUN ASKED FOR, in bytes, or 0 when it asked for nothing and
// the policy should answer instead. Stated in MEGABYTES, because that is the
// unit every memory line in this container is printed in and a device run
// types it off the pass's own output.
qint64 statedCeilingBytes(const QStringList& args)
{
    const int flag = args.indexOf(QStringLiteral("--web-ceiling"));
    if (flag >= 0 && flag + 1 < args.size()) {
        bool isNumber = false;
        const qint64 asked = args.at(flag + 1).toLongLong(&isNumber);
        if (isNumber) return asked > 0 ? asked * 1024 * 1024 : 0;
    }
    bool stated = false;
    const int asked = qEnvironmentVariableIntValue("LOGOS_WEB_APP_CEILING_MB", &stated);
    return stated && asked > 0 ? qint64(asked) * 1024 * 1024 : 0;
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

qint64 LiveRuntimeBudget::ceilingForDeviceInUse(qint64 deviceMemoryBytes,
                                                qint64 deviceLowMemoryBytes,
                                                qint64 runtimeFootprintBytes)
{
    if (deviceMemoryBytes <= 0) return 0;
    const qint64 keepFree = deviceLowMemoryBytes > 0
                                ? deviceLowMemoryBytes
                                : deviceMemoryBytes / kAssumedLowMemoryDivisor;
    const qint64 ceiling =
        deviceMemoryBytes - keepFree - std::max<qint64>(0, runtimeFootprintBytes);
    // A DEVICE TOO SMALL TO HOLD ONE PAGE OVER ITS OWN LINE gets no ceiling
    // rather than a negative one -- a negative ceiling is "evict on every
    // observation", which would take the page the user is looking at down to
    // the floor of one on the first poll of every run.
    return ceiling > 0 ? ceiling : 0;
}

LiveRuntimeBudget LiveRuntimeBudget::forThisDevice(const QStringList& args)
{
    const qint64 device = deviceMemoryBytes();

    // THE RUN'S OWN NUMBER WINS, and the policy is what happens when nobody
    // states one.
    const int stated = statedRuntimeCount(args);
    const int runtimes = stated > 0 ? stated : runtimesForDeviceMemory(device);

    // ...AND THE CEILING IS STATED IN WHATEVER FRAME THIS PLATFORM'S READING IS
    // IN (logos-workspace#244). The pair has to match: observe() weighs
    // AppMemory::budgetWeighedBytes() and knows nothing about which of the two
    // it was handed, so the only place the two halves can be kept together is
    // here, where both are chosen at once -- off the same answer that call
    // chooses by (AppMemory::kBudgetWeighsTheDevice).
    const qint64 fromDevice =
        kBudgetWeighsTheDevice
            ? ceilingForDeviceInUse(device, deviceLowMemoryBytes(), kDeviceRuntimeBytes)
            : ceilingForDeviceMemory(device);

    // ...AND A RUN CAN STATE THE CEILING TOO, for the same reason it can state
    // the count (#244). A ceiling that cannot be reached is indistinguishable
    // from one that never needed to be, and that is how #153's unreachable
    // branch survived a full landing -- so a device run has to be able to put
    // the ceiling where the device will cross it and watch the eviction
    // happen. It is stated in MB against whatever frame this platform weighs:
    // `--web-ceiling 1400` on a phone that idles with 1318 MB in use is one
    // page's worth above the resting figure.
    const qint64 statedCeiling = statedCeilingBytes(args);
    return LiveRuntimeBudget(runtimes, kDeviceRuntimeBytes,
                             statedCeiling > 0 ? statedCeiling : fromDevice);
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

QStringList LiveRuntimeBudget::observe(qint64 weighedBytes)
{
    // NOTHING TO WEIGH AGAINST, or nothing weighed: either way this is not a
    // reason to take a page away. A host that states its own count and no
    // ceiling counts, exactly as this class did before #153.
    if (m_ceilingBytes <= 0 || weighedBytes < 0) return {};

    if (weighedBytes > m_ceilingBytes) {
        // ONE PAGE PER OBSERVATION. The reading is the host's and not the
        // page's, so the container cannot know which page grew; giving one up
        // and looking again converges on the same answer without spending three
        // cold starts on a spike that was about to pass.
        m_allowance = std::max(1, std::min(m_allowance, int(m_live.size()) - 1));
        return trimToAllowance();
    }

    // ...AND IT GROWS BACK, but only from well under the ceiling. See
    // relaxBelowBytes().
    if (weighedBytes <= relaxBelowBytes()) m_allowance = m_maxLive;
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
