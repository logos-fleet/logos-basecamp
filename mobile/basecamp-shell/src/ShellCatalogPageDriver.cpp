#include "ShellCatalogPageDriver.h"

#include "BundledSetShellHost.h"
#include "ShellModulesBackend.h"
#include "appmanager/PlatformFloor.h"
#include "appmanager/StoreAppManager.h"

#include <QDir>
#include <QImage>
#include <QPointF>
#include <QStandardPaths>
#include <QQuickItem>
#include <QSet>
#include <QStringList>
#include <QVariantMap>

namespace {

const QLatin1String kSectionButton("sidebar.section.app_manager");
const QLatin1String kPage("storeCatalogView");
const QLatin1String kNoCatalog("storeCatalog.unavailable");
const QLatin1String kList("storeCatalog.list");
const QLatin1String kRowPrefix("storeCatalog.row.");
const QLatin1String kInstallPrefix("storeCatalog.install.");
const QLatin1String kReasonPrefix("storeCatalog.reason.");

QString textOf(QQuickItem* item)
{
    return item ? item->property("text").toString() : QString();
}

QString nameOf(const QVariant& entry)
{
    return entry.toMap().value(QStringLiteral("name")).toString();
}

// Whether this refusal is the PLATFORM FLOOR'S rather than package_manager's.
//
// Told apart by the shape PlatformFloor::reasonFor gives its sentence, taken
// FROM that function rather than written out again here: the two refusals are
// identical to a row (the page shows what it is given) and different to a
// reader of this run -- "available on macOS and Linux" is the variant rule, and
// "requires <module>" is the one #169 derives.
bool isFloorRefusal(const QString& reason)
{
    // The sentence with a mark where the module name goes, so the two ends of
    // it are whatever reasonFor writes rather than a copy kept in step by hand.
    static const QString mark = QStringLiteral("\x01");
    static const QString shape = basecamp::appmanager::PlatformFloor::reasonFor(mark);
    const int split = shape.indexOf(mark);
    if (split < 0)
        return false;
    const QString head = shape.left(split);
    const QString tail = shape.mid(split + mark.size());
    // Longer than both ends together, so a name was named.
    return reason.size() > head.size() + tail.size()
           && reason.startsWith(head) && reason.endsWith(tail);
}

} // namespace

ShellCatalogPageDriver::ShellCatalogPageDriver(BundledSetShellHost* host, QWidget* shellWidget,
                                               QObject* parent)
    : ShellSceneDriver(shellWidget, parent)
    , m_host(host)
{
}

bool ShellCatalogPageDriver::run()
{
    QQuickItem* button = waitFor(kSectionButton, 5000);
    if (!button) {
        dumpNames(QStringLiteral("the Applications section button is not in the sidebar"));
        emit log(QStringLiteral("WRONG: no %1 in the sidebar; the catalog page cannot be "
                                "opened at all").arg(kSectionButton));
        return false;
    }
    if (!tap(button)) {
        emit log(QStringLiteral("WRONG: the Applications section button could not be "
                                "pressed where it is drawn"));
        return false;
    }

    QQuickItem* page = waitFor(kPage, 5000);
    if (!page || !page->isVisible()) {
        dumpNames(QStringLiteral("the Applications section drew no catalog page"));
        emit log(QStringLiteral("WRONG: the Applications section is open and the catalog "
                                "page is not on it -- a Store shell's App Manager is the "
                                "only place its catalog is shown"));
        return false;
    }
    emit log(QStringLiteral("shell: Applications -> the catalog page is on screen"));

    const bool ok = checkRows();
    // HELD, so the page is on screen long enough to be seen. A screenshot or a
    // screen recording is the evidence this pass exists to make possible, and
    // the next driver takes the window back within a frame or two.
    settle(2000);
    return ok;
}

