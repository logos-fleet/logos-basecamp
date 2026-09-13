// srcdeps: appmanager/ModuleCallScript.cpp
#include "appmanager/ModuleCallScript.h"

#include <QtTest>

using basecamp::appmanager::ModuleCall;
using basecamp::appmanager::ModuleCallScript;

// The on-device `logoscore call`, as it is read off a phone app's command line.
class ModuleCallScriptTest : public QObject
{
    Q_OBJECT

    static ModuleCallScript parse(const QStringList& tail)
    {
        return ModuleCallScript::fromArguments(QStringList{ "BasecampShell" } + tail);
    }

private slots:
    void anOrdinaryLaunchAsksForNothing()
    {
        const ModuleCallScript s = parse({ "--repository", "http://127.0.0.1:8080/" });
        QVERIFY(s.isEmpty());
        QVERIFY(s.calls().isEmpty());
        QVERIFY(s.refusals().isEmpty());
    }

    void aMethodWithNoArgumentsNeedsNoParentheses()
    {
        const ModuleCallScript s = parse({ "--call", "keystore_module.list_accounts" });
        QCOMPARE(s.calls().size(), 1);
        QCOMPARE(s.calls().at(0).module, QString("keystore_module"));
        QCOMPARE(s.calls().at(0).method, QString("list_accounts"));
        QVERIFY(s.calls().at(0).args.isEmpty());

        const ModuleCallScript empty = parse({ "--call", "keystore_module.list_accounts()" });
        QCOMPARE(empty.calls().size(), 1);
        QVERIFY(empty.calls().at(0).args.isEmpty());
    }

    // THE WHOLE REASON THE TYPES ARE WRITTEN DOWN. logoscore infers one from the
    // spelling, so an 0x address is a number to it and every address-taking
    // method answers null with status ok (#106). Here the default is a string
    // and nothing about a value's shape can change that.
    void aBareArgumentIsAString()
    {
        const ModuleCallScript s = parse(
            { "--call", "keystore_module.unlock(0x8ad0Fcf71D6FBD060BAfd45f5155b1e52d3591C5,pw)" });
        QCOMPARE(s.calls().size(), 1);
        const QVariantList args = s.calls().at(0).args;
        QCOMPARE(args.size(), 2);
        QVERIFY(args.at(0).typeId() == QMetaType::QString);
        QCOMPARE(args.at(0).toString(),
                 QString("0x8ad0Fcf71D6FBD060BAfd45f5155b1e52d3591C5"));
        QCOMPARE(args.at(1).toString(), QString("pw"));

        // ...and a number-looking one is a string too, unless it says int:.
        const ModuleCallScript n = parse({ "--call", "m.f(42)" });
        QVERIFY(n.calls().at(0).args.at(0).typeId() == QMetaType::QString);
    }

    void theOtherThreeTypesAreAsked()
    {
        const ModuleCallScript s = parse(
            { "--call", R"(m.f(int:42,bool:true,json:{"chainId":1,"tags":[1,2]},str:0x2a))" });
        QVERIFY2(s.refusals().isEmpty(), qPrintable(s.refusals().join("; ")));
        const QVariantList args = s.calls().at(0).args;
        QCOMPARE(args.size(), 4);
        QCOMPARE(args.at(0).toLongLong(), 42LL);
        QVERIFY(args.at(1).typeId() == QMetaType::Bool);
        QCOMPARE(args.at(1).toBool(), true);
        // The commas INSIDE the JSON are the JSON's, not the argument list's.
        const QVariantMap map = args.at(2).toMap();
        QCOMPARE(map.value("chainId").toInt(), 1);
        QCOMPARE(map.value("tags").toList().size(), 2);
        QCOMPARE(args.at(3).toString(), QString("0x2a"));
    }

    void callsRunInTheOrderTheyWereGiven()
    {
        const ModuleCallScript s = parse({ "--call", "a.one", "--call", "b.two",
                                           "--call", "a.three" });
        QCOMPARE(s.calls().size(), 3);
        QCOMPARE(s.calls().at(0).method, QString("one"));
        QCOMPARE(s.calls().at(1).method, QString("two"));
        QCOMPARE(s.calls().at(2).method, QString("three"));
        // ...and each module is named once, in first-mention order: that is
        // what has to be loaded before any of it can run.
        QCOMPARE(s.modules(), QStringList({ "a", "b" }));
    }

    // A phone's whole diagnostic surface is one console. A flag that silently
    // did nothing reads exactly like a module that did not answer.
    void everyRefusalIsKeptAndNamed()
    {
        const ModuleCallScript s = parse({ "--call", "nodot",
                                           "--call", "m.f(int:notanumber)",
                                           "--call", "m.f(",
                                           "--call", "m.f(json:{)",
                                           "--call" });
        QVERIFY(s.calls().isEmpty());
        QCOMPARE(s.refusals().size(), 5);
        QVERIFY(!s.isEmpty());
        for (const QString& refusal : s.refusals())
            QVERIFY2(!refusal.isEmpty(), "a refusal with no sentence is not a report");
    }

    void aGoodCallSurvivesABadNeighbour()
    {
        const ModuleCallScript s = parse({ "--call", "m.bad(int:x)",
                                           "--call", "m.good(hello)" });
        QCOMPARE(s.calls().size(), 1);
        QCOMPARE(s.calls().at(0).method, QString("good"));
        QCOMPARE(s.refusals().size(), 1);
    }
};

QTEST_MAIN(ModuleCallScriptTest)
#include "module_call_script_test.moc"
