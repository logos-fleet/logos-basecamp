// The minimum size the Shell's window may declare, on a screen that may be
// smaller than the one the desktop layout was drawn for.
#pragma once

#include <QSize>

namespace basecamp {

// `desired` bounded to what `screen` can actually show.
//
// A minimum size is a request to a WINDOW MANAGER: on the desktop it stops a
// user dragging the window narrower than the layout works at. A phone has no
// window manager and no drag -- the window is the screen -- so a minimum
// larger than the screen is not honoured by making the window bigger, it is
// honoured by letting the LAYOUT overflow: Qt lays the children out at the
// minimum and the far side of them ends up past the edge of the display, where
// no touch can reach it. Measured on the iPhone 16 Pro simulator
// (logos-workspace#87): a 800-pt floor on a 402-pt screen put the Modules
// tab's Load/Unload control ~300 pt off the right edge.
//
// An invalid or empty `screen` (no platform screen to ask) leaves `desired`
// alone -- an unknown screen is not evidence of a small one.
QSize shellWindowFloor(const QSize& desired, const QSize& screen);

// The same, for the screen this process is actually on. Split from the
// arithmetic above so the decision is testable without a display.
QSize shellWindowFloorForThisScreen(const QSize& desired);

} // namespace basecamp
