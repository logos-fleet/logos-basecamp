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
// line `startPrivateSync` writes before it asks for the first window -- but the
// host cannot READ it until the window in flight is over, so this budget has to
// cover that window, not a page round trip.
constexpr int kAcceptedBudgetMs = 180000;
// HOW LONG THE PAGE WAITS BEFORE PRESSING CANCEL, measured on its OWN thread.
// Long enough that `Sync now` has certainly been pressed and the view has
// re-evaluated the Cancel button's `enabled: state === "running"`; short enough
// to be inside the first `sync_step`, which on this venue's iPad simulator
// against public Sepolia is the whole 11.7 M-block history in 3.6 s.
constexpr int kCancelAfterMs = 1200;
// A CANCEL LANDS AFTER THE WINDOW ALREADY IN FLIGHT, and that window is the
// expensive one: `sync_step` takes everything up to the subsquid frontier in a
// single window however far away it is. Measured 3.7 s for the whole 11.7 M
// block Sepolia history on an M2 simulator, and minutes on a handset's radio
// against a chain with more in it. Three minutes covers both.
constexpr int kCancelBudgetMs = 180000;
// ...and by then every line is already in hand, because this watch reads the
// whole flow. It is a scan of what happened, not a wait.
constexpr int kSyncMoveBudgetMs = 30000;

// THE SHIELD'S BUDGETS. Its plan is `prepare_shield` (pure calldata, no
// network) plus `get_transaction_count` and `gas_price` -- two ordinary RPCs
// over a phone's radio -- and then one `request_approval` against a module in
// the same image. A minute covers all of it with the admission retry in it.
constexpr int kShieldRouteBudgetMs = 60000;
// HOW LONG THE PAGE WAITS BEFORE PRESSING CANCEL. Generous rather than tuned:
// the cancel is correct wherever in the plan it lands (see the flow), so this
// only has to be long enough that `Shield` has certainly been pressed and short
// enough that a run is not waiting on it.
constexpr int kShieldCancelAfterMs = 4000;
// A cancel during `sign` is one call to a module in the same image; a cancel
// during `plan` waits for the RPC already in flight. A minute covers both.
constexpr int kShieldCancelBudgetMs = 60000;
// A scan of what happened, like the sync's.
constexpr int kShieldMoveBudgetMs = 30000;

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
// BOTH PRESSES GO IN BEFORE THE HOST STOPS ANSWERING, and the assertions come
// after. That order is not a preference, it is what two device runs measured on
// an iPad Air 13-inch (M2) simulator against public Sepolia:
//
//   * the whole 11.7 M-block COLD sync is 3.6 s and TWO windows -- the subsquid
//     frontier in one and a ~1 300-block tail in the other -- so the page
//     publishes `running` at 0 % and `done` at 100 % with nothing between them.
//     A flow that waits for a moved percentage before pressing Cancel waits for
//     a walk that is already over, and then presses a Cancel the view has
//     disabled (`enabled: state === "running"`);
//   * and worse, THE HOST CANNOT ACT AT ALL WHILE THAT WINDOW RUNS. A `web`
//     module's `sync_step` crosses to the host, which answers it synchronously
//     on the thread this driver's own loop turns -- so the page's "pressed
//     'Sync now'" reached the host 3.6 s before the host could read it, and a
//     Cancel sent on the strength of it is 3.6 s too late. Measured: the
//     `pressed 'Cancel'` line lands AFTER `private sync done:`.
//
// So `Sync now` and `Cancel` are both armed in the page, the page fires the
// second one on its own timer 1.2 s later, and what they did is read off the
// module's announcements afterwards. It works on a device where the walk is
// seconds and on one where it is minutes: `railgun_module` is
// `concurrency: single`, so the cancel queues behind the window in flight, and
// the wallet checks its cancel flag BEFORE the `done` flag when that window
// lands -- so a cancelled walk publishes `cancelled` and not `done`.
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
        WebDriveStep::pressAhead(QStringLiteral("Sync now"), 0),
        WebDriveStep::pressAhead(QStringLiteral("Cancel"), kCancelAfterMs),
        WebDriveStep::await(accepted),
        WebDriveStep::await(kept),
        WebDriveStep::await(moved),
    };
    return flow;
}

