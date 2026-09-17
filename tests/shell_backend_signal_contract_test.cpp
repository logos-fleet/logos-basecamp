// WHAT THE SHARED SHELL SUBSCRIBES TO ON `backend`, AND THE TWO OBJECTS THAT
// HAVE TO ANSWER FOR IT.
//
// `backend` is a context property, and there are TWO classes behind it: the
// desktop's MainUIBackend and a phone's ShellModulesBackend. The QML above them
// is the same file either way -- src/Basecamp/Shell/ContentViews.qml is loaded
// by MainContainer in both builds -- so a `Connections` handler that matches a
// signal on one of them and nothing on the other is HALF wired, and QML reports
// that as a warning rather than an error:
//
//   ContentViews.qml:33:5: QML Connections: Detected function
//     "onRepositoryOperationCompleted" in Connections element. This is probably
//     intended to be a signal handler but no signal of the target matches the name.
//   QObject::connect: No such signal ShellModulesBackend::requestOpenAddApplicationDialog(QVariantMap)
//
// Three of those were printed at every Store-shell startup for at least two days
// (logos-workspace#249). A Connections block whose handler resolves to nothing
// is SILENTLY INERT: the install-completion handler in that file had no signal
// to fire it, and the operator who found it was looking at a catalog page whose
// Install button appeared to do nothing.
//
// SO THIS IS A SOURCE-LEVEL GATE, and it has to be: a metaobject check would
// need both backends linked, and ShellModulesBackend pulls in a core runtime, a
// module manager and LogosAPI -- none of which a unit-test harness has, and none
// of which this question is about. What is checked is exactly what the engine
// resolves at run time, read off the same files it reads:
//
//   every `function on<Name>` in a `Connections { target: backend }` block
//   every SIGNAL(...) a `connect` names on `m_host->backendObject()`
//
// ...must be declared in the `signals:` section of BOTH backend headers.
//
// A BLOCK MAY OPT OUT, with `ignoreUnknownSignals: true` -- OverlayDialogs.qml
// does, because its dialogs are the desktop's and a Store shell draws none of
// them. Saying so in the file is the point: it is a declaration that the
// handlers below are expected to be inert on one of the two, rather than a
// warning nobody reads.
#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QtTest>

