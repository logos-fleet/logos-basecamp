// srcdeps: basecamp-shell/src/ViewDependencies.cpp
//
// WHAT HAS TO BE RUNNING BEFORE A NATIVE VIEW IS MOUNTED (logos-workspace#205).
//
// The rule, and the defect it closes, are ViewDependencies.h. It is a pure
// function of the facts precisely so that it can be decided here: on a phone
// "the app came up dead" is six minutes and a hand-load away from the evidence,
// and the app works the moment you load the module by hand -- which is what
// made this look like a chat defect for as long as it did.
//
// Run: nix build .#unit-tests -L

#include "basecamp-shell/src/ViewDependencies.h"

#include <QtTest/QtTest>

using basecamp::shell::ModuleFacts;
using basecamp::shell::bringUpViewDependencies;

namespace {

QVariantMap member(const QString& name, const QString& type,
                   const QStringList& dependencies = { })
{
    QVariantMap m;
    m[QStringLiteral("name")] = name;
    m[QStringLiteral("type")] = type;
    m[QStringLiteral("version")] = QStringLiteral("1.0.0");
    m[QStringLiteral("dependencies")] = dependencies;
    return m;
}

// The set the iPad was running when #205 was found: a view module, the module
// it declares, and the module that one pulls in behind it.
ModuleFacts chatSet()
{
    ModuleFacts facts;
    facts.bundledSet = {
        member(QStringLiteral("capability_module"), QStringLiteral("core")),
        member(QStringLiteral("delivery_module"), QStringLiteral("core")),
        member(QStringLiteral("chat_module"), QStringLiteral("core"),
               { QStringLiteral("delivery_module") }),
        member(QStringLiteral("chat_ui"), QStringLiteral("ui_qml"),
               { QStringLiteral("chat_module"), QStringLiteral("delivery_module") }),
    };
    // Every non-view member is registered at start(); only the three the
    // Shell's own surfaces need are LOADED.
    facts.known  = { QStringLiteral("capability_module"), QStringLiteral("delivery_module"),
                     QStringLiteral("chat_module") };
    facts.loaded = { QStringLiteral("capability_module") };
    return facts;
}

} // namespace

class ViewDependenciesTest : public QObject
{
    Q_OBJECT

private slots:
    // THE DEFECT ITSELF. Mounting chat_ui on a plain launch has to bring
    // chat_module up first, in the order the manifest declares it.
    void bringsUpWhatTheViewDeclares();
    // ...and nothing else. A view whose dependency is already running costs
    // no load: the second tile press must not re-enter a live module.
    void skipsWhatIsAlreadyRunning();
    // A member with no `dependencies` is not a failure, it is a view that
    // needs nothing.
    void viewWithNoDependenciesIsReady();
    // A name the manifest does not carry has no declared dependencies to read,
    // and refusing the mount over that would take every Downloaded app off the
    // screen.
    void unknownMemberIsReady();
    // GENUINELY ABSENT, which is the case the user has to be told about: the
    // manifest declares a module and the core has never heard of it. The mount
    // is refused and the missing name is named.
    void absentDependencyRefusesTheMount();
    // Here and will not come up -- an image that is present and broken. Also a
    // refusal, and told apart from the one above because they are different
    // instructions to whoever reads the screen.
    void refusedDependencyRefusesTheMount();
    // The loads happen in manifest order, and the verdict reports them in the
    // order they were actually run.
    void loadsInManifestOrder();
};

void ViewDependenciesTest::bringsUpWhatTheViewDeclares()
{
    ModuleFacts facts = chatSet();
    QStringList asked;
    const auto verdict = bringUpViewDependencies(facts, QStringLiteral("chat_ui"),
                                                 [&](const QString& name) {
                                                     asked << name;
                                                     facts.loaded << name;
                                                     return true;
                                                 });
    QVERIFY(verdict.ready);
    QCOMPARE(asked, QStringList({ QStringLiteral("chat_module"),
                                  QStringLiteral("delivery_module") }));
    QCOMPARE(verdict.loaded, asked);
    QVERIFY(verdict.missing.isEmpty());
    QVERIFY(verdict.refused.isEmpty());
}