// wallet_ui's Private tab, the OTHER direction: the shield that puts public
// funds INTO the pool, started and then LEFT (logos-workspace#235, clauses 1
// and 2 for `wrap` / `approve` / `shield`).
//
// WHAT THIS CAN CHECK ON A DEVICE WITH NO MONEY IN IT, which is the whole
// design of the flow. The route is `plan` -> `sign` -> `wrap` -> `approve` ->
// `shield`, and only the last three spend anything. `plan` is
// `railgun_module.prepare_shield` (pure calldata, no network) plus two eth_rpc
// reads; `sign` lodges one approval request with `keystore_module` and then
// waits for a human in the Signer app -- which is not installed on this venue's
// simulators, so the route parks there indefinitely and NOTHING is ever signed
// or broadcast. That park is exactly where clause 2's interesting answer lives:
// a cancel here withdraws the request and leaves nothing on chain, and the
// wallet says so in a `note`.
//
// SO THE FLOW IS SAFE BY CONSTRUCTION, not by being careful with an amount. It
// cannot reach a transaction because there is no approver to let it.
//
// BOTH PRESSES ARE ARMED BEFORE ANYTHING IS ASSERTED, for the same reason the
// sync flow's are: a `web` module's outbound call is answered SYNCHRONOUSLY on
// the thread this driver's loop turns, so the host cannot read the page's
// "pressed 'Shield'" until the plan's RPC reads are done. The page fires the
// second press on its own timer.
//
// THE CANCEL IS CORRECT WHEREVER IT LANDS, which is why the delay is generous
// rather than tuned. During `plan` the wallet sets a flag and the reply in
// flight finds it; during `sign` it withdraws the request it has a handle for;
// between the two -- the request lodged but the reply not yet delivered -- the
// reply itself withdraws it. All three ends publish `cancelled` with the same
// note.
//
// THE BUTTONS ARE PRESSED BY objectName AND NOT BY THEIR TEXT. The accessible
// name is matched case-insensitively FROM THE FRONT, and this tab now has a
// "Shield" button under a "Shield into the pool" heading and three controls
// whose names begin with "Cancel". An objectName is asked of the QML runtime
// directly and is exact.
WebDriveFlow privateShield()
{
    WebDriveFlow flow;
    flow.name = QStringLiteral("private-shield");
    flow.app = QStringLiteral("wallet_ui");
    flow.verdict = QStringLiteral(
        "THE WALLET PLANS A SHIELD AND CAN BE LEFT BEFORE ANYTHING IS SIGNED, from inside "
        "the app");

    WebPageWatch onRoute;
    onRoute.marker = QStringLiteral("private shield running:");
    onRoute.field = QStringLiteral("leg");
    onRoute.want = WebPageWatch::Want::Present;
    onRoute.budgetMs = kShieldRouteBudgetMs;
    onRoute.what = QStringLiteral("the leg the shield is on");

    WebPageWatch left;
    left.marker = QStringLiteral("private shield cancelled:");
    left.field = QStringLiteral("note");
    left.want = WebPageWatch::Want::Present;
    left.budgetMs = kShieldCancelBudgetMs;
    left.what = QStringLiteral("what leaving the shield left behind");

    WebPageWatch moved;
    // THE SURFACE MOVED, which is #235's "the app has not hung" stated as
    // something a driver can read: more than one state was published for one
    // shield. Over the whole flow, because on a fast device every line is
    // already behind the cursor by the time the cancel lands.
    moved.marker = QStringLiteral("private shield ");
    moved.field = QStringLiteral("state");
    moved.want = WebPageWatch::Want::Moved;
    moved.overTheWholeFlow = true;
    moved.budgetMs = kShieldMoveBudgetMs;
    moved.what = QStringLiteral("a route that published more than one state");

    flow.steps = {
        WebDriveStep::press(QStringLiteral("Private")),
        // Sepolia WETH, and an amount in base units small enough to be dust if
        // this ever DID reach a chain. Neither is load-bearing: the route cannot
        // get past `sign` without an approver.
        WebDriveStep::type(QStringLiteral("privateShieldAssetField"),
                           QStringLiteral("0xfFf9976782d46CC05630D1f6eBAb18b2324d6B14")),
        WebDriveStep::type(QStringLiteral("privateShieldAmountField"), QStringLiteral("1000")),
        WebDriveStep::pressAhead(QStringLiteral("privateShieldButton"), 0),
        WebDriveStep::pressAhead(QStringLiteral("privateShieldCancelButton"),
                                 kShieldCancelAfterMs),
        WebDriveStep::await(onRoute),
        WebDriveStep::await(left),
        WebDriveStep::await(moved),
    };
    return flow;
}

