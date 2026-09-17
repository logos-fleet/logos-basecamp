#include "WebDriveFlows.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QLatin1String>

namespace basecamp::shell {

namespace {

// Budgets are the MODULE's, not a page's, so each is written where the work it
// waits on is described.
//
// A `sync_status` is one `eth_blockNumber` against whatever RPC the build was
// pointed at, over a phone's radio, and the wallet retries a refusal by the
// core's admission race six times at 500 ms (#235). 30 s covers both.
constexpr int kStatusBudgetMs = 30000;
// A window of the accumulator walk. The wallet asks for 25 000 blocks or 5 s,
// whichever comes first, and the FIRST window of a cold sync takes everything
// up to the subsquid frontier in one go -- 8 s against public Sepolia on an
// M2 simulator, and longer on a handset's radio. Three minutes is several
// windows' worth on the slowest of those, and a percentage that has not moved
// by then has not moved.
constexpr int kSyncMoveBudgetMs = 180000;
// A cancel lands after the window already in flight, which is bounded by the
// 5 s budget the wallet asked for plus one round trip.
constexpr int kCancelBudgetMs = 60000;

// wallet_ui's Advanced-tab seed import: the flow logos-workspace#147 could not
// verify on a device and the reason #174 was split out of it.
//
// A CONTROL IS NAMED BY WHICHEVER HANDLE IT HAS. The buttons here are named by
// their text, which is what Qt's accessibility tree publishes for one; the
// FIELDS are named by their `objectName`, because Qt publishes a text editor
// with no accessible name at all and the module already carries objectNames for
// the desktop inspector to find it by.
//
// The seed is the all-zero BIP-39 test vector, deliberately: it is the phrase
// every wallet test in this workspace uses, it is worthless, and it must never
// be a phrase anyone could have funded.
//
// IT LEAVES THE FORM HOLDING WHAT IT TYPED, which is why it cannot also be the
// flow that drives the Private tab: the one state a screenshot of this run is
// worth taking is the filled form.
WebDriveFlow seedImport()
{
    WebDriveFlow flow;
    flow.name = QStringLiteral("seed-import");
    flow.app = QStringLiteral("wallet_ui");
    flow.verdict = QStringLiteral("TYPED TEXT REACHES A WEB APP'S PAGE");
    flow.steps = {
        WebDriveStep::press(QStringLiteral("Advanced")),
        WebDriveStep::type(QStringLiteral("advSeedField"),
                           QStringLiteral("abandon abandon abandon abandon abandon abandon "
                                          "abandon abandon abandon abandon abandon about")),
        WebDriveStep::type(QStringLiteral("advAcctLabelField"), QStringLiteral("issue174")),
        WebDriveStep::type(QStringLiteral("advAcctPwField"), QStringLiteral("hunter2")),
        WebDriveStep::press(QStringLiteral("Import")),
        // READ BACK AFTER THE WHOLE FLOW, not as part of the step that typed
        // it: a field that still holds what was typed once the form has been
        // submitted is the module's state, and a value captured inside the
        // typing step could be the driver reading its own echo.
        WebDriveStep::read(QStringLiteral("advAcctLabelField"), QStringLiteral("issue174")),
    };
    return flow;
}

// wallet_ui's Private tab: the RAILGUN accumulator sync a private send waits
// on, started and then LEFT (logos-workspace#238, for #235's clauses 1 and 2).
//
// WHY EVERY VERDICT HERE IS A CONSOLE LINE. The private surface's state is a
// PROP the view renders -- a progress bar, a label, a note. There is no field
// holding a percentage to read back, and a bar's pixels are not a number. What
// there is, is the module's own announcement of each publication, which the
// container forwards to the app's log. So the walk is asserted from what the
// WALLET said about it.
//
// THE THREE THINGS IT PROVES, in the order they can be proved:
//   * the tab answers at all -- `Check` is one `eth_blockNumber` and the page
//     publishes a real distance to the chain head for it;
//   * the walk RUNS -- a `running:` line whose percentage has moved off the one
//     `startPrivateSync` published before it asked for a window;
//   * the cancel LEAVES SOMETHING -- `state: "cancelled"` carrying
//     `keptToBlock`. That last one is the resume contract and the one thing a
//     human looking at the screen would not notice: a cancel that keeps no
//     block has thrown away the walk it just paid minutes for, and the page
//     looks exactly the same either way.
WebDriveFlow privateSync()
{
    WebDriveFlow flow;
    flow.name = QStringLiteral("private-sync");
    flow.app = QStringLiteral("wallet_ui");
    flow.verdict =
        QStringLiteral("THE WALLET'S PRIVATE SYNC RUNS AND CAN BE LEFT, from inside the app");

    WebPageWatch distance;
    // Any state's line -- `idle`, `done` or `running` all carry the plan, and
    // which one a device is in is not this step's business. `unavailable` does
    // NOT carry it, which is exactly the case this step is here to catch.
    distance.marker = QStringLiteral("private sync ");
    distance.field = QStringLiteral("targetBlock");
    distance.want = WebPageWatch::Want::Present;
    distance.budgetMs = kStatusBudgetMs;
    distance.what = QStringLiteral("the distance to the chain head");

    WebPageWatch moved;
    moved.marker = QStringLiteral("private sync running:");
    moved.field = QStringLiteral("percent");
    moved.want = WebPageWatch::Want::Moved;
    moved.budgetMs = kSyncMoveBudgetMs;
    moved.what = QStringLiteral("a percentage that moved");

    WebPageWatch kept;
    kept.marker = QStringLiteral("private sync cancelled:");
    kept.field = QStringLiteral("keptToBlock");
    kept.want = WebPageWatch::Want::Present;
    kept.budgetMs = kCancelBudgetMs;
    kept.what = QStringLiteral("the block the cancelled walk kept");

    flow.steps = {
        WebDriveStep::press(QStringLiteral("Private")),
        // PRESSING THE TAB DOES NOT RE-READ. `selectTab()` is the doc-test
        // hook, and a TabButton pressed by a finger only moves the
        // StackLayout -- so the distance is asked for by the control a user
        // would press for it, which is also how this step proves the page's
        // buttons are reachable before the long one is pressed.
        WebDriveStep::press(QStringLiteral("Check")),
        WebDriveStep::await(distance),
        WebDriveStep::press(QStringLiteral("Sync now")),
        WebDriveStep::await(moved),
        WebDriveStep::press(QStringLiteral("Cancel")),
        WebDriveStep::await(kept),
    };
    return flow;
}

// The object a `<something>: {…}` line carries, or an empty one.
QJsonObject objectOn(const QString& line)
{
    const int brace = line.indexOf(QLatin1Char('{'));
    if (brace < 0) return {};
    return QJsonDocument::fromJson(line.mid(brace).toUtf8()).object();
}

} // namespace

WebDriveStep WebDriveStep::press(const QString& control)
{
    WebDriveStep step;
    step.act = Act::Press;
    step.control = control;
    return step;
}

WebDriveStep WebDriveStep::type(const QString& control, const QString& text)
{
    WebDriveStep step;
    step.act = Act::Type;
    step.control = control;
    step.text = text;
    return step;
}

WebDriveStep WebDriveStep::read(const QString& control, const QString& holds)
{
    WebDriveStep step;
    step.act = Act::Read;
    step.control = control;
    step.text = holds;
    return step;
}

WebDriveStep WebDriveStep::await(const WebPageWatch& watch)
{
    WebDriveStep step;
    step.act = Act::Await;
    step.watch = watch;
    return step;
}

std::optional<QString> WebPageWatcher::readingOf(const QString& line, const QString& marker,
                                                 const QString& field)
{
    if (marker.isEmpty() || !line.contains(marker)) return std::nullopt;
    const QJsonValue value = objectOn(line).value(field);
    // ABSENT AND NULL ARE THE SAME ANSWER. `etaMs` is null until a window has
    // completed and `keptToBlock` is absent on a cancel that had no plan to
    // drop; neither is a reading, and a watch that accepted one would report a
    // number that is not there.
    if (value.isUndefined() || value.isNull()) return std::nullopt;
    return value.toVariant().toString();
}

WebPageWatcher::WebPageWatcher(WebPageWatch watch)
    : m_watch(std::move(watch))
{
}

bool WebPageWatcher::offer(const QString& line)
{
    if (m_settled) return true;
    if (m_watch.marker.isEmpty() || !line.contains(m_watch.marker)) return false;
    ++m_linesSeen;
    const std::optional<QString> value = readingOf(line, m_watch.marker, m_watch.field);
    if (!value) return false;
    if (m_watch.want == WebPageWatch::Want::Present) {
        m_baseline = *value;
        m_haveBaseline = true;
        m_reading = *value;
        m_settled = true;
        return true;
    }
    // Moved: the first reading is the thing to move AWAY from, and is never
    // itself the answer.
    if (!m_haveBaseline) {
        m_baseline = *value;
        m_haveBaseline = true;
        return false;
    }
    if (*value == m_baseline) return false;
    m_reading = *value;
    m_settled = true;
    return true;
}

QList<WebDriveFlow> WebDriveFlows::forApp(const QString& app)
{
    if (app == QLatin1String("wallet_ui"))
        return { seedImport(), privateSync() };
    return {};
}

QStringList WebDriveFlows::namesFor(const QString& app)
{
    QStringList names;
    for (const WebDriveFlow& flow : forApp(app))
        names << flow.name;
    return names;
}

WebDriveFlow WebDriveFlows::select(const QString& app, const QString& name)
{
    const QList<WebDriveFlow> flows = forApp(app);
    if (flows.isEmpty()) return {};
    if (name.isEmpty()) return flows.first();
    for (const WebDriveFlow& flow : flows)
        if (flow.name == name)
            return flow;
    return {};
}

} // namespace basecamp::shell
