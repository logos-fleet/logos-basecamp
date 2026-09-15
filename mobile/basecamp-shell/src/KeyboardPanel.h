// WHICH panel the platform drew over a focused field.
//
// `QInputMethod::isVisible()` answers "a panel is up", and on iOS that is two
// different things: a software keyboard, or the shortcut bar iOS draws INSTEAD
// of one while a hardware keyboard is connected -- visible, with a rectangle,
// and with nothing on it to type with. Every simulator in the fleet connects a
// hardware keyboard by default, so the bar is what a simulator run reports and
// the difference is the whole of logos-workspace#152's open question.
//
// The height against the screen is the only thing that separates them, and the
// two measurements it has to separate are real (logos-workspace#170):
//
//   iPad Air 11-inch simulator, hardware keyboard: 820x69  of 820x1180
//   iPad Air (4th generation) device, keyboard:    820x337 of 820x1180
//
// A ratio rather than a height in points, because the same keyboard covers far
// fewer points on a phone than on an iPad. Kept apart from ShellKeyboardDriver
// so that the rule over those numbers is testable without a device, an app or
// a scene.
#pragma once

#include <QRectF>

namespace basecamp::shell {

enum class KeyboardPanel {
    None,         // the platform drew nothing at all
    Unmeasured,   // a panel is up and there is no geometry to judge it by
    ShortcutBar,  // iOS's hardware-keyboard bar: a panel with no keys
    Keyboard,     // a software keyboard
};

// `panel` is QInputMethod::keyboardRectangle() and `screen` the DISPLAY's
// geometry, both in logical points. A panel shorter than a sixth of the screen
// is a bar: the two measured shapes above sit either side of that by a wide
// margin (6% and 29%), and no software keyboard is that short.
KeyboardPanel panelDrawn(bool inputMethodVisible, const QRectF& panel, const QRectF& screen);

} // namespace basecamp::shell