// ── #250's two other screens ─────────────────────────────────────────────────
//
// IMPORTING A SEED IS THE SLOW STEP, not the tab. `import_mnemonic` derives a
// key and writes a scrypt vault, in wasm, on a tablet -- and it runs behind the
// keystore's Tier D chain (`caller_identity`, `configure`) with the core's
// admission retry in front of that. Two minutes rather than a measured number:
// nothing here is waiting on the wallet being quick, only on it answering.
constexpr int kAccountBudgetMs = 120000;
// One call to a module in the same app image, which keeps the record locally
// and may read receipts over the radio to freshen it. A minute covers both.
constexpr int kHistoryBudgetMs = 60000;
// The same shape, and shorter work: `set_proxy_config` is a document handed to
// the coordinator, which answers a bare bool.
constexpr int kProxyBudgetMs = 60000;

// THE PROXY THIS TYPES. A Tor SOCKS endpoint on the device's own loopback, and
// nothing is dialled by applying it: `set_proxy_config` stores the setting and
// pushes it into eth_rpc's chains. It is the placeholder the field itself
// suggests, so a screenshot of the run reads as the tab's own example.
const QString kProxyUrl = QStringLiteral("socks5h://127.0.0.1:9050");

// ...and what the wallet's settings line then holds. `proxyApplied()` in
// src/wallet_ui_web_backend.cpp, for a proxy that is not required -- this flow
// leaves the fail-closed box alone. A readback of a control is the strongest
// verdict this driver has and it is also a COUPLING: change that sentence and
// this flow says the keys did not reach the field. Which is the trade this one
// step is worth making, because `proxyStatus` is the module's own state.
const QString kProxyApplied =
    QStringLiteral("Proxy applied: %1 (optional)").arg(kProxyUrl);

