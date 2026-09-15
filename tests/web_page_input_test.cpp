// srcdeps: webview/WebPageInput.cpp
//
// DRIVING A `web` APP'S PAGE (logos-workspace#174) -- the half that can be
// stated without a webview.
//
// What the driver is FOR needs a device: a real key event crossing a container
// into a Qt-wasm page, and a module's own field holding what it typed. What it
// must not get wrong is smaller and belongs here -- the call it builds for a
// control whose name carries a quote, and the reading of the one line of
// console vocabulary the page answers in. Both are what stands between "the
// page said no such control" and a driver that reports a pass because it
// matched the wrong line.
#include "webview/WebPageInput.h"

#include <QtTest>

using basecamp::web::WebPageInput;

class WebPageInputTest : public QObject
{
    Q_OBJECT

private slots:
    // The script is what a container injects, so a caller can send it before
    // every call without keeping track of which page has it.
    void theScriptInstallsItselfOnce()
    {
        const QString script = WebPageInput::driverScript();
        QVERIFY(script.contains(QLatin1String("if (window.logosDrive) return;")));
        QVERIFY(script.contains(QLatin1String("window.logosDrive = {")));
    }

    // It wakes Qt's accessibility tree, which is the whole reason a page can be
    // driven by name at all: the tree is EMPTY until the hidden button Qt
    // leaves for a screen-reader user is clicked.
    void theScriptWakesTheAccessibilityTree()
    {
        const QString script = WebPageInput::driverScript();
        QVERIFY(script.contains(QLatin1String(".hidden-visually-read-by-screen-reader")));
        QVERIFY(script.contains(QLatin1String(".qt-window-a11y-container")));
    }

    void theCallsNameTheControl()
    {
        QCOMPARE(WebPageInput::pressCall(QStringLiteral("Advanced")),
                 QStringLiteral("window.logosDrive.press('Advanced')"));
        QCOMPARE(WebPageInput::typeCall(QStringLiteral("Account label"),
                                        QStringLiteral("issue174")),
                 QStringLiteral("window.logosDrive.type('Account label', 'issue174')"));
        QCOMPARE(WebPageInput::readCall(QStringLiteral("Account label")),
                 QStringLiteral("window.logosDrive.read('Account label')"));
    }

    // A quote in a control's name or in the text being typed is a broken
    // script, and a broken script is silence -- the page throws where nothing
    // is listening, and the driver waits out its budget for an answer that was
    // never going to come.
    void aQuoteDoesNotBreakTheScript()
    {
        QCOMPARE(WebPageInput::pressCall(QStringLiteral("Pay Ann's bill")),
                 QStringLiteral("window.logosDrive.press('Pay Ann\\'s bill')"));
        QCOMPARE(WebPageInput::typeCall(QStringLiteral("Label"),
                                        QStringLiteral("a\\b")),
                 QStringLiteral("window.logosDrive.type('Label', 'a\\\\b')"));
    }

    void aValueIsReadBackForTheControlItIsAbout()
    {
        const QString line =
            QStringLiteral("logos-drive: 'Account label' now 'issue174'");
        const auto value = WebPageInput::valueReported(line, QStringLiteral("Account label"));
        QVERIFY(value.has_value());
        QCOMPARE(*value, QStringLiteral("issue174"));
        // ...and not for a different one. Two fields are typed into in the same
        // run and the answers arrive on the same console.
        QVERIFY(!WebPageInput::valueReported(line, QStringLiteral("Seed phrase")).has_value());
    }

    void anEmptyFieldIsAnAnswerAndNotAnAbsentOne()
    {
        const auto value = WebPageInput::valueReported(
            QStringLiteral("logos-drive: 'Account label' now ''"),
            QStringLiteral("Account label"));
        QVERIFY(value.has_value());
        QCOMPARE(*value, QString());
    }

    // A passphrase answers with a count rather than its contents, and that text
    // IS the answer: a driver comparing it against what it typed does not
    // match, which is the honest outcome for a field whose contents are
    // deliberately not readable off a device console.
    void aPasswordAnswersWithACount()
    {
        const auto value = WebPageInput::valueReported(
            QStringLiteral("logos-drive: 'Account passphrase' now 7 character(s)"),
            QStringLiteral("Account passphrase"));
        QVERIFY(value.has_value());
        QCOMPARE(*value, QStringLiteral("7 character(s)"));
    }

    // Any line from the page's own console -- a 26 MB QML runtime's included --
    // is offered to these, so the marker is load-bearing.
    void aLineWithoutTheMarkerIsNotAnAnswer()
    {
        QVERIFY(!WebPageInput::valueReported(
                     QStringLiteral("qml: 'Account label' now 'issue174'"),
                     QStringLiteral("Account label")).has_value());
        QVERIFY(!WebPageInput::pressReported(QStringLiteral("qml: pressed 'Import'"),
                                             QStringLiteral("Import")));
        QVERIFY(!WebPageInput::refusalReported(
                     QStringLiteral("qml: no control named 'Import'"),
                     QStringLiteral("Import")));
    }

    void aPressIsReportedForTheControlItPressed()
    {
        const QString line = QStringLiteral("logos-drive: pressed 'Import'");
        QVERIFY(WebPageInput::pressReported(line, QStringLiteral("Import")));
        QVERIFY(!WebPageInput::pressReported(line, QStringLiteral("Advanced")));
    }

    // Both refusals, because they are different findings and a driver has to
    // stop on either: a name the page does not have at all, and one it has
    // below the fold.
    void bothRefusalsAreRefusals()
    {
        QVERIFY(WebPageInput::refusalReported(
            QStringLiteral("logos-drive: no control named 'Import'; reachable: New | Send"),
            QStringLiteral("Import")));
        QVERIFY(WebPageInput::refusalReported(
            QStringLiteral("logos-drive: 'Import' is on the page and not reachable"),
            QStringLiteral("Import")));
        QVERIFY(!WebPageInput::refusalReported(
            QStringLiteral("logos-drive: no control named 'Import'; reachable: New"),
            QStringLiteral("New")));
    }
};

QTEST_MAIN(WebPageInputTest)
#include "web_page_input_test.moc"
