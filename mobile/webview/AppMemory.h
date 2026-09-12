#pragma once

#include <QtGlobal>

namespace basecamp::web {

// WHAT THIS PROCESS IS COSTING, in bytes, or -1 where the platform will not say.
//
// Slice 28 asks for the app's memory to "return to within a stated budget,
// logged" when a Downloaded module gives up its UI page, and a budget nobody
// weighs is a policy rather than a measurement. This is the weighing.
//
// WHICH NUMBER, and why it differs per platform:
//
//   * iOS/macOS: `phys_footprint` from TASK_VM_INFO. Not resident size --
//     footprint is what jetsam kills an app for, so it is the number the budget
//     is actually spent against, and a WKWebView's WebContent process is NOT in
//     it (it is a separate process the system charges partly to this one).
//   * Android/Linux: VmRSS from /proc/self/statm, which is the whole of what
//     this process holds. Chromium's renderer for a WebView is again its own
//     process.
//
// SO THE NUMBER IS THE HOST'S, NOT THE PAGE'S, on both phones, and that is the
// honest thing to log: what a shell can measure about itself. The page's own
// cost is the platform's to report and neither phone offers it to an embedder.
qint64 appResidentBytes();

} // namespace basecamp::web
