#include "webview/AppMemory.h"

#if defined(Q_OS_DARWIN)
#  include <mach/mach.h>
#  include <mach/task_info.h>
#  include <sys/sysctl.h>
#elif defined(Q_OS_LINUX) || defined(Q_OS_ANDROID)
#  include <QByteArray>
#  include <QFile>
#  include <unistd.h>
#endif

namespace basecamp::web {

#if defined(Q_OS_LINUX) || defined(Q_OS_ANDROID)
namespace {

// ONE FIELD OUT OF /proc/meminfo, in bytes, or -1 when it is not there.
//
// READ UNTIL readLine() RETURNS NOTHING, never `atEnd()`. A file in /proc
// reports a SIZE OF ZERO, so QFile::atEnd() is true before a single byte has
// been read and a loop written the obvious way reads nothing at all --
// measured on the Samsung SM-G990B on 2026-09-16, where deviceMemoryBytes()
// answered -1 and the Shell ran the whole session with no ceiling and the
// fallback count of one.
qint64 meminfoBytes(const char* field)
{
    QFile meminfo(QStringLiteral("/proc/meminfo"));
    if (!meminfo.open(QIODevice::ReadOnly)) return -1;
    const QByteArray wanted = QByteArray(field) + ':';
    for (QByteArray line = meminfo.readLine(); !line.isEmpty(); line = meminfo.readLine()) {
        if (!line.startsWith(wanted)) continue;
        const QList<QByteArray> fields = line.simplified().split(' ');
        if (fields.size() < 2) return -1;
        bool ok = false;
        const qlonglong kb = fields.at(1).toLongLong(&ok);
        return ok ? qint64(kb) * 1024 : -1;
    }
    return -1;
}

} // namespace
#endif

qint64 appResidentBytes()
{
#if defined(Q_OS_DARWIN)
    task_vm_info_data_t info{};
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_VM_INFO,
                  reinterpret_cast<task_info_t>(&info), &count) != KERN_SUCCESS)
        return -1;
    return qint64(info.phys_footprint);
#elif defined(Q_OS_LINUX) || defined(Q_OS_ANDROID)
    // statm rather than status: two integers and no parsing of a units column,
    // and the second is the resident set in PAGES.
    QFile statm(QStringLiteral("/proc/self/statm"));
    if (!statm.open(QIODevice::ReadOnly)) return -1;
    const QList<QByteArray> fields = statm.readLine().simplified().split(' ');
    if (fields.size() < 2) return -1;
    bool ok = false;
    const qlonglong pages = fields.at(1).toLongLong(&ok);
    if (!ok) return -1;
    return qint64(pages) * qint64(::sysconf(_SC_PAGESIZE));
#else
    return -1;
#endif
}

qint64 deviceMemoryBytes()
{
#if defined(Q_OS_DARWIN)
    qint64 bytes = 0;
    size_t size = sizeof(bytes);
    if (::sysctlbyname("hw.memsize", &bytes, &size, nullptr, 0) != 0) return -1;
    return bytes > 0 ? bytes : -1;
#elif defined(Q_OS_LINUX) || defined(Q_OS_ANDROID)
    // MemTotal rather than `_SC_PHYS_PAGES`: on Android the two differ (the
    // kernel's own reservations are already out of MemTotal), and MemTotal is
    // the figure every other tool on the device reports.
    return meminfoBytes("MemTotal");
#else
    return -1;
#endif
}

qint64 deviceAvailableBytes()
{
#if defined(Q_OS_LINUX) || defined(Q_OS_ANDROID)
    // MemAvailable rather than MemFree: free memory on a phone is a handful of
    // MB by design, because the kernel spends the rest on page cache it will
    // hand back on demand. MemAvailable is the kernel's own estimate of what a
    // new allocation could actually get, which is the question being asked.
    return meminfoBytes("MemAvailable");
#else
    // See the header: iOS publishes no device-wide free figure to an app, and
    // inventing one out of hw.memsize would be a guess wearing a measurement's
    // clothes.
    return -1;
#endif
}

#if !defined(Q_OS_ANDROID)
// NO ActivityManager HERE. Android publishes its own low-memory line and every
// other platform in this file does not -- including plain Linux, where
// /proc/meminfo answers the two readings above but nothing states the level the
// system acts at. -1 is "there is no such reading", which
// LiveRuntimeBudget::ceilingForDeviceInUse answers with a share of the device.
qint64 deviceLowMemoryBytes()
{
    return -1;
}

QString processVisibilityReport()
{
    // The question is Android's: everywhere else either the pages are in this
    // process (iOS, and appResidentBytes() sees them) or there are no pages.
    return QStringLiteral("this platform charges a page to the process that opened it");
}
#endif

qint64 budgetWeighedBytes()
{
    if (!kBudgetWeighsTheDevice) return appResidentBytes();

    // THE DEVICE'S BOOK, because the page is not in this process's. See the
    // header: a `web` page lives in a Chromium renderer of its own and the only
    // figure it appears in is how much of the device is in use.
    const qint64 total = deviceMemoryBytes();
    const qint64 available = deviceAvailableBytes();
    if (total <= 0 || available < 0) return -1;
    return total - available;
}

#if !defined(Q_OS_IOS) && !defined(Q_OS_ANDROID)
// EVERY OTHER PLATFORM SAYS SO. A desktop has no memory-warning signal a Qt app
// can subscribe to, and a container that pretended otherwise would report a
// pressure handler it does not have. The polling half (observe()) still runs.
bool watchAppMemoryPressure(std::function<void()>)
{
    return false;
}
#endif

QString megabytes(qint64 bytes)
{
    return QStringLiteral("%1 MB").arg(double(bytes) / (1024.0 * 1024.0), 0, 'f', 0);
}

} // namespace basecamp::web