bool ShellCatalogPageDriver::checkRows()
{
    basecamp::appmanager::StoreAppManager* manager = m_host->backend()->appManager();
    if (!manager) {
        emit log(QStringLiteral("WRONG: this shell has no App Manager at all"));
        return false;
    }

    QVariantList entries = manager->catalogEntries();
    if (entries.isEmpty()) {
        // ONE RE-READ. The repository registry is persisted under the app's data
        // directory, so a launch that names no `--repository` still has the one
        // an earlier launch added -- and the App Manager's own refresh happens
        // at startup, which on a cold start is before the network is up.
        manager->refreshCatalog();
        settle(1500);
        entries = manager->catalogEntries();
    }

    if (entries.isEmpty()) {
        // A LEGITIMATE STATE, not a failure: a Store shell's Bundled set is data
        // (ADR 0007) and the smallest useful shell carries no package modules at
        // all. What is NOT legitimate is an empty page, which reads as a catalog
        // with nothing in it.
        const QString why = manager->catalogUnavailableReason();
        if (why.isEmpty()) {
            emit log(QStringLiteral("CATALOG PAGE: this launch is pointed at no "
                                    "repository, so there is nothing to list"));
            return true;
        }
        QQuickItem* message = waitFor(kNoCatalog, 2000);
        if (!message || !message->isVisible() || textOf(message) != why) {
            emit log(QStringLiteral("WRONG: there is no catalog (\"%1\") and the page "
                                    "does not say so -- an empty list reads as a catalog "
                                    "with nothing in it").arg(why));
            return false;
        }
        emit log(QStringLiteral("CATALOG PAGE OK: no catalog in this build, and the page "
                                "says \"%1\"").arg(why));
        return true;
    }

    // A LIST IS SCROLLED, and that is not an implementation detail to work
    // around: a ListView instantiates the delegates around its viewport and
    // nothing else, so "the page draws a row for every entry" is a claim about
    // what a finger reaches by swiping rather than about one frame. This walks
    // the page the way a user does -- top to bottom, a bit less than a viewport
    // at a time, stopping short on a row a stop had to cut in half -- and
    // judges each row while it is on screen, because a row scrolled far enough
    // away is destroyed again.
    QQuickItem* list = find(kList);
    if (!list) {
        dumpNames(QStringLiteral("the catalog page has no list"));
        emit log(QStringLiteral("WRONG: the catalog page draws no list at all"));
        return false;
    }

    QSet<QString> judged;
    QStringList refusals;
    int offered = 0;
    int floorRefusals = 0;
    bool wrong = false;

    const auto judgeVisibleRows = [&]() {
        for (const QVariant& value : entries) {
            const QVariantMap entry = value.toMap();
            const QString name = nameOf(value);
            if (judged.contains(name))
                continue;
            QQuickItem* row = find(kRowPrefix + name);
            if (!row)
                continue;
            // EXISTING IS NOT BEING ON SCREEN. A ListView keeps a band of
            // delegates alive outside its viewport (cacheBuffer), so a row can
            // be found and still be drawn past the bottom edge -- and its
            // install control would then be judged unreachable for a reason
            // that is this driver's scroll position rather than the layout.
            // Measured: `storeCatalog.install.web_counter_b` at y 1461 of a
            // 1326-tall view, on the step before the one that brought it in.
            // Left for a later step, which is where it is fully visible.
            const QPointF inList = row->mapToItem(list, QPointF(0, 0));
            if (inList.y() < 0 || inList.y() + row->height() > list->height())
                continue;
            judged.insert(name);

            const bool canInstall = entry.value(QStringLiteral("canInstall")).toBool();
            const bool available = entry.value(QStringLiteral("available")).toBool();
            const QString reason = entry.value(QStringLiteral("unavailableReason")).toString();
            QQuickItem* install = find(kInstallPrefix + name);

            if (canInstall) {
                if (!install) {
                    emit log(QStringLiteral("WRONG: %1 may be installed here and the row "
                                            "offers no way to").arg(name));
                    wrong = true;
                    continue;
                }
                // NOT PRESSED. The control's job is to start a download and an
                // install; what is asserted here is that a finger could start
                // one -- which is a claim about the layout, and the one a
                // narrow screen breaks (logos-workspace#84, #87).
                if (!pressWouldReach(install))
                    wrong = true;
                else
                    ++offered;
                continue;
            }

            if (install) {
                emit log(QStringLiteral("WRONG: %1 cannot be installed here (%2) and the "
                                        "row offers an install control anyway -- it would "
                                        "succeed and the module would die at its first call")
                             .arg(name, reason.isEmpty() ? QStringLiteral("no reason given")
                                                         : reason));
                wrong = true;
                continue;
            }
            if (available)
                continue;  // unavailable only because it is already installed

            QQuickItem* shown = find(kReasonPrefix + name);
            if (!shown || !shown->isVisible()) {
                emit log(QStringLiteral("WRONG: %1 is listed unavailable (\"%2\") and the "
                                        "row does not say why -- a row that refuses "
                                        "silently is the failure this page exists to "
                                        "prevent").arg(name, reason));
                wrong = true;
                continue;
            }
            if (textOf(shown) != reason) {
                emit log(QStringLiteral("WRONG: %1's row says \"%2\" where the App Manager "
                                        "refused it with \"%3\"")
                             .arg(name, textOf(shown), reason));
                wrong = true;
                continue;
            }
            refusals << QStringLiteral("%1 -- %2").arg(name, reason);
            if (isFloorRefusal(reason)) {
                ++floorRefusals;
                // AND A PICTURE OF THE FIRST ONE, while the row that carries it
                // is on screen. The assertions above are read off the live
                // scene, which is the proof; this is the thing a person can
                // look at, and on a phone there is no inspector to take it
                // with (ADR 0002). One is enough: they all render the same way.
                if (m_picture.isEmpty())
                    m_picture = savePicture(row);
            }
        }
    };

    // The first rows exist a few ticks after the page appears; everything below
    // them exists only once the list has been scrolled to it.
    waitFor(kRowPrefix + nameOf(entries.first()), kRowTimeoutMs);

    const qreal viewport = list->height();
    const qreal step = viewport * 0.8;

    // Where the first row this stop could NOT take whole begins, or -1 when
    // there is none: one that exists, starts below the top edge and is not
    // judged yet -- so either it is cut off by the bottom edge or it is out of
    // the viewport altogether, and the rows above it are all done. A pixel of
    // slack, so that scrolling there puts it just inside the top edge rather
    // than exactly on it. A row taller than the viewport is never whole
    // anywhere and is not one of these: stopping on it would be stopping for
    // ever.
    const auto unfinishedRowTop = [&]() -> qreal {
        for (const QVariant& value : entries) {
            const QString name = nameOf(value);
            if (judged.contains(name))
                continue;
            QQuickItem* row = find(kRowPrefix + name);
            if (!row)
                continue;
            const qreal top = row->mapToItem(list, QPointF(0, 0)).y();
            if (top > 1 && row->height() + 2 <= viewport)
                return top - 1;
        }
        return -1;
    };

    for (qreal contentY = 0;;) {
        judgeVisibleRows();
        // The list grows as it is walked -- a delegate's height is not known
        // until it exists -- so the end is re-read at every stop rather than
        // fixed up front.
        const qreal end = list->property("contentHeight").toReal() - viewport;
        if (step <= 0 || contentY >= end)
            break;
        // THE NEXT STOP is the start of a row this one had to leave half
        // drawn, and a step down the list when there is none. Stepping blindly
        // would leave a delegate taller than the OVERLAP between two stops
        // straddling the bottom edge at one and the top edge at the next --
        // never whole on screen, and then reported missing for a reason that is
        // this driver's step size rather than the page. Always at least a pixel
        // further down and never past the end, so the walk reaches the bottom
        // and stops there.
        const qreal unfinished = unfinishedRowTop();
        const qreal target = contentY + (unfinished > 0 ? unfinished : step);
        contentY = qMin(qMax(target, contentY + 1), end);
        list->setProperty("contentY", contentY);
        settle(kScrollSettleMs);
    }
    list->setProperty("contentY", 0);

    QStringList missing;
    for (const QVariant& value : entries) {
        if (!judged.contains(nameOf(value)))
            missing << nameOf(value);
    }
    if (!missing.isEmpty()) {
        dumpNames(QStringLiteral("the catalog page draws no row for %1")
                      .arg(missing.join(QStringLiteral(", "))));
        emit log(QStringLiteral("WRONG: the catalog lists %1 and the page has no row for "
                                "%2 of them, scrolled end to end: %3")
                     .arg(entries.size()).arg(missing.size())
                     .arg(missing.join(QStringLiteral(", "))));
        return false;
    }
    if (wrong)
        return false;

    emit log(QStringLiteral("CATALOG PAGE OK: %1 row(s) on screen, %2 offered for install, "
                            "%3 refused (%4 by this build's Platform floor)")
                 .arg(entries.size()).arg(offered).arg(refusals.size()).arg(floorRefusals));
    if (!m_picture.isEmpty())
        emit log(QStringLiteral("catalog page picture: %1").arg(m_picture));
    for (const QString& refusal : refusals)
        emit log(QStringLiteral("  refused on screen: %1").arg(refusal));
    return true;
}

// The page as it is drawn, written where a run can fetch it off the device.
//
// grabFramebuffer(), not the platform's screenshot: a QQuickWidget's pixels are
// its scene's, rendered by the scene graph into an offscreen surface, and the
// simulator's own screen capture showed the frame from BEFORE the section
// change for the whole of this pass -- measured, 24 captures a second apart on
// an iPad Air 13-inch (M2), every one of them identical. The app is the only
// thing here that can see what the app drew.
QString ShellCatalogPageDriver::savePicture(QQuickItem* item)
{
    const QImage frame = frameOf(surfaceOf(item));
    if (frame.isNull()) {
        emit log(QStringLiteral("catalog page: the surface gave no frame to save"));
        return {};
    }
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (dir.isEmpty() || !QDir().mkpath(dir))
        return {};
    const QString path = dir + QStringLiteral("/catalog-page.png");
    if (!frame.save(path)) {
        emit log(QStringLiteral("catalog page: could not write %1").arg(path));
        return {};
    }
    return QStringLiteral("%1 (%2x%3)").arg(path).arg(frame.width()).arg(frame.height());
}
