// srcdeps: basecamp-shell/src/QuickWidgetKeyboardFocus.cpp
//
// WHETHER A TAPPED FIELD IN AN EMBEDDED QML SCENE CAN RAISE A KEYBOARD.
//
// On an iPad, tapping any chat_ui input field left the field with a caret and
// no on-screen keyboard (logos-workspace#152). The caret is the scene's focus;
// the keyboard is the WINDOW's. A QQuickWidget renders into an offscreen
// QQuickWindow, so those are two different objects, and the platform input
// context is only ever told about the second one --
// QGuiApplication::focusObject(), which for a widget window is its focus
// widget.
//
// Nothing on iOS makes the QQuickWidget that focus widget when a finger lands
// on it: the platform asks for focus on touch RELEASE
// (QIOSIntegration::SetFocusOnTouchRelease), QApplication::notify only applies
// that policy on touch BEGIN, and the synthesised mouse press that would
// otherwise have carried it never happens because QQuickWidget accepts the
// touch itself. So the tests below state the invariant that was missing, in the
// terms the platform reads it in:
//
//   the window's focus widget must be the surface whose scene holds the caret,
//   and the object QGuiApplication hands the input context must answer
//   Qt::ImEnabled.
//
// They are platform-independent on purpose -- they set up the STATE an iOS tap
// leaves behind (an item with activeFocus inside a surface that is not the
// focus widget) rather than trying to reproduce the touch -- so they run on the
// offscreen platform the other unit tests use.
//
// Run: nix build .#unit-tests -L
#include "basecamp-shell/src/QuickWidgetKeyboardFocus.h"

#include <QtTest/QtTest>

#include <QApplication>
#include <QInputMethodQueryEvent>
#include <QLineEdit>
#include <QQmlError>
#include <QQuickItem>
#include <QQuickWidget>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QVBoxLayout>
#include <QWidget>

namespace {

// One field in a scene, and a second widget beside it to hold the focus the
// surface is missing -- which is the whole of what an iOS tap leaves behind.
const char* kSceneQml = R"(
import QtQuick
Rectangle {
    width: 320; height: 200; color: "white"
    TextInput {
        objectName: "field"
        anchors.centerIn: parent
        width: 240; height: 32
    }
    Rectangle {
        objectName: "notAField"
        width: 20; height: 20
    }
}
)";

void spin(int ms = 100)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
}

} // namespace

