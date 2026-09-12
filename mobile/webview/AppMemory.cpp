#include "webview/AppMemory.h"

#if defined(Q_OS_DARWIN)
#  include <mach/mach.h>
#  include <mach/task_info.h>
#elif defined(Q_OS_LINUX) || defined(Q_OS_ANDROID)
#  include <QByteArray>
#  include <QFile>
#  include <unistd.h>
#endif

namespace basecamp::web {

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

} // namespace basecamp::web
