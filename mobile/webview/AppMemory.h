#pragma once

#include <QString>
#include <QtGlobal>

#include <functional>

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

// HOW MUCH MEMORY THIS DEVICE HAS, in bytes, or -1 where the platform will not
// say -- `hw.memsize` on iOS/macOS, `MemTotal` on Android/Linux.
//
// It is here because it is the other half of the same question and is measured
// the same way: what the app costs, and what the thing it is running on can
// afford. #153 is that the second half was never asked -- the live-runtime
// budget was a fixed count of ONE on a 2 GB phone and on a 16 GB tablet alike,
// justified by a constant sampled once on a Samsung.
//
// A SIMULATOR ANSWERS THE MAC'S MEMORY, which is not a bug to be corrected here
// (there is no honest way for a process to tell) and is why the policy that
// reads this caps its count -- see LiveRuntimeBudget::kMaxLiveRuntimes.
qint64 deviceMemoryBytes();

// HOW MUCH OF THAT IS STILL FREE, in bytes, or -1 where this platform has no
// such figure -- `MemAvailable` from /proc/meminfo on Android/Linux.
//
// THE ONLY NUMBER ON ANDROID IN WHICH A PAGE'S REAL COST APPEARS, which is why
// it is here (logos-workspace#230). appResidentBytes() above is this process
// and the page is not in this process: measured on a Xiaomi 25028RN03Y
// (2.7 GB) on 2026-09-17, the first `web` page moved the app's own figure by
// 101 MB and the second by 1 MB, while the Chromium renderer that actually
// holds them both went from nothing to 255 MB. `MemAvailable` moved by the
// renderer's amount, because it is the device's book and the renderer is on it.
//
// IT IS NOT THE APP'S NUMBER AND MUST NOT BE READ AS ONE. Every other process
// on the phone is in it too, so it falls when something else grows and rises
// when the OS reclaims -- it says what room is left, never what this app is
// spending. Reported beside appResidentBytes() rather than instead of it.
//
// -1 ON DARWIN, deliberately. iOS charges jetsam a per-process footprint and
// publishes no device-wide free figure to an app; the honest answer there is
// that there is no such reading, not `hw.memsize` minus a guess.
qint64 deviceAvailableBytes();

// HOW MUCH ROOM THE OS ITSELF WANTS KEPT FREE on this device, in bytes, or -1
// where the platform does not publish one -- `ActivityManager.MemoryInfo`'s
// `threshold` on Android, which is the level at which the system starts killing
// background processes to get memory back.
//
// IT IS THE ONE PER-DEVICE CALIBRATION ANDROID GIVES AN APP, and that is why it
// is read rather than a fraction being invented: logos-workspace#244 measured
// the same absolute headroom meaning opposite things on two phones -- a Xiaomi
// 25028RN03Y reaped the shared renderer with MemAvailable still at 1.24 GB
// while a Samsung SM-G990B was untroubled at 1.05 GB. A policy stated as "this
// many MB free" cannot be right on both; one stated against the device's own
// line can.
//
// READ THROUGH JNI, so it answers -1 until there is an Android context --
// implemented in AndroidWebPage.cpp beside the other JNI reading in this header
// and answered with -1 in AppMemory.cpp everywhere else, plain Linux included
// (there is no ActivityManager there).
qint64 deviceLowMemoryBytes();

// WHAT THE LIVE-RUNTIME BUDGET WEIGHS ON THIS PLATFORM, in bytes, or -1 where
// there is nothing here that moves with a page.
//
// ONE CALL, TWO FRAMES, and the split is the whole of logos-workspace#244:
//
//   * iOS/macOS: appResidentBytes(). The pages are charged to this process, so
//     the process's own footprint is the figure the budget is spent against and
//     #153's reading was right here.
//   * Android/Linux: deviceMemoryBytes() - deviceAvailableBytes(), i.e. HOW
//     MUCH OF THE DEVICE IS IN USE. The pages are NOT in this process -- every
//     `web` page of a build shares one Chromium renderer that is a separate
//     process of its own -- so appResidentBytes() is blind to them, and was
//     measured carrying the WRONG SIGN: on 2026-09-17 a memory warning that
//     shed two pages took the renderer down 65 MB while the app's own figure
//     went UP 3 MB. A budget reading that number learns that evicting costs
//     memory. `MemAvailable` is the book the renderer is on.
//
// THE ANDROID FIGURE IS NOT THIS APP'S and must not be read as one: every other
// process on the device is in it. What it supports is the question the budget
// actually has to answer on a phone -- "is there still room on this device for
// the pages I am holding" -- which is why the ceiling it is read against is
// stated in the same frame (LiveRuntimeBudget::ceilingForDeviceInUse) rather
// than as a share of the app.
qint64 budgetWeighedBytes();

// WHAT THIS PROCESS CAN SEE OF THE PAGES' RENDERER, as one line for a device
// log. Never a decision -- evidence, and it is here because
// logos-workspace#244 asked for the answer to be stated with its evidence
// rather than assumed: if an app CAN weigh its own WebView renderer in-process
// then the Android reading above is the wrong design and should be replaced by
// the renderer's own figure.
//
// On Android it reports what `ActivityManager.getRunningAppProcesses()` returns
// and how much of /proc this app's uid can see; everywhere else it says the
// question does not arise.
QString processVisibilityReport();

// SUBSCRIBE TO THE PLATFORM'S MEMORY WARNING. `onWarning` is called on the Qt
// main thread when the OS says it wants memory back; returns false where this
// platform has no such signal, and then the container is left with the polling
// half alone.
//
// iOS posts UIApplicationDidReceiveMemoryWarningNotification and then kills the
// app if nothing changes -- it does not ask twice. Android calls
// ComponentCallbacks2.onTrimMemory with a level, and the levels that mean "you
// are about to be killed" (RUNNING_LOW and worse) are the ones passed on here.
//
// Implemented per platform beside that platform's page (IosWebPage.mm,
// AndroidWebPage.cpp) because both signals arrive through the app's own UI
// object, and answered with `false` in AppMemory.cpp everywhere else.
bool watchAppMemoryPressure(std::function<void()> onWarning);

// A byte count as whole megabytes, which is the unit every line about memory in
// the Web container is stated in -- the budget's, the container's and the smoke
// host's. One spelling of it, so two logs of the same number never disagree
// about their rounding.
QString megabytes(qint64 bytes);

} // namespace basecamp::web