// wallet_ui's History tab: "History → Refresh history returns data", the first
// of the three failures logos-workspace#250 was reported with.
//
// WHY IT IMPORTS AN ACCOUNT FIRST. `Refresh history` is disabled until the
// wallet holds one (`enabled: root.ready && acctBox.currentText.length > 0`),
// so on a freshly installed app this flow would press a dead button and then
// wait out a budget for an answer nobody was asked for. The seed is the all-zero
// BIP-39 test vector for the same reason the seed-import flow uses it: it is
// worthless, every wallet test in this workspace uses it, and it must never be a
// phrase anyone could have funded. Importing it twice is the same key again.
//
// THE GATE IS THE WALLET'S OWN ACCOUNT LINE, not a sleep: `accounts now:`
// carries the account the tabs will ask about, and it is published when
// `list_accounts` answered -- which is after the import landed.
//
// AND THE VERDICT IS THE ANSWER, NOT THE ASK. The wallet announces
// `refreshHistory: asking wallet_backend_module …` before it knows anything,
// and it announced that on the operator's iPad too -- the ask is exactly what
// #250 fixed and exactly what cannot tell a run that WORKED from a run that was
// refused. `history updated:` is published only on a reply the coordinator
// answered, and `rows` is the reading rather than its emptiness: an account with
// no transactions in it is an answer, and it is the answer a device with a
// fresh keystore gives.
WebDriveFlow history()
{
    WebDriveFlow flow;
    flow.name = QStringLiteral("history");
    flow.app = QStringLiteral("wallet_ui");
    flow.verdict =
        QStringLiteral("THE HISTORY TAB IS ANSWERED BY wallet_backend_module, on the device");

    WebPageWatch account;
    account.marker = QStringLiteral("accounts now:");
    account.field = QStringLiteral("selected");
    account.want = WebPageWatch::Want::Present;
    account.budgetMs = kAccountBudgetMs;
    account.what = QStringLiteral("the account the tabs will ask about");

    WebPageWatch answered;
    answered.marker = QStringLiteral("history updated:");
    answered.field = QStringLiteral("rows");
    answered.want = WebPageWatch::Want::Present;
    answered.budgetMs = kHistoryBudgetMs;
    answered.what = QStringLiteral("the coordinator's answer, and how many rows were in it");

    flow.steps = {
        WebDriveStep::press(QStringLiteral("Advanced")),
        WebDriveStep::type(QStringLiteral("advSeedField"),
                           QStringLiteral("abandon abandon abandon abandon abandon abandon "
                                          "abandon abandon abandon abandon abandon about")),
        WebDriveStep::type(QStringLiteral("advAcctLabelField"), QStringLiteral("issue250")),
        WebDriveStep::type(QStringLiteral("advAcctPwField"), QStringLiteral("hunter2")),
        // BY objectName. The accessible name is matched from the front and this
        // button sits under an "Import account (seed phrase)" heading, which
        // matches "Import" just as well and is not a button.
        WebDriveStep::press(QStringLiteral("advImportButton")),
        WebDriveStep::await(account),
        WebDriveStep::press(QStringLiteral("History")),
        WebDriveStep::press(QStringLiteral("historyRefreshButton")),
        WebDriveStep::await(answered),
    };
    return flow;
}

// wallet_ui's Settings tab: "Advanced → Proxy config opens", the second of the
// three. The operator's tab answered "Proxy config needs wallet_backend_module"
// and made no call at all.
//
// TWO CLAIMS, AND THE SECOND IS THE ONE A PERSON WOULD HAVE MADE. The console
// line says the coordinator took the document and carries what was applied; the
// READBACK says the TAB shows it. `proxyStatus` is a property the backend has
// always published and the view rendered nowhere, so pressing Apply changed the
// screen in no way at all -- applied, refused or, before #250, never asked for.
// A flow that only read the console would have left that unnoticed.
WebDriveFlow proxyConfig()
{
    WebDriveFlow flow;
    flow.name = QStringLiteral("proxy-config");
    flow.app = QStringLiteral("wallet_ui");
    flow.verdict = QStringLiteral(
        "THE WALLET'S PROXY SETTING REACHES wallet_backend_module AND THE TAB SAYS SO");

    WebPageWatch applied;
    applied.marker = QStringLiteral("proxy applied:");
    applied.field = QStringLiteral("proxy");
    applied.want = WebPageWatch::Want::Present;
    applied.budgetMs = kProxyBudgetMs;
    applied.what = QStringLiteral("the proxy the coordinator accepted");

    flow.steps = {
        WebDriveStep::press(QStringLiteral("Settings")),
        WebDriveStep::type(QStringLiteral("proxyUrlField"), kProxyUrl),
        WebDriveStep::press(QStringLiteral("proxyApplyButton")),
        WebDriveStep::await(applied),
        WebDriveStep::read(QStringLiteral("proxyStatusText"), kProxyApplied),
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

WebDriveStep WebDriveStep::pressAhead(const QString& control, int afterMs)
{
    WebDriveStep step;
    step.act = Act::PressAhead;
    step.control = control;
    step.afterMs = afterMs;
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
        return { seedImport(), privateSync(), privateShield(), history(), proxyConfig() };
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
