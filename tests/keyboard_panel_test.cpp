// srcdeps: basecamp-shell/src/KeyboardPanel.cpp
//
// WHAT THE PLATFORM DREW WHEN THE FIELD TOOK THE INPUT METHOD.
//
// logos-workspace#152 fixed the focus half: tapping a chat_ui field now hands
// the input context an object that answers Qt::ImEnabled, and iOS reports a
// panel. What it could not settle is WHICH panel, because every simulator in
// the fleet connects a hardware keyboard by default and iOS then draws its
// shortcut bar -- a panel that is visible, has a rectangle, and has nothing on
// it to type with. #170 measured the difference on the venue's physical iPad
// Air (4th generation), where no hardware keyboard is attached:
//
//   simulator, hardware keyboard connected:  820x69  of an 820x1180 screen
//   device, full software keyboard:          820x337 of an 820x1180 screen
//
// Both are `QInputMethod::isVisible() == true`. The height against the screen
// is the only thing that tells them apart, so the rule that reads it is the
// thing worth pinning: those two numbers are the whole of #152's open question
// and they came from two device runs that cannot be repeated in a check.
//
// Run: nix build .#unit-tests -L
#include "basecamp-shell/src/KeyboardPanel.h"

#include <QtTest/QtTest>

using basecamp::shell::KeyboardPanel;
using basecamp::shell::panelDrawn;

namespace {
// The venue's iPad Air (4th generation) in portrait, in Qt's logical points --
// the units QScreen::geometry() and QInputMethod::keyboardRectangle() are both
// in, and the units both measurements above were taken in.
const QRectF kIPadScreen(0, 0, 820, 1180);
// The iPhone 16 Pro simulator, the fleet's only handset.
const QRectF kPhoneScreen(0, 0, 402, 874);
} // namespace

class KeyboardPanelTest : public QObject
{
    Q_OBJECT
private slots:
    // #170's measurement, from the physical iPad: a third of the screen.
    void fullKeyboardOnTheDevice()
    {
        QCOMPARE(panelDrawn(true, QRectF(0, 843, 820, 337), kIPadScreen),
                 KeyboardPanel::Keyboard);
    }

    // #152's measurement, from the iPad Air 11-inch simulator: 69 points of
    // shortcut bar, which is what a CONNECTED hardware keyboard leaves behind.
    void shortcutBarOnTheSimulator()
    {
        QCOMPARE(panelDrawn(true, QRectF(0, 1111, 820, 69), kIPadScreen),
                 KeyboardPanel::ShortcutBar);
    }

    // The same two shapes on the handset, where a third of a 874-point screen
    // is a much smaller number than a third of an iPad's: the rule is a ratio
    // for that reason and not a height in points.
    void theSameTwoShapesOnAPhone()
    {
        QCOMPARE(panelDrawn(true, QRectF(0, 583, 402, 291), kPhoneScreen),
                 KeyboardPanel::Keyboard);
        QCOMPARE(panelDrawn(true, QRectF(0, 819, 402, 55), kPhoneScreen),
                 KeyboardPanel::ShortcutBar);
    }

    // Nothing drawn at all -- the case a run gets when Qt's half is right and
    // the platform declines. The rectangle is not consulted.
    void noPanelWhenTheInputMethodIsNotVisible()
    {
        QCOMPARE(panelDrawn(false, QRectF(0, 843, 820, 337), kIPadScreen),
                 KeyboardPanel::None);
        QCOMPARE(panelDrawn(false, QRectF(), kIPadScreen), KeyboardPanel::None);
    }

    // isVisible() turns true on the platform's will-show notification and the
    // geometry arrives WITH it, so a driver that gives up waiting sees a panel
    // with no rectangle. That is not a shortcut bar -- calling it one would put
    // "this device has a hardware keyboard connected" in the log of a run that
    // measured nothing.
    void visibleWithNoGeometryIsNotABar()
    {
        QCOMPARE(panelDrawn(true, QRectF(), kIPadScreen), KeyboardPanel::Unmeasured);
        QCOMPARE(panelDrawn(true, QRectF(0, 1180, 820, 0), kIPadScreen),
                 KeyboardPanel::Unmeasured);
    }

    // And with no screen to measure against there is no ratio, so there is no
    // verdict either.
    void noScreenMeansNoVerdict()
    {
        QCOMPARE(panelDrawn(true, QRectF(0, 843, 820, 337), QRectF()),
                 KeyboardPanel::Unmeasured);
    }
};

QTEST_MAIN(KeyboardPanelTest)
#include "keyboard_panel_test.moc"
