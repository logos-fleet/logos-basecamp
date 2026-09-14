// srcdeps: basecamp-shell/src/WebAppSurface.cpp
//
// WHERE A WEB APP'S PAGE IS ALLOWED TO BE, decided in widget coordinates.
//
// A `web` module's UI is not a widget: it is a platform page the container
// mounted AT THE WINDOW'S SIZE, behind the Shell's own surface, and "open it"
// is a z-order instruction. Brought forward it covers the Shell entirely --
// sidebar, tab bar and all -- and a user who opened an app cannot leave it
// again (#110).
//
// The Shell's answer is to dock a PLACEHOLDER for a web app exactly as it docks
// a `ui_qml` one. The placeholder is this class, and its whole job is to say
// where the Shell's workspace put it, in the window's coordinates, whenever
// that changes -- which the host then hands to the Web container as the rect
// the page may occupy. The chrome the user needs is then outside that rect and
// stays live.
//
// It is a widget with no pixels in it. Nothing it could paint would ever be
// seen: on both phones the page is a native view in front of Qt's whole
// surface, so what is under the page is invisible by construction.
//
//   nix build .#unit-tests -L
#include "basecamp-shell/src/WebAppSurface.h"

#include <QtTest/QtTest>
#include <QApplication>
#include <QSignalSpy>
#include <QVBoxLayout>
#include <QWidget>

class WebAppSurfaceTest : public QObject
{
    Q_OBJECT

private:
    // A stand-in for the Shell: a window with chrome above the content area and
    // a surface filling what is left. The numbers are what matter -- the page
    // must not be given the chrome's rows.
    struct Shell {
        QWidget window;
        QWidget* chrome = nullptr;
        WebAppSurface* surface = nullptr;
    };

    static std::unique_ptr<Shell> shell(const QString& module = QStringLiteral("wallet_ui"))
    {
        auto s = std::make_unique<Shell>();
        auto* layout = new QVBoxLayout(&s->window);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);
        s->chrome = new QWidget(&s->window);
        s->chrome->setFixedHeight(80);
        s->surface = new WebAppSurface(module, &s->window);
        layout->addWidget(s->chrome);
        layout->addWidget(s->surface, 1);
        s->window.resize(600, 800);
        return s;
    }

    static void settle()
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }

private slots:
    // A surface nobody has shown yet has no rect at all. An empty rect means
    // "the whole window" to the container, and a host that published one before
    // the Shell had laid anything out would put the page back over the chrome.
    void aSurfaceThatWasNeverShownIsNotOnScreen()
    {
        auto s = shell();
        QVERIFY(!s->surface->onScreen());
        QCOMPARE(s->surface->pageRect(), QRect());
    }

    // THE RECT IS THE WINDOW'S, not the parent's. The container positions a
    // native view inside the host window, so a rect measured against anything
    // else would land the page at an offset nobody can see the cause of.
    void aShownSurfaceSaysWhereItIsInTheWindow()
    {
        auto s = shell();
        QSignalSpy placed(s->surface, &WebAppSurface::placementChanged);
        s->window.show();
        settle();

        QVERIFY(s->surface->onScreen());
        const QRect rect = s->surface->pageRect();
        QCOMPARE(rect.top(), 80);
        QCOMPARE(rect.left(), 0);
        QCOMPARE(rect.width(), 600);
        QCOMPARE(rect.height(), 720);
        // ...AND THE CHROME IS OUTSIDE IT, which is the whole point of #110.
        QVERIFY(!rect.contains(QPoint(300, 40)));

        QVERIFY(!placed.isEmpty());
        const QList<QVariant> last = placed.last();
        QCOMPARE(last.at(0).toString(), QStringLiteral("wallet_ui"));
        QCOMPARE(last.at(1).toRect(), rect);
        QCOMPARE(last.at(2).toBool(), true);
    }

    // The window is resized -- a rotation, a split view, a soft keyboard. The
    // page has to follow, and the only thing that knows it moved is the widget
    // the layout moved.
    void aSurfaceFollowsTheWindow()
    {
        auto s = shell();
        s->window.show();
        settle();
        QSignalSpy placed(s->surface, &WebAppSurface::placementChanged);

        s->window.resize(900, 500);
        settle();

        QCOMPARE(s->surface->pageRect(), QRect(0, 80, 900, 420));
        QVERIFY(!placed.isEmpty());
        QCOMPARE(placed.last().at(1).toRect(), QRect(0, 80, 900, 420));
    }

    // AN ANCESTOR MOVING COUNTS. The surface itself never moves inside its own
    // parent when the chrome above it grows -- the parent does the moving -- and
    // a class that only watched its own move and resize events would report a
    // stale rect for exactly that case.
    void anAncestorGrowingMovesThePage()
    {
        auto s = shell();
        s->window.show();
        settle();
        QSignalSpy placed(s->surface, &WebAppSurface::placementChanged);

        s->chrome->setFixedHeight(200);
        settle();

        QCOMPARE(s->surface->pageRect().top(), 200);
        QVERIFY(!placed.isEmpty());
        QCOMPARE(placed.last().at(1).toRect().top(), 200);
    }

    // A CONTENT AREA LARGER THAN THE WINDOW. Measured: on an iPhone 16 Pro the
    // Shell's workspace dock is 703x739 inside a 402x874 window, and a `ui_qml`
    // app's widget just has its overflow left undrawn. A native page positioned
    // at that width would be hundreds of pixels of module UI past the right edge
    // of the screen with nothing to scroll it back. The page gets the hole the
    // user can see.
    void aSurfaceWiderThanTheWindowIsClippedToIt()
    {
        // No layout: the surface keeps the geometry it is given, which is how a
        // content area comes to be bigger than the window holding it.
        QWidget window;
        window.resize(600, 800);
        auto* surface = new WebAppSurface(QStringLiteral("wallet_ui"), &window);
        surface->setGeometry(0, 80, 900, 1200);
        window.show();
        settle();

        QCOMPARE(surface->pageRect(), QRect(0, 80, 600, 720));
        QVERIFY(surface->onScreen());
    }

    // ...AND A PLACEHOLDER ENTIRELY OFF THE WINDOW IS NOWHERE A PAGE CAN GO.
    // Qt still calls it visible; there is no hole.
    void aSurfaceOffTheWindowIsNotOnScreen()
    {
        QWidget window;
        window.resize(600, 800);
        auto* surface = new WebAppSurface(QStringLiteral("wallet_ui"), &window);
        surface->setGeometry(900, 900, 200, 200);
        window.show();
        settle();

        QVERIFY(!surface->onScreen());
        QCOMPARE(surface->pageRect(), QRect());
    }

    // NOBODY IS LOOKING AT IT. Raising another dock tab, or leaving the
    // workspace section entirely, hides the placeholder -- and that is what the
    // host reads as "put this page back behind the Shell". The alternative is
    // what #110 reports: an app the user can open and not leave.
    void aHiddenSurfaceIsOffScreen()
    {
        auto s = shell();
        s->window.show();
        settle();
        QSignalSpy placed(s->surface, &WebAppSurface::placementChanged);

        s->surface->hide();
        settle();

        QVERIFY(!s->surface->onScreen());
        QCOMPARE(s->surface->pageRect(), QRect());
        QVERIFY(!placed.isEmpty());
        QCOMPARE(placed.last().at(2).toBool(), false);

        s->surface->show();
        settle();
        QVERIFY(s->surface->onScreen());
        QCOMPARE(placed.last().at(2).toBool(), true);
    }

    // IT DOES NOT REPEAT ITSELF. Every announcement costs the host a show() on
    // the container, which spends the live-runtime budget and prints the app's
    // memory; a surface that re-announced on every polish pass would turn a
    // layout into a log.
    void anUnchangedPlacementIsNotAnnouncedAgain()
    {
        auto s = shell();
        s->window.show();
        settle();
        QSignalSpy placed(s->surface, &WebAppSurface::placementChanged);

        s->window.resize(600, 800);   // the size it already has
        settle();
        s->surface->updateGeometry();
        settle();

        QCOMPARE(placed.count(), 0);
    }
};

QTEST_MAIN(WebAppSurfaceTest)
#include "web_app_surface_test.moc"
