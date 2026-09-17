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
// The wallet publishing `running` is synchronous with the press -- it is the
// line `startPrivateSync` writes before it asks for the first window -- so this
// is a page round trip and not a chain walk.
constexpr int kAcceptedBudgetMs = 30000;
// A CANCEL LANDS AFTER THE WINDOW ALREADY IN FLIGHT, and that window is the
// expensive one: `sync_step` takes everything up to the subsquid frontier in a
// single window however far away it is. Measured 3.7 s for the whole 11.7 M
// block Sepolia history on an M2 simulator, and minutes on a handset's radio
// against a chain with more in it. Three minutes covers both.
constexpr int kCancelBudgetMs = 180000;
// ...and by then every line is already in hand, because this watch reads the
// whole flow. It is a scan of what happened, not a wait.
constexpr int kSyncMoveBudgetMs = 30000;

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
// THE FOUR THINGS IT PROVES:
//   * the tab answers at all -- `Check` is one `eth_blockNumber` and the page
//     publishes a real distance to the chain head for it;
//   * the walk was ACCEPTED -- `startPrivateSync` published `running`, which is
//     also what enables the Cancel button;
//   * the cancel LEAVES SOMETHING -- `state: "cancelled"` carrying
//     `keptToBlock`. That is the resume contract and the one thing a human
//     looking at the screen would not notice: a cancel that keeps no block has
//     thrown away the walk it just paid minutes for, and the page looks exactly
//     the same either way;
//   * and the percentage MOVED while it ran.
//
// THE CANCEL IS PRESSED THE MOMENT THE WALK IS ACCEPTED, and the movement is
// asserted after it rather than before. That order is not a preference, it is
// what a device measured: on an iPad Air 13-inch (M2) simulator against public
// Sepolia the whole 11.7 M-block cold sync is 3.7 s and TWO windows -- the
// subsquid frontier in one and a 1 300-block tail in the other -- so the page
// publishes `running` at 0 % and `done` at 100 % with nothing in between.
// Waiting for a moved percentage BEFORE pressing Cancel therefore waits for a
// walk that has already finished, and then presses a Cancel the view has
// disabled. Pressing it while the window is in flight works on both a device
// where the walk is seconds and one where it is minutes: `railgun_module` is
// `concurrency: single`, so the cancel queues behind the window and the wallet
// checks the cancel flag BEFORE the `done` flag when the window lands.
//
// The movement is then read over the WHOLE flow (`overTheWholeFlow`), because
// on the fast device every line that carried it -- including the `cancelled:`
// line's own `percent` -- is published inside those 3.7 s.
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

    WebPageWatch accepted;
    accepted.marker = QStringLiteral("private sync running:");
    accepted.field = QStringLiteral("state");
    accepted.want = WebPageWatch::Want::Present;
    accepted.budgetMs = kAcceptedBudgetMs;
    accepted.what = QStringLiteral("a walk that started");

    WebPageWatch kept;
    kept.marker = QStringLiteral("private sync cancelled:");
    kept.field = QStringLiteral("keptToBlock");
    kept.want = WebPageWatch::Want::Present;
    kept.budgetMs = kCancelBudgetMs;
    kept.what = QStringLiteral("the block the cancelled walk kept");

    WebPageWatch moved;
    // EVERY STATE'S LINE, because the number this is about is one PROP and the
    // state beside it is not what moved it. On the fast device the reading that
    // settles this is the `cancelled:` line's own `percent`.
    moved.marker = QStringLiteral("private sync ");
    moved.field = QStringLiteral("percent");
    moved.want = WebPageWatch::Want::Moved;
    moved.overTheWholeFlow = true;
    moved.budgetMs = kSyncMoveBudgetMs;
    moved.what = QStringLiteral("a percentage that moved while the walk ran");

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
        WebDriveStep::await(accepted),
        WebDriveStep::press(QStringLiteral("Cancel")),
        WebDriveStep::await(kept),
        WebDriveStep::await(moved),
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
