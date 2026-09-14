// srcdeps: ShellWindowFloor.cpp
//
// THE SHELL WINDOW'S MINIMUM SIZE, on a screen that may be smaller than it.
//
// A minimum size is a request to a window manager, and a phone does not have
// one. Qt does not shrink a widget below its minimum -- it lets it OVERFLOW,
// which puts the far side of the layout past the edge of the screen where no
// touch can reach it (see [[qt-layout-overflows-below-its-minimum]] and
// logos-workspace#84, which is the same failure one level down, inside the
// Settings table).
//
// Measured on the iPhone 16 Pro simulator with the Shell's desktop floor of
// 800x600 in place (logos-workspace#87): the screen is 402x874 pt and the
// Shell laid itself out 784 pt wide, so the Settings pane was 704 wide inside
// a 402-wide screen and every Modules row's Load/Unload control sat ~300 pt
// off the right edge. Nothing reported it: the pane itself was big enough to
// contain the control, so the tab's own driver saw a press well inside "the
// viewport" and pressed it by coordinate, which only a synthesised event can
// do.
//
// Run: nix build .#unit-tests -L
#include "ShellWindowFloor.h"

#include <QtTest/QtTest>

using basecamp::shellWindowFloor;

class ShellWindowFloorTest : public QObject {
    Q_OBJECT

private slots:
    // The desktop is unchanged: every desktop screen is larger than the floor,
    // so the floor is the floor.
    void test_a_screen_larger_than_the_floor_keeps_it()
    {
        QCOMPARE(shellWindowFloor(QSize(800, 600), QSize(1440, 900)), QSize(800, 600));
        QCOMPARE(shellWindowFloor(QSize(800, 600), QSize(3840, 2160)), QSize(800, 600));
    }

    // The bug, in the two dimensions the venue's devices have it.
    void test_a_phone_screen_caps_the_floor()
    {
        // iPhone 16 Pro, portrait: 402 pt of width against a 800-pt floor.
        QCOMPARE(shellWindowFloor(QSize(800, 600), QSize(402, 874)), QSize(402, 600));
        // Samsung SM-G990B, portrait: 1080x2340 px at 3x.
        QCOMPARE(shellWindowFloor(QSize(800, 600), QSize(360, 780)), QSize(360, 600));
    }

    // Height too -- a phone in landscape is the case, and a floor that fits
    // the width and not the height overflows just as far.
    void test_a_short_screen_caps_the_height()
    {
        QCOMPARE(shellWindowFloor(QSize(800, 600), QSize(874, 402)), QSize(800, 402));
    }

    // An iPad is between the two: wide enough for the floor, so it never saw
    // this -- which is why #84's device runs passed while a phone could not
    // reach the control at all.
    void test_a_tablet_screen_keeps_the_floor()
    {
        QCOMPARE(shellWindowFloor(QSize(800, 600), QSize(928, 1326)), QSize(800, 600));
        QCOMPARE(shellWindowFloor(QSize(800, 600), QSize(724, 1140)), QSize(724, 600));
    }

    // No screen to ask (a headless run, a platform plugin that reports
    // nothing) is not a reason to drop the floor: an unknown screen leaves the
    // desktop's answer alone rather than inventing a smaller one.
    void test_an_unusable_screen_leaves_the_floor_alone()
    {
        QCOMPARE(shellWindowFloor(QSize(800, 600), QSize()), QSize(800, 600));
        QCOMPARE(shellWindowFloor(QSize(800, 600), QSize(0, 0)), QSize(800, 600));
        QCOMPARE(shellWindowFloor(QSize(800, 600), QSize(-1, -1)), QSize(800, 600));
    }
};

QTEST_MAIN(ShellWindowFloorTest)
#include "shell_window_floor_test.moc"
