// srcdeps: liblogos-smoke/src/ViewMountTeardown.cpp cpp/logos_plugin_unload.cpp
//
// THE ORDER A NATIVE VIEW MOUNT IS TAKEN DOWN IN (logos-workspace#212).
//
// The defect this covers had no crash and no wrong pixel in it. Closing a
// mounted `ui_qml` app tore the mount down in BUILD order -- node, host,
// plugin, api -- so the plugin, which is the only thing that can say anything
// to the modules the view called, was destroyed after its transport. chat_ui's
// "close my session" went out into nothing:
//
//   [qt] ChatModule::shutdown: remote call failed:
//        "consumer wrapper has no transport (null bridge)"
//
// and, because closing an app is deliberately not an unload, chat_module kept
// the session. Every re-open opened another.
//
// It is asserted HERE rather than on a phone because on a phone the evidence is
// one warning line in a console nobody is reading, six minutes of build away,
// and the app looks perfect either way.
//
// The hook is driven by the REAL logos::runPluginAboutToUnload (logos-plugin-qt
// cpp/logos_plugin_unload.cpp, compiled into this test) rather than a lookalike,
// so what is under test is the sequence the phone runs.
//
// Run: nix build .#unit-tests -L

#include "liblogos-smoke/src/ViewMountTeardown.h"

#include <QtTest/QtTest>

#include <QStringList>
#include <QTimer>

using basecamp::mobile::destroyViewMount;
using basecamp::mobile::finishViewMount;
using basecamp::mobile::ViewMountParts;

namespace {

// The journal every fixture writes to. A file-static rather than a member
// because what is being recorded is the order of DESTRUCTIONS, and a destructor
// has no test object to reach.
QStringList g_journal;

// Writes its name when it dies. Stands in for the node, the host and the
// LogosAPI -- all three are plain QObjects to the teardown, which is the point:
// what it is allowed to know about them is that they are the transport.
class Tombstone : public QObject
{
    Q_OBJECT
public:
    explicit Tombstone(QString name) : m_name(std::move(name)) {}
    ~Tombstone() override { g_journal << m_name; }

private:
    QString m_name;
};

// A view plugin that answers the teardown hook, and records the answer.
//
// `aboutToUnload` is Q_INVOKABLE and `unloadFinished` is a signal, because that
// is the whole contract: the host reaches both BY NAME through the meta-object
// (PluginInterface is compiled separately into every plugin, so neither may be
// a virtual). A fixture that declared them any other way would test nothing.
class FixturePlugin : public QObject
{
    Q_OBJECT
public:
    enum Answer { Synchronous = 0, Asynchronous = 1 };

    explicit FixturePlugin(Answer answer) : m_answer(answer) {}
    ~FixturePlugin() override { g_journal << QStringLiteral("plugin"); }

    Q_INVOKABLE int aboutToUnload()
    {
        g_journal << QStringLiteral("about-to-unload");
        if (m_answer == Asynchronous) {
            // The shape a view with real work to finish has: the answer comes
            // back on a later turn of the loop, so the host has to be running
            // one for it to arrive at all.
            QTimer::singleShot(20, this, [this] {
                g_journal << QStringLiteral("work-done");
                emit unloadFinished();
            });
        }
        return static_cast<int>(m_answer);
    }

Q_SIGNALS:
    void unloadFinished();

private:
    Answer m_answer;
};

// A plugin from before the hook existed: no aboutToUnload meta-method at all.
// The common case, and it has to stay free.
class HooklessPlugin : public QObject
{
    Q_OBJECT
public:
    ~HooklessPlugin() override { g_journal << QStringLiteral("plugin"); }
};

ViewMountParts partsAround(QObject* plugin)
{
    ViewMountParts parts;
    parts.plugin = plugin;
    parts.node = new Tombstone(QStringLiteral("node"));
    parts.host = new Tombstone(QStringLiteral("host"));
    parts.api = new Tombstone(QStringLiteral("api"));
    return parts;
}

} // namespace