void ViewDependenciesTest::skipsWhatIsAlreadyRunning()
{
    ModuleFacts facts = chatSet();
    facts.loaded << QStringLiteral("chat_module") << QStringLiteral("delivery_module");
    QStringList asked;
    const auto verdict = bringUpViewDependencies(facts, QStringLiteral("chat_ui"),
                                                 [&](const QString& name) {
                                                     asked << name;
                                                     return true;
                                                 });
    QVERIFY(verdict.ready);
    QVERIFY(asked.isEmpty());
    QVERIFY(verdict.loaded.isEmpty());
}

void ViewDependenciesTest::viewWithNoDependenciesIsReady()
{
    ModuleFacts facts;
    facts.bundledSet = { member(QStringLiteral("view_counter"), QStringLiteral("ui_qml")) };
    int calls = 0;
    const auto verdict = bringUpViewDependencies(facts, QStringLiteral("view_counter"),
                                                 [&](const QString&) { ++calls; return true; });
    QVERIFY(verdict.ready);
    QCOMPARE(calls, 0);
}

void ViewDependenciesTest::unknownMemberIsReady()
{
    const ModuleFacts facts = chatSet();
    int calls = 0;
    const auto verdict = bringUpViewDependencies(facts, QStringLiteral("wallet_ui"),
                                                 [&](const QString&) { ++calls; return true; });
    QVERIFY(verdict.ready);
    QCOMPARE(calls, 0);
}

void ViewDependenciesTest::absentDependencyRefusesTheMount()
{
    ModuleFacts facts = chatSet();
    facts.known.removeAll(QStringLiteral("chat_module"));
    QStringList asked;
    const auto verdict = bringUpViewDependencies(facts, QStringLiteral("chat_ui"),
                                                 [&](const QString& name) {
                                                     asked << name;
                                                     facts.loaded << name;
                                                     return true;
                                                 });
    QVERIFY(!verdict.ready);
    QCOMPARE(verdict.missing, QStringList({ QStringLiteral("chat_module") }));
    // An absent dependency is not offered to the loader: there is nothing to
    // load, and asking the core would report a failure that means something
    // else.
    QCOMPARE(asked, QStringList({ QStringLiteral("delivery_module") }));
}

void ViewDependenciesTest::refusedDependencyRefusesTheMount()
{
    const ModuleFacts facts = chatSet();
    const auto verdict = bringUpViewDependencies(facts, QStringLiteral("chat_ui"),
                                                 [](const QString& name) {
                                                     return name != QStringLiteral("chat_module");
                                                 });
    QVERIFY(!verdict.ready);
    QVERIFY(verdict.missing.isEmpty());
    QCOMPARE(verdict.refused, QStringList({ QStringLiteral("chat_module") }));
}

void ViewDependenciesTest::loadsInManifestOrder()
{
    ModuleFacts facts;
    facts.bundledSet = {
        member(QStringLiteral("a"), QStringLiteral("core")),
        member(QStringLiteral("b"), QStringLiteral("core")),
        member(QStringLiteral("c"), QStringLiteral("core")),
        // Declaration order, NOT the manifest's: a view names what it calls,
        // and the closure below each name is the core's to walk.
        member(QStringLiteral("z_ui"), QStringLiteral("ui_qml"),
               { QStringLiteral("c"), QStringLiteral("a"), QStringLiteral("b") }),
    };
    facts.known = { QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c") };
    QStringList asked;
    const auto verdict = bringUpViewDependencies(facts, QStringLiteral("z_ui"),
                                                 [&](const QString& name) {
                                                     asked << name;
                                                     return true;
                                                 });
    QVERIFY(verdict.ready);
    QCOMPARE(asked, QStringList({ QStringLiteral("c"), QStringLiteral("a"),
                                  QStringLiteral("b") }));
}

QTEST_MAIN(ViewDependenciesTest)
#include "view_dependencies_test.moc"