namespace {

QString readAll(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    return QString::fromUtf8(f.readAll());
}

// The signal names a Qt header declares between `signals:` and the next access
// specifier. Only that section: a `void foo(...)` elsewhere in the class is a
// method, and accepting one would let a method stand in for the signal a
// Connections handler needs.
QSet<QString> signalsDeclaredIn(const QString& source)
{
    QSet<QString> out;
    static const QRegularExpression section(
        QStringLiteral("(^|\\n)\\s*signals\\s*:"));
    static const QRegularExpression ender(
        QStringLiteral("(^|\\n)\\s*(public|protected|private|public slots|"
                       "private slots|protected slots)\\s*:"));
    static const QRegularExpression decl(
        QStringLiteral("\\bvoid\\s+([A-Za-z_]\\w*)\\s*\\("));

    int from = 0;
    for (;;) {
        const QRegularExpressionMatch begin = section.match(source, from);
        if (!begin.hasMatch())
            break;
        const int bodyStart = begin.capturedEnd();
        const QRegularExpressionMatch end = ender.match(source, bodyStart);
        const int bodyEnd = end.hasMatch() ? end.capturedStart() : source.size();
        const QString body = source.mid(bodyStart, bodyEnd - bodyStart);
        QRegularExpressionMatchIterator it = decl.globalMatch(body);
        while (it.hasNext())
            out.insert(it.next().captured(1));
        from = bodyEnd;
    }
    return out;
}

// `onFooBar` -> `fooBar`, the rule QML uses to pair a handler with a signal.
QString signalForHandler(const QString& handler)
{
    QString name = handler;
    name[0] = name.at(0).toLower();
    return name;
}

// Every handler in every `Connections { target: backend }` block that has NOT
// declared `ignoreUnknownSignals: true`, with the line it is on -- a failure
// that cannot be located is a failure nobody fixes.
QList<QPair<QString, int>> backendHandlersIn(const QString& qml)
{
    QList<QPair<QString, int>> out;
    static const QRegularExpression opener(QStringLiteral("\\bConnections\\s*\\{"));
    static const QRegularExpression handler(
        QStringLiteral("\\bfunction\\s+on([A-Z]\\w*)\\s*\\("));

    QRegularExpressionMatchIterator blocks = opener.globalMatch(qml);
    while (blocks.hasNext()) {
        const QRegularExpressionMatch block = blocks.next();
        // The block's extent, by brace matching from its opening one. A QML
        // object body nests (a Loader, an inline component), so counting is the
        // only way to know where the Connections ends.
        int depth = 1;
        int i = block.capturedEnd();
        for (; i < qml.size() && depth > 0; ++i) {
            if (qml.at(i) == QLatin1Char('{'))
                ++depth;
            else if (qml.at(i) == QLatin1Char('}'))
                --depth;
        }
        const QString body = qml.mid(block.capturedEnd(), i - block.capturedEnd());
        if (!body.contains(QRegularExpression(QStringLiteral("target\\s*:\\s*backend\\b"))))
            continue;
        if (body.contains(QRegularExpression(
                QStringLiteral("ignoreUnknownSignals\\s*:\\s*true"))))
            continue;

        QRegularExpressionMatchIterator it = handler.globalMatch(body);
        while (it.hasNext()) {
            const QRegularExpressionMatch m = it.next();
            const int at = block.capturedEnd() + m.capturedStart();
            out << qMakePair(m.captured(1),
                             qml.left(at).count(QLatin1Char('\n')) + 1);
        }
    }
    return out;
}

// Every SIGNAL(...) a connect() names on the backend, by the sender it was
// given. Scanned FORWARD from each `backendObject()` to the end of the
// statement, so a connect with the backend as its RECEIVER -- which names a
// SLOT after it, not a SIGNAL -- contributes nothing.
QList<QPair<QString, int>> backendSignalMacrosIn(const QString& cpp)
{
    QList<QPair<QString, int>> out;
    static const QRegularExpression sender(QStringLiteral("backendObject\\s*\\(\\s*\\)"));
    static const QRegularExpression macro(QStringLiteral("\\bSIGNAL\\s*\\(\\s*([A-Za-z_]\\w*)"));

    QRegularExpressionMatchIterator it = sender.globalMatch(cpp);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        const int stop = cpp.indexOf(QLatin1Char(';'), m.capturedEnd());
        const QString rest = cpp.mid(m.capturedEnd(),
                                     (stop < 0 ? cpp.size() : stop) - m.capturedEnd());
        QRegularExpressionMatchIterator sigs = macro.globalMatch(rest);
        while (sigs.hasNext()) {
            const QRegularExpressionMatch s = sigs.next();
            const int at = m.capturedEnd() + s.capturedStart();
            out << qMakePair(s.captured(1),
                             cpp.left(at).count(QLatin1Char('\n')) + 1);
        }
    }
    return out;
}

} // namespace

class ShellBackendSignalContractTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        m_root = QStringLiteral(BASECAMP_REPO_ROOT);
        m_desktop = signalsDeclaredIn(readAll(m_root + QStringLiteral("/app/MainUIBackend.h")));
        m_store = signalsDeclaredIn(
            readAll(m_root + QStringLiteral("/mobile/basecamp-shell/src/ShellModulesBackend.h")));
        // A PARSER THAT FOUND NOTHING would make every case below vacuously
        // green, which is the one way a gate like this fails silently.
        QVERIFY2(m_desktop.size() > 10,
                 qPrintable(QStringLiteral("only %1 signals parsed out of MainUIBackend.h")
                                .arg(m_desktop.size())));
        QVERIFY2(m_store.size() > 5,
                 qPrintable(QStringLiteral("only %1 signals parsed out of "
                                           "ShellModulesBackend.h").arg(m_store.size())));
        QVERIFY2(m_desktop.contains(QStringLiteral("shellIntentRequested")),
                 "the parser does not see a signal this header certainly declares");
    }

    // ContentViews.qml is loaded by MainContainer in BOTH builds, so each of
    // its handlers has to resolve against both backends.
    void aSharedConnectionsHandlerResolvesOnBothBackends()
    {
        const QString path = m_root + QStringLiteral("/src/Basecamp/Shell/ContentViews.qml");
        const QString qml = readAll(path);
        QVERIFY2(!qml.isEmpty(), qPrintable(path));

        const QList<QPair<QString, int>> handlers = backendHandlersIn(qml);
        QVERIFY2(!handlers.isEmpty(), "no backend handlers found -- the parser is wrong");

        QStringList dead;
        for (const auto& h : handlers) {
            const QString signalName = signalForHandler(h.first);
            QStringList missing;
            if (!m_desktop.contains(signalName))
                missing << QStringLiteral("MainUIBackend");
            if (!m_store.contains(signalName))
                missing << QStringLiteral("ShellModulesBackend");
            if (!missing.isEmpty()) {
                dead << QStringLiteral("ContentViews.qml:%1 on%2 matches no signal on %3")
                            .arg(h.second).arg(h.first, missing.join(QStringLiteral(" or ")));
            }
        }
        QVERIFY2(dead.isEmpty(), qPrintable(dead.join(QStringLiteral("\n  "))));
    }

    // ...and so does every signal MainContainer names by string. The compiler
    // checks neither end of a SIGNAL() macro; this is the only thing that does.
    void aStringNamedConnectResolvesOnBothBackends()
    {
        const QString path = m_root + QStringLiteral("/src/MainContainer.cpp");
        const QString cpp = readAll(path);
        QVERIFY2(!cpp.isEmpty(), qPrintable(path));

        const QList<QPair<QString, int>> named = backendSignalMacrosIn(cpp);
        QVERIFY2(!named.isEmpty(), "no backend SIGNAL() found -- the parser is wrong");

        QStringList dead;
        for (const auto& s : named) {
            QStringList missing;
            if (!m_desktop.contains(s.first))
                missing << QStringLiteral("MainUIBackend");
            if (!m_store.contains(s.first))
                missing << QStringLiteral("ShellModulesBackend");
            if (!missing.isEmpty()) {
                dead << QStringLiteral("MainContainer.cpp:%1 SIGNAL(%2) does not exist on %3")
                            .arg(s.second).arg(s.first, missing.join(QStringLiteral(" or ")));
            }
        }
        QVERIFY2(dead.isEmpty(), qPrintable(dead.join(QStringLiteral("\n  "))));
    }

    // The three #249 named, by name. The two cases above would catch them as a
    // class; this says which three they were, so a reader of a future failure
    // knows whether it is the same defect coming back.
    void theThreeStartupWarningsOf249StayFixed()
    {
        for (const QString& name : {QStringLiteral("repositoryOperationCompleted"),
                                    QStringLiteral("shellIntentRequested"),
                                    QStringLiteral("requestOpenAddApplicationDialog")}) {
            QVERIFY2(m_store.contains(name),
                     qPrintable(QStringLiteral("ShellModulesBackend declares no %1 -- "
                                               "the shared shell subscribes to it and a "
                                               "Store shell would print a startup warning "
                                               "and be silently inert (logos-workspace#249)")
                                    .arg(name)));
        }
    }

private:
    QString m_root;
    QSet<QString> m_desktop;
    QSet<QString> m_store;
};

QTEST_MAIN(ShellBackendSignalContractTest)
#include "shell_backend_signal_contract_test.moc"
