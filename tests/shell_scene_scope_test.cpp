// srcdeps: basecamp-shell/src/ShellSceneDriver.cpp
//
// WHICH SCENE A DRIVER IS ALLOWED TO PRESS.
//
// A driver in the mobile Shell stands in for a finger: it finds a control by
// objectName in one of the Shell's QQuickWidgets and sends a press at its
// centre. The Shell has several of those scenes at once, and -- this is the
// part that was missing -- it can have TWO COPIES OF THE SAME ONE.
//
// logos-workspace#187, measured on the venue's physical iPad Air (4th gen).
// ShellAppDriver closes a Bundled app and opens it again to prove the second
// mount is live. Closing takes the widget out of its dock and `deleteLater()`s
// it, and a DEFERRED DELETE IS NOT DELIVERED while the drivers run: they work
// off `QCoreApplication::processEvents()` inside a `QTimer::singleShot`, which
// never returns to the event loop that posted the delete. So the unmounted
// chat_ui surface is still a child of the Shell, off screen, with its whole QML
// scene alive in it -- and `dumpNames()` printed exactly that:
//
//     scene 'qrc:/logos/chat_ui/ChatView.qml' (root )
//       newMenuButton, conversationList, ...
//     scene 'qrc:/logos/chat_ui/ChatView.qml' (root )
//       newMenuButton, conversationList, ...
//
// The lookup took the FIRST match, which is the dead one, and every step after
// it succeeded: the "+" opened its menu, New DM opened the dialog, the address
// field took activeFocus and iOS drew a full software keyboard over a screen
// that showed none of it. A photograph of the device shows the conversations
// pane exactly as it was. An off-screen scene still runs its bindings, still
// answers its properties and still has geometry -- it just is not anywhere a
// finger can reach, and a driver that presses it reports a path GREEN that no
// user can walk.
//
// So the rule below: a lookup sees the scenes that are ON SCREEN, and nothing
// else. A control whose only copy is in an off-screen scene is not found at
// all, which is the honest answer and the one that makes the pass fail --
// dumpNames() then lists the off-screen scenes too, marked, so the reader is
// told where the copy went in the same breath.
//
// Run: nix build .#unit-tests -L
#include "basecamp-shell/src/ShellSceneDriver.h"

#include <QtTest/QtTest>

#include <QApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QQmlError>
#include <QQuickItem>
#include <QQuickWidget>
#include <QTemporaryDir>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>

#include <memory>

namespace {

// The shape of a mounted app, reduced to the one handle a driver looks for.
const char* kSceneQml = R"(
import QtQuick
Rectangle {
    width: 320; height: 240; color: "black"
    Rectangle {
        objectName: "newMenuButton"
        x: 20; y: 20; width: 120; height: 40
    }
}
)";

void spin(int ms = 120)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
}

// The protected lookups, as a test can call them. Nothing else is added: the
// point of the test is the policy inside find(), not a new entry point.
class Driver : public ShellSceneDriver
{
public:
    explicit Driver(QWidget* shell) : ShellSceneDriver(shell) {}

    QQuickItem*   look(const QString& name) { return find(name); }
    QQuickWidget* whichSurface(QQuickItem* item) { return surfaceOf(item); }
};

} // namespace

class ShellSceneScopeTest : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_dir;
    QString m_qml;

    // A Shell with two copies of one app's scene in it, in the order the Shell
    // makes them: the one that was closed first, the live one second. Both are
    // children of the Shell, because that is what a deferred delete that has
    // not run yet leaves behind.
    struct Shell {
        std::unique_ptr<QWidget> window;
        QQuickWidget* stale = nullptr;
        QQuickWidget* live  = nullptr;
    };

    Shell makeShellWithAStaleCopy()
    {
        Shell s;
        s.window = std::make_unique<QWidget>();
        auto* layout = new QVBoxLayout(s.window.get());
        s.stale = new QQuickWidget;
        s.live  = new QQuickWidget;
        for (QQuickWidget* surface : { s.stale, s.live }) {
            surface->setResizeMode(QQuickWidget::SizeRootObjectToView);
            layout->addWidget(surface);
            surface->setSource(QUrl::fromLocalFile(m_qml));
            for (const QQmlError& e : surface->errors())
                qWarning("qml: %s", qPrintable(e.toString()));
        }
        s.window->resize(400, 520);
        s.window->show();
        spin();
        // ...and then the app is closed: the dock takes the widget off screen
        // and posts a delete that this run will never deliver.
        s.stale->hide();
        spin();
        return s;
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        m_qml = m_dir.filePath(QStringLiteral("Scene.qml"));
        QFile f(m_qml);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(kSceneQml);
        f.close();
    }

    // The fixture is only worth anything if both copies really are there, with
    // the dead one first -- which is the order findChildren answers in and the
    // order the Shell creates them in.
    void test_the_fixture_has_both_copies_with_the_dead_one_first()
    {
        const Shell s = makeShellWithAStaleCopy();
        const QList<QQuickWidget*> scenes = s.window->findChildren<QQuickWidget*>();
        QCOMPARE(scenes.size(), 2);
        QCOMPARE(scenes.first(), s.stale);
        QVERIFY(!s.stale->isVisible());
        QVERIFY(s.live->isVisible());
        // ...and the dead one has a live scene with the handle in it, which is
        // exactly why the old lookup found it.
        QVERIFY(s.stale->rootObject());
        QVERIFY(s.stale->rootObject()->findChild<QQuickItem*>(QStringLiteral("newMenuButton")));
    }

    // THE ISSUE. Two scenes carry 'newMenuButton'; only one is on screen.
    void test_a_lookup_answers_the_scene_that_is_on_screen()
    {
        const Shell s = makeShellWithAStaleCopy();
        Driver driver(s.window.get());

        QQuickItem* found = driver.look(QStringLiteral("newMenuButton"));
        QVERIFY(found);
        QCOMPARE(driver.whichSurface(found), s.live);
    }

    // And when the only copy is off screen there is no answer to give. A
    // driver that pressed it would be pressing something no finger can reach,
    // and reporting the path it walked as working.
    void test_a_control_only_in_an_off_screen_scene_is_not_found()
    {
        const Shell s = makeShellWithAStaleCopy();
        s.live->hide();
        spin();
        Driver driver(s.window.get());

        QVERIFY(!driver.look(QStringLiteral("newMenuButton")));
    }
};

QTEST_MAIN(ShellSceneScopeTest)
#include "shell_scene_scope_test.moc"