class QuickWidgetKeyboardFocusTest : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_dir;
    QString m_qml;

    // A window with a QQuickWidget and one plain widget beside it. Returned
    // rather than held so each test gets its own focus history.
    struct Fixture {
        std::unique_ptr<QWidget> window;
        QQuickWidget* surface = nullptr;
        QLineEdit* other = nullptr;
        QQuickItem* field = nullptr;
    };

    Fixture makeFixture()
    {
        Fixture f;
        f.window = std::make_unique<QWidget>();
        auto* layout = new QVBoxLayout(f.window.get());
        f.other = new QLineEdit;
        f.other->setObjectName(QStringLiteral("otherWidget"));
        f.surface = new QQuickWidget;
        f.surface->setObjectName(QStringLiteral("surface"));
        f.surface->setResizeMode(QQuickWidget::SizeRootObjectToView);
        layout->addWidget(f.other);
        layout->addWidget(f.surface);

        f.surface->setSource(QUrl::fromLocalFile(m_qml));
        for (const QQmlError& e : f.surface->errors())
            qWarning("qml: %s", qPrintable(e.toString()));
        f.window->resize(400, 320);
        f.window->show();
        spin(150);
        f.field = f.surface->rootObject()
                      ? f.surface->rootObject()->findChild<QQuickItem*>(QStringLiteral("field"))
                      : nullptr;
        return f;
    }

    // The state an iOS tap leaves: the caret is in the scene and the surface is
    // not the window's focus widget.
    static void putTheCaretInTheSceneWithoutFocusingTheSurface(const Fixture& f)
    {
        f.other->setFocus(Qt::OtherFocusReason);
        spin(50);
        f.field->forceActiveFocus();
        spin(50);
    }

    static bool windowFocusObjectAcceptsInput(const Fixture& f)
    {
        QWindow* handle = f.window->windowHandle();
        return handle
            && QuickWidgetKeyboardFocus::acceptsInputMethod(handle->focusObject());
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        m_qml = m_dir.path() + QStringLiteral("/Main.qml");
        QFile file(m_qml);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(kSceneQml);
    }

    // THE BUG, as the platform sees it. Without a watcher the caret is in the
    // scene, the surface is not the focus widget, and the object the input
    // context would be handed is the wrong one.
    void test_an_unwatched_surface_leaves_the_keyboard_pointed_elsewhere()
    {
        Fixture f = makeFixture();
        QVERIFY(f.field);
        putTheCaretInTheSceneWithoutFocusingTheSurface(f);

        QVERIFY(f.field->hasActiveFocus());
        QCOMPARE(f.window->focusWidget(), static_cast<QWidget*>(f.other));
        QVERIFY(f.surface->quickWindow()->focusObject() != nullptr);
    }

    // AND THE FIX. A watched surface takes the window's focus the moment its
    // scene focuses something that wants a keyboard.
    void test_a_watched_surface_takes_the_window_focus_for_its_field()
    {
        Fixture f = makeFixture();
        QVERIFY(f.field);
        QuickWidgetKeyboardFocus watcher;
        watcher.watch(f.surface);

        putTheCaretInTheSceneWithoutFocusingTheSurface(f);

        QCOMPARE(f.window->focusWidget(), static_cast<QWidget*>(f.surface));
        QVERIFY(f.field->hasActiveFocus());
        // And the object the platform input context is handed answers the one
        // question it asks before raising a keyboard.
        QVERIFY(windowFocusObjectAcceptsInput(f));
    }

    // A scene that focused its field BEFORE anyone was watching still counts:
    // chat_ui's New DM dialog focuses its address field from onOpened, which is
    // the same turn the dialog is created in.
    void test_watching_a_surface_that_already_holds_the_caret_fixes_it_at_once()
    {
        Fixture f = makeFixture();
        QVERIFY(f.field);
        putTheCaretInTheSceneWithoutFocusingTheSurface(f);
        QCOMPARE(f.window->focusWidget(), static_cast<QWidget*>(f.other));

        QuickWidgetKeyboardFocus watcher;
        watcher.watch(f.surface);

        QCOMPARE(f.window->focusWidget(), static_cast<QWidget*>(f.surface));
        QVERIFY(windowFocusObjectAcceptsInput(f));
    }

    // Focus that is not a text item's is left alone. A button taking focus in
    // the sidebar must not pull the keyboard's focus off the composer.
    void test_a_scene_focusing_something_that_is_not_a_field_does_not_take_focus()
    {
        Fixture f = makeFixture();
        QuickWidgetKeyboardFocus watcher;
        watcher.watch(f.surface);
        f.other->setFocus(Qt::OtherFocusReason);
        spin(50);

        auto* plain = f.surface->rootObject()->findChild<QQuickItem*>(
            QStringLiteral("notAField"));
        QVERIFY(plain);
        plain->forceActiveFocus();
        spin(50);

        QVERIFY(!QuickWidgetKeyboardFocus::acceptsInputMethod(
            f.surface->quickWindow()->focusObject()));
        QCOMPARE(f.window->focusWidget(), static_cast<QWidget*>(f.other));
    }

    // Tapping back into a field the user had left. Leaving the surface clears
    // the scene's active focus, so coming back is a fresh focus change and the
    // watcher is what re-points the keyboard at it.
    void test_returning_to_a_field_after_leaving_the_surface_points_back_at_it()
    {
        Fixture f = makeFixture();
        QuickWidgetKeyboardFocus watcher;
        watcher.watch(f.surface);
        putTheCaretInTheSceneWithoutFocusingTheSurface(f);
        QCOMPARE(f.window->focusWidget(), static_cast<QWidget*>(f.surface));

        f.other->setFocus(Qt::OtherFocusReason);
        spin(50);
        QCOMPARE(f.window->focusWidget(), static_cast<QWidget*>(f.other));

        f.field->forceActiveFocus();
        spin(50);
        QCOMPARE(f.window->focusWidget(), static_cast<QWidget*>(f.surface));
        QVERIFY(windowFocusObjectAcceptsInput(f));
    }

    // A surface the watcher never heard of is found when it is shown, which is
    // how an app mounted long after startup is covered.
    void test_watching_everything_covers_a_surface_created_later()
    {
        QuickWidgetKeyboardFocus watcher;
        watcher.watchEverything();

        Fixture f = makeFixture();
        QVERIFY(f.field);
        putTheCaretInTheSceneWithoutFocusingTheSurface(f);

        QCOMPARE(f.window->focusWidget(), static_cast<QWidget*>(f.surface));
        QVERIFY(windowFocusObjectAcceptsInput(f));
    }

    // A destroyed surface leaves nothing behind for the next one to trip over.
    void test_a_destroyed_surface_is_forgotten()
    {
        QuickWidgetKeyboardFocus watcher;
        {
            Fixture f = makeFixture();
            watcher.watch(f.surface);
        }
        spin(50);
        Fixture again = makeFixture();
        watcher.watch(again.surface);
        putTheCaretInTheSceneWithoutFocusingTheSurface(again);
        QCOMPARE(again.window->focusWidget(), static_cast<QWidget*>(again.surface));
    }
};

QTEST_MAIN(QuickWidgetKeyboardFocusTest)
#include "quick_widget_keyboard_focus_test.moc"