class ViewMountTeardownTest : public QObject
{
    Q_OBJECT

private slots:
    void init() { g_journal.clear(); }

    // ── THE DEFECT ITSELF ───────────────────────────────────────────────────
    // The plugin is the only object in a mount that calls OUT, and it must be
    // destroyed while there is still something to call out through. Build order
    // put it after the node and the host; this is the assertion that says it
    // may not go back there.
    void pluginIsDestroyedBeforeItsTransport()
    {
        destroyViewMount(partsAround(new FixturePlugin(FixturePlugin::Synchronous)));

        const int plugin = g_journal.indexOf(QStringLiteral("plugin"));
        QVERIFY2(plugin != -1, "the plugin was never destroyed");
        for (const QString& part : { QStringLiteral("node"), QStringLiteral("host"),
                                     QStringLiteral("api") }) {
            const int transport = g_journal.indexOf(part);
            QVERIFY2(transport != -1,
                     qPrintable(QStringLiteral("%1 was never destroyed").arg(part)));
            QVERIFY2(plugin < transport,
                     qPrintable(QStringLiteral("the plugin was destroyed AFTER %1 -- its "
                                               "last call out reaches nothing (%2)")
                                    .arg(part, g_journal.join(QLatin1Char(',')))));
        }
    }

    // And the hook comes first of all, which is the half that actually closes
    // the session: a view is asked to finish while every one of its four
    // objects is still there, not while it is being deleted.
    void theHookRunsBeforeAnythingIsDestroyed()
    {
        const ViewMountParts parts = partsAround(new FixturePlugin(FixturePlugin::Synchronous));
        finishViewMount(parts.plugin, 2000);
        destroyViewMount(parts);

        QCOMPARE(g_journal, (QStringList{ QStringLiteral("about-to-unload"),
                                          QStringLiteral("plugin"),
                                          QStringLiteral("node"),
                                          QStringLiteral("host"),
                                          QStringLiteral("api") }));
    }

    // A view that cannot finish inline gets its grace period here too, and the
    // proof is that its deferred work lands ABOVE its own destruction: only a
    // running event loop after the hook returned could have dispatched it.
    void asynchronousTeardownIsWaitedFor()
    {
        const ViewMountParts parts = partsAround(new FixturePlugin(FixturePlugin::Asynchronous));
        finishViewMount(parts.plugin, 2000);
        destroyViewMount(parts);

        QVERIFY2(g_journal.contains(QStringLiteral("work-done")),
                 "the view said Asynchronous and was torn down without being waited for");
        QVERIFY(g_journal.indexOf(QStringLiteral("work-done"))
                < g_journal.indexOf(QStringLiteral("plugin")));
    }

    // A plugin that predates the hook has no such meta-method: invokeMethod
    // fails, nothing is logged, and teardown proceeds at once. The common case
    // must cost nothing -- including no grace period.
    void aPluginWithoutTheHookIsTornDownImmediately()
    {
        QElapsedTimer t;
        t.start();
        const ViewMountParts parts = partsAround(new HooklessPlugin);
        finishViewMount(parts.plugin, 5000);
        destroyViewMount(parts);

        QCOMPARE(g_journal, (QStringList{ QStringLiteral("plugin"),
                                          QStringLiteral("node"),
                                          QStringLiteral("host"),
                                          QStringLiteral("api") }));
        QVERIFY2(t.elapsed() < 2000,
                 "a plugin with no hook waited out a grace period it never asked for");
    }

    // A mount that failed half-way through has some of its four and not the
    // rest, and is torn down by the same call. Nothing here may assume a
    // complete mount.
    void aPartialMountIsTornDownWithoutCrashing()
    {
        ViewMountParts parts;
        parts.api = new Tombstone(QStringLiteral("api"));
        finishViewMount(parts.plugin, 2000);
        destroyViewMount(parts);

        QCOMPARE(g_journal, QStringList{ QStringLiteral("api") });
    }
};

QTEST_MAIN(ViewMountTeardownTest)
#include "view_mount_teardown_test.moc"
