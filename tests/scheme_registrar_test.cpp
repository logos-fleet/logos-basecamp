// srcdeps: links/SchemeRegistrar.cpp links/LinkUrl.cpp
//
// LOGOS_NO_SCHEME_REGISTER's value parsing.
//
// This existed as a bare qEnvironmentVariableIsSet, which made `=0` SKIP
// registration — a flag whose off switch turns it on. It fails in the direction
// hardest to notice: links quietly stop working, and the variable you set to
// fix that is the thing breaking it. The table below is the whole reason the
// parsing was split out of the anonymous namespace.

#include "links/SchemeRegistrar.h"

#include <QTest>

class TestSchemeRegistrar : public QObject
{
    Q_OBJECT

private slots:
    void testValueMeansSkip_data();
    void testValueMeansSkip();
};

void TestSchemeRegistrar::testValueMeansSkip_data()
{
    QTest::addColumn<QString>("value");
    QTest::addColumn<bool>("skip");

    // Off — "register normally".
    QTest::newRow("empty")        << QString()                    << false;
    QTest::newRow("zero")         << QStringLiteral("0")          << false;
    QTest::newRow("false")        << QStringLiteral("false")      << false;
    QTest::newRow("FALSE")        << QStringLiteral("FALSE")      << false;
    QTest::newRow("no")           << QStringLiteral("no")         << false;
    QTest::newRow("off")          << QStringLiteral("off")        << false;
    QTest::newRow("padded false") << QStringLiteral("  False  ")  << false;

    // On — skip registration.
    QTest::newRow("one")          << QStringLiteral("1")          << true;
    QTest::newRow("true")         << QStringLiteral("true")       << true;
    QTest::newRow("YES")          << QStringLiteral("YES")        << true;
    // Anything unrecognised is ON. A typo means the flag you were reaching for
    // takes effect, rather than silently doing nothing.
    QTest::newRow("garbage")      << QStringLiteral("ture")       << true;
}

void TestSchemeRegistrar::testValueMeansSkip()
{
    QFETCH(QString, value);
    QFETCH(bool, skip);
    QCOMPARE(SchemeRegistrar::valueMeansSkip(value), skip);
}

QTEST_MAIN(TestSchemeRegistrar)
#include "scheme_registrar_test.moc"
