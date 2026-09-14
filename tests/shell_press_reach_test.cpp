// srcdeps: basecamp-shell/src/ShellSceneDriver.cpp
//
// WHETHER A SYNTHESISED PRESS IS ONE A FINGER COULD HAVE MADE.
//
// The Shell's drivers press by coordinate, which is the one thing a person
// cannot do: a QMouseEvent lands wherever it is addressed, on screen or not.
// So a driver is only worth its verdict if it refuses the presses a person
// could not make -- logos-workspace#84 made that the rule, after #9's driver
// had spent several cycles activating an off-screen control by hand and
// reporting the Modules tab green while nobody could load a module at all.
//
// #84's version of the rule asked ONE question: does the control's own view
// contain the point? That misses the case the iPhone 16 Pro found
// (logos-workspace#87). A Qt layout handed less width than its minimum does
// not shrink, it OVERFLOWS -- so on a 402-pt screen the Shell laid itself out
// 784 pt wide, the Settings pane inside it was 704 pt wide, and the Modules
// row's toggle at x=630 was comfortably inside its own pane and about 300 pt
// past the right edge of the phone. The driver pressed it and reported
// SHELL MODULES TAB ROUND TRIP OK over a screen on which the control was
// invisible.
//
// The second question is therefore where that point lands on the SCREEN.
//
// Run: nix build .#unit-tests -L
#include "ShellSceneDriver.h"

#include <QtTest/QtTest>

class ShellPressReachTest : public QObject {
    Q_OBJECT

private slots:
    // The ordinary case: a pane that fits its phone, a control inside it.
    void test_a_control_inside_a_pane_that_fits_is_reachable()
    {
        const QRectF screen(0, 0, 402, 874);
        QVERIFY(pressIsReachable(QPointF(260, 354), QSizeF(306, 763),
                                 QPointF(356, 445), screen));
    }

    // #84's case, unchanged: scrolled away inside its own view.
    void test_a_control_outside_its_own_pane_is_not_reachable()
    {
        const QRectF screen(0, 0, 928, 1326);
        QVERIFY(!pressIsReachable(QPointF(1000, 264), QSizeF(928, 1326),
                                  QPointF(1000, 264), screen));
    }

    // #87's case: inside the pane, and the pane is off the phone. These are
    // the numbers the iPhone 16 Pro simulator printed.
    void test_a_control_inside_an_overflowing_pane_is_not_reachable()
    {
        const QRectF screen(0, 0, 402, 874);
        // x=630 of a 704-wide pane whose left edge is at x=84 on the screen.
        QVERIFY(!pressIsReachable(QPointF(630, 354), QSizeF(704, 763),
                                  QPointF(714, 445), screen));
    }

    // The edges. Inclusive on both rectangles, which is QRectF's own rule and
    // the one #84's check already had -- a control flush against the right
    // edge of a compact table is the layout working, not failing. What is
    // outside is anything PAST it, on either rectangle.
    void test_the_far_edge_is_inside_and_past_it_is_not()
    {
        const QRectF screen(0, 0, 402, 874);
        QVERIFY(pressIsReachable(QPointF(306, 100), QSizeF(306, 763),
                                 QPointF(306, 100), screen));
        QVERIFY(!pressIsReachable(QPointF(306.5, 100), QSizeF(306, 763),
                                  QPointF(306.5, 100), screen));
        // Inside its pane, one point past the right edge of the phone.
        QVERIFY(!pressIsReachable(QPointF(100, 100), QSizeF(306, 763),
                                  QPointF(402.5, 100), screen));
    }

    // A screen the platform could not report is not evidence of a small one:
    // the pane's own answer stands, which is what a desktop check gets.
    void test_an_unknown_screen_falls_back_to_the_pane()
    {
        QVERIFY(pressIsReachable(QPointF(630, 354), QSizeF(704, 763),
                                 QPointF(714, 445), QRectF()));
        QVERIFY(!pressIsReachable(QPointF(800, 354), QSizeF(704, 763),
                                  QPointF(884, 445), QRectF()));
    }
};

QTEST_MAIN(ShellPressReachTest)
#include "shell_press_reach_test.moc"
