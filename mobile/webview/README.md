# The Web container on a phone

A Downloaded module runs in a webview: `WKWebView` on iOS,
`android.webkit.WebView` on Android, `QWebEngineView` on the desktop
(`../../app/web/`). This directory is the two phones' half.

Almost none of it is platform code. What each platform contributes is four
things — a webview whose requests arrive at the bridge, a script that runs
before the page's own, a URL to load, and a report when the page dies — and
everything else is plain C++ that a desktop test drives.

```
  MobileWebBridge            what a page may fetch, and how its frames cross
  MobileWebModuleView        liblogos' WebModuleView over one bridge + one page
  LiveRuntimeBudget          how many QML runtimes may be alive at once
  MobileWebContainerBackend  the factory, the registry, the budget
  AppMemory                  what this process is costing, for the budget's log
  WebPageProbe               one page, on the device, round-tripping a frame

  IosWebPage.mm              WKWebView + WKURLSchemeHandler
  AndroidWebPage.cpp         + android/src/co/logos/webview/LogosWebPage.java
```

## What the app ships so a `web` module can run

A page fetches two things off the container's origin: the module's own package,
and the app's bundled Qt-wasm QML runtime. `nix/mobile-web-assets.nix` builds
both into one directory — 25 MB of runtime and one 4 MB directory per module —
and each platform carries it the only way it can:

| | where | how it is found |
|---|---|---|
| iOS | `<App>.app/logos-runtime`, `<App>.app/web-modules` | resources are ordinary files, so the modules directory is handed to the core as-is (`iosWebModulesDir()`) |
| Android | `assets/logos-web`, unpacked once into the app's data directory | an APK asset is NOT a file: Qt reads `assets:/` through a virtual file engine that cannot answer `canonicalFilePath()`, which is how `LogosWebPaths::resolveUnder` refuses a traversal. `unpackAndroidWebAssets()` copies it out on first launch, stamped with the build's store path |

Both are architecture-free wasm and JavaScript, so the two phones and the
desktop serve the same bytes off the same URL shape.

**The Xcode copy is passed its destination.** `$<TARGET_BUNDLE_CONTENT_DIR>`
expands to a path containing Xcode's own `$(EFFECTIVE_PLATFORM_NAME)`, and CMake
escapes the dollar when it writes the post-build script — so the assets landed in
a directory literally called `Debug${EFFECTIVE_PLATFORM_NAME}` and the app
shipped nothing, silently. The runner already knows the `.app` path it will
install and passes it as `LOGOS_IOS_APP_BUNDLE_DIR`.

## Which container policy a phone can assert

None of liblogos'. A phone runs modules in TWO containers — Native for the
Bundled set's Bare images, Web for a Downloaded module's `web` variant — and
each policy names one container: `inproc` refuses a `web` variant by name, and
`web` refuses every Bare image. So the core is left on `auto` (the artifact
decides) and `BundledSetCoreRuntime::refuseSubprocessArtifacts()` asserts the
thing that is actually true of a phone: every module the core discovered is
`bare` or `web`, because a Qt plugin would need a subprocess module host and
neither platform allows one.

## Why the channel is a URL scheme

The desktop publishes a `QWebChannel` object into the page. Neither phone has
one, and the obvious substitute is ruled out on iOS: the mobile round-trip
spike (2026-09-09) found that a `WKScriptMessageHandler` traps in
`JSC::sanitizeStackForVM` when the page is entered under Qt's separate-main-stack
entry. What is left is the scheme the Qt-wasm loader needs for its documents
anyway, so it carries the frames too:

```
GET  <origin>/__logos/<token>/send?s&i&n&d   one frame, page -> host
GET  <origin>/__logos/<token>/poll           frames, host -> page (long poll)
GET  <origin>/__logos/<token>/close          the page has stopped serving
GET  <origin>/__logos/<token>/log?l&m        one console line
```

No port is bound — the whole exchange is inside the webview, which discharges
slice 28's fourth criterion by construction rather than by policy. The
per-launch token is checked anyway: it costs nothing, it is what the same
criterion demands of the loopback path if that is ever chosen instead, and it
means a document loaded into the webview by some other route cannot speak as the
module just by knowing the scheme. No `file://` load happens anywhere.

## Three things the devices decided

Each of these was found by running, and each shaped the design rather than
being worked around.

| what | where |
|---|---|
| **The frame travels in the query, chunked.** Neither phone hands its interceptor a request body: WebKit gives a `WKURLSchemeHandler` a request whose `HTTPBody` and `HTTPBodyStream` are nil for a `fetch` with a body, and `WebResourceRequest` has no body accessor at all. So the shim percent-encodes a frame, splits it at 4000 characters and sends one request per chunk with a sequence number. | `MobileWebBridge` |
| **Android serves the page over `https`.** A WebView loads the entry document off `logos://module/index.html` perfectly well and then refuses every fetch out of it — *"URL scheme \"logos\" is not supported"* — because Chromium's Fetch there only speaks the standard schemes whatever the embedder registered. The origin is a parameter, and Android uses `https://appassets.androidplatform.net`, the host androidx reserves for this. Measured on a Samsung, 2026-09-12. | `WebOrigin` in `../../app/web/LogosWebPaths.h` |
| **Android gets the shim inside the document.** There is no user-script API there, and `evaluateJavascript` runs after the page's own first script — too late for a loader that reads `window.logosQmlRuntimeBase` at the top of it. iOS uses `WKUserScript` at document start and the document on disk is untouched. | `setInjectsShimIntoHtml` |

## The live-runtime budget

The Qt-wasm QML runtime measured 185–240 MB resident and 2.6–3 s of cold start
per page (290 MB on a Samsung SM-G990B, measured 2026-09-12). A shell that kept
one per installed module would be killed by the OS, so the runtime is alive for
the module the user is looking at and, on a small device, for nothing else.

**The count comes from the device** (logos-workspace#153). It used to be a
fixed **one** everywhere, with the constant above carried only for the log line
and `AppMemory`'s reading consulted by nothing — so an iPad destroyed and rebuilt
a page on every flip between two apps, and a page that ballooned to 600 MB still
read as `1 runtime … 290 MB of 290 MB` and was evicted by nothing until the OS
killed the app. `LiveRuntimeBudget::forThisDevice()` now reads
`deviceMemoryBytes()` and gives the container two numbers:

| | how it is derived | what it does |
|---|---|---|
| the **count** | a sixth of the device's memory, divided by one page's 290 MB, floored at 1 and capped at 3 | how many UI pages may be live at once |
| the **ceiling** (iOS/macOS) | a third of the device's memory — under what the OS acts on, not at it | what the app as a whole may weigh before a page is shed on weight alone |
| the **ceiling** (Android/Linux) | the device's memory less the OS's own low-memory line (`ActivityManager.MemoryInfo.threshold`) and one page's margin | how much of the **device** may be in use before a page is shed on weight alone |

The ceiling has two rows because the figure it is read against does
(logos-workspace#244, below): on iOS the pages are charged to this process and
on Android they are not, so what is weighed there is the device's own book and
the ceiling has to be a level of device use rather than a share of the app.

The cap is not about memory: a user flips between two or three apps, and the
pages' real cost is in a renderer process this app cannot weigh (below), so
every page past the third is a bet against a number nobody here can read. A
simulator makes the same point loudly — it reports the Mac's 64 GB.

A run states its own count with `--web-budget <n>` (or
`LOGOS_WEB_RUNTIME_BUDGET`), which is what the `--drive web-budget` pass uses to
measure what two and three live runtimes cost on a device whose policy says one,
and its own ceiling with `--web-ceiling <MB>` (or `LOGOS_WEB_APP_CEILING_MB`),
which is how a device run makes the ceiling TRIP — put it a page's worth above
what the pass prints at rest and the next page is shed on weight alone. A
ceiling nobody has ever seen trip is indistinguishable from one that never
needed to, which is how logos-workspace#244 survived a landing.

**And the measurement is read.** Two entry points, both ending in the same
eviction the count makes:

* `observeMemory()` — what this **platform** weighs right now
  (`AppMemory::budgetWeighedBytes()`), against the ceiling. Called when a page is
  shown and on the container's existing 20 s poll-timer cadence, which is the
  only way growth *inside* a page is ever noticed. Over the ceiling it gives up
  ONE page per observation (the reading is never the page's own, so the container
  cannot know which page grew); it relaxes again only a page's worth below the
  ceiling, so it cannot oscillate around a number that moves by megabytes between
  frames.
* `memoryWarning()` — the OS asking, which is the one signal here that is not
  this app's own opinion. Everything but the visible page goes at once, because
  iOS kills an app rather than ask it twice. Subscribed through
  `watchAppMemoryPressure()`: `UIApplicationDidReceiveMemoryWarningNotification`
  on iOS, `ComponentCallbacks2.onTrimMemory` on Android (`LogosMemoryPressure.java`,
  levels `RUNNING_LOW` and worse — `UI_HIDDEN` is not pressure). Checkable on a
  device: `adb shell am send-trim-memory co.logos.basecamp.shell RUNNING_CRITICAL`.

`LiveRuntimeBudget` decides (least recently *visible* first, so flipping between
two modules does not evict the one being flipped to) and
`MobileWebContainerBackend::show()` announces. It does not act: the page belongs
to liblogos' container, which destroys it when the module is unloaded, and a
backend that destroyed one behind the container's back would leave a published
module with a dead channel.

**The books may only name a module this container has a page for**, and both
halves of that are logos-workspace#151. `show()` is the shell saying "the user
is looking at this one"; arriving for a module whose page has gone it is stale
by construction, so it says so and spends nothing — written into the books it
took the single runtime's slot and named the module that really was up for
eviction, so a tab press on a dead app unloaded the live one. And `forget()`
states what is left when a page goes away, because nothing did: the last word
about an unloaded module stayed `web_counter is visible; 1 live runtime(s), 290
MB of 290 MB` — true when it was printed, never taken back, and read beside the
Shell's `app web_counter is not mounted` as a container that had lost track of
its own pages. The console is the whole of a phone's diagnostic surface; an
accounting it only ever reports the good news of is not one.

`show()` also puts the module's page in FRONT of the host's own surface and
sends the others back (`PlatformPage::setFrontmost`). Both phones mount a page
at the BACK of the hierarchy when it is created, because a webview outside the
hierarchy is throttled and a Qt-wasm runtime drawing through
`requestAnimationFrame` would freeze — so "the user is looking at this module"
has to be a second instruction. Z-order rather than visibility, for the same
reason: a background module that is still answering calls needs its timers.

**What the log can and cannot weigh.** `AppMemory` reports this process's
footprint, and `show()` prints it. The page's own cost — 290–345 MB for the
first one, measured below — is NOT in that number: both phones run a webview's
content in a separate process (WebKit's WebContent, Chromium's sandboxed
renderer) that an embedder cannot ask about — iOS offers no API for another
task's footprint, and Android's renderer runs under an isolated uid, so its
`/proc` is not ours. What a shell controls, and what the budget states, is how
many pages are alive — so the ceiling above is honestly "the app is getting
close", never "that page is the problem".

**And on Android the app's own figure does not move with the pages at all**
(logos-workspace#230). Measured on 2026-09-17 with `--drive web-budget
--web-budget 3`, reading the renderer from the host side with `adb shell ps`:

| device | 0 / 1 / 2 / 3 live runtimes — `appResidentBytes()` | …and the app's Chromium renderer |
|---|---|---|
| Xiaomi 25028RN03Y, 2.7 GB | 303 / 435 / 434 / 435 MB | 0 / 290 / 298 / 300 MB |
| Samsung SM-G990B, 5.2 GB | 254 / 293 / 268 / 271 MB | 0 / 345 / 345 / 348 MB |

Two things follow, and both are about the budget rather than the log:

* **All of a build's pages share ONE renderer process.** The first `web`
  runtime costs about 290 MB there; the second and third cost about 5 MB each.
  `budgetBytes()` — `kDeviceRuntimeBytes` times the count — therefore states
  870 MB for three pages that in fact cost ~300 MB. The constant is right for
  the first page and wrong as a multiplier.
* **`appResidentBytes()` carries no per-page signal past the first page**, and
  on the eviction it carries the wrong sign: a memory warning that shed two
  pages took the renderer from 348 MB to 283 MB on the Samsung and from 300 MB
  to 222 MB on the Xiaomi, while the app's own figure went *up* 3 MB and 2 MB.
  `observe()` is therefore blind on this platform — it is weighing a number the
  thing it governs does not appear in.

`deviceAvailableBytes()` is the one reading a page does appear in on Android,
and the pass prints it as `device free`. It is the DEVICE's book, not the
app's — every other process is in it too — so it is reported beside the app's
figure and never instead of it; see `AppMemory.h`.

### What the budget weighs, per platform (logos-workspace#244)

The consequence of the table above is that `observe()`'s branch was
**unreachable on Android**: the Xiaomi's ceiling was 930 MB and the app sat at
435 MB holding three pages, so `observe()` answered `{}` on every observation of
every run and the policy silently degenerated to the fixed count #153 replaced.
`AppMemory::budgetWeighedBytes()` is the fix, and it is one call with two
answers:

| platform | what it answers | why |
|---|---|---|
| iOS / macOS | `appResidentBytes()` — this process's `phys_footprint` | the pages are charged here, and #153's reading was right |
| Android / Linux | `deviceMemoryBytes() - deviceAvailableBytes()` — how much of the device is in use | the pages are in a Chromium renderer that is not this process, and the device's book is the only one it appears in |

Which of the two frames this platform is in is asked **once**, as
`AppMemory::kBudgetWeighsTheDevice`. Four things have to agree about it and none
can tell on its own that it disagrees: the figure above, the ceiling
`forThisDevice()` pairs with it, the name a device log prints for it, and how
far the `--drive web-budget` pass expects a page to move it. A second copy of
the platform condition that drifted from the first would put the two halves of
the comparison in different frames — silently, which is how #244 lasted a whole
landing.

**Can an app weigh its own renderer in-process?** That answer would change this
design — the renderer's own figure beats the device's book on every count — so
the `--drive web-budget` pass prints the evidence rather than assuming, as
`web budget: process visibility — …` (`AppMemory::processVisibilityReport()`):
what `ActivityManager.getRunningAppProcesses()` returns, and how many pids
`/proc` shows this uid. Compare it against the host's `adb shell ps -A -o
PID,RSS,NAME`, which does see the renderer.

**Why the OS's own line and not a fraction.** `MemAvailable` is the whole
device's, so "the app is over its share" cannot be read off it, and no absolute
headroom is right on two devices at once: the Xiaomi lost its renderer with
1.24 GB still available while the Samsung was untroubled at 1.05 GB.
`ActivityManager.MemoryInfo.threshold` is the level at which *that* device's
system starts killing background processes, computed per device, and is the one
per-device calibration Android gives an app. `AppMemory::deviceLowMemoryBytes()`
reads it by JNI reflection; where it is not available (plain Linux, or before
there is a context) the fallback is an eighth of the device.

**And the stated cost is one renderer, not one per page.** Since all pages share
a renderer, `budgetBytes()`/`projectedBytes()` are now `kDeviceRuntimeBytes` for
the first page plus `kAdditionalRuntimeBytes` (8 MB) for each after it, instead
of the count times 290 MB — which had every device log over-reporting three live
runtimes by about 3x. The **count** deliberately stays on the renderer's price:
divided by 8 MB every device would afford the cap and the count would stop being
a device question at all, which is #153's fixed number arrived at from the other
side.

**The pass says whether the figure moved.** `--drive web-budget` records the
weighed figure at 0, 1, … N live runtimes and again after the memory warning,
and prints `WRONG:` when the pages did not move it or when shedding them did not
take it back down. The last of those is the discriminating one: the blind
reading passes the others (starting a renderer does cost the app something) and
fails only on the sign.

Measured 2026-09-17 with `--drive web-budget --web-budget 3`, `app` being
`appResidentBytes()` and `weighed` being what the budget now reads:

| device | RAM | ceiling | `app` — 0/1/2/3 live, then after the shed | `weighed` — 0/1/2/3, then after the shed |
|---|---|---|---|---|
| Xiaomi 25028RN03Y | 2789 MB | 2283 MB | 305 / 400 / 382 / 369 → **365** | 1381 / 1508 / 1501 / 1517 → **1410** |
| Lenovo TB520FU | 15275 MB | 14769 MB | 387 / 501 / 483 / 484 → **486** | 5349 / 6379 / 6311 / 6191 → **5959** |

The app's figure goes the wrong way on both — a shed of two pages left it 4 MB
lower on the phone and 2 MB *higher* on the tablet — while the weighed figure
falls 107 MB and 232 MB, which is the renderer the host's `ps` sees give its
pages back.

The floor the pass checks those movements against is per platform: a third of a
renderer where the device's book carries the whole of it, and the noise figure
on iOS, where this process is charged only part of the WebContent process — a
page moved `appResidentBytes()` 178 → 221 MB on a physical iPad Air 4 (#153),
a seventh of the Android signal.

**Steps past the first are printed, not asserted.** The pages share a renderer,
so the second and third cost ~5 MB each — under the noise of a figure the whole
device is in. On the Lenovo the second page's step read −68 MB and the third's
−120 MB with nothing shed; the first page's step was +1031 MB.

**And the renderer is NOT reachable in-process.** `processVisibilityReport()` on
both devices:

```
getRunningAppProcesses() -> 1: co.logos.basecamp.shell(pid 1284);
/proc shows 1 pid(s) to this uid: 1284; this process is pid 1284
```

while the host's `ps -A -o PID,RSS,NAME` at the same instant showed
`com.google.android.webview:sandboxed_process0:…` at pid 29955 holding 308 MB.
The renderer is an *isolated* process with a uid of its own, so it is in neither
list: `getRunningAppProcesses()` answers the caller's uid only, and `/proc` is
mounted with `hidepid`. That is why the device's book is read and not the
renderer's own figure — the renderer's figure would be better, and this app
cannot have it.

**iOS is left alone, and the first iOS run of the check questioned that.** #244
fixed Android on the instruction that `phys_footprint` already moves with the
pages on iOS. On an iPad Air 13-inch (M2) simulator (2026-09-17) it read
`101 / 133 / 152 / 151 MB` for 0/1/2/3 live runtimes and **152 MB after a shed
of two pages** — 1 MB higher, the same wrong sign Android had. #153's physical
iPad Air 4 figures have the same shape (`178 / 221 / 221`, a second page costing
0 MB) but no after-shed reading was ever taken there. The metric was NOT changed
on Darwin; the measurement that settles it needs a physical device and is
logos-workspace#254, and the pass names that issue beside the `WRONG:` so an iOS
run is not read as a fresh regression.

**The ceiling trips, and it was watched doing it.** With `--web-ceiling 1440` on
the Xiaomi (at rest 1294 MB in use), the first page took the figure to 1596 MB,
the allowance dropped 3 → 1 on that observation, and each page opened after it
gave the previous one up: `1596 → 1537 → 1492 MB` with one live runtime held
throughout. That is the branch #153 wrote, acting for the first time.

**A page can be taken away without the budget doing it.** On the Xiaomi, two
runs holding two live pages through a 60-second idle wait had the shared
renderer reaped (`onServiceDisconnected (crash or killed by oom)`, both pages
lost at once) with `MemAvailable` still at 1.24 GB and the app 500 MB under its
ceiling. `forget()` is what keeps the books straight when that happens; nothing
in the budget saw it coming.

**Known limit, and it is the artifact's.** A `ui_qml` module's `web` variant
today is ONE page carrying both the QML runtime and the module's own Qt-wasm
backend image (logos-module-builder's `buildWebViewModule.nix`), so giving the
UI up gives the Wasm host up with it and the module stops answering calls until
it is shown again. Slice 28 asks for a background module to keep answering, and
that needs the variant to ship a second, headless entry document — the Bare
`web` variant's loader page — with the container relaying between the two.
Nothing here changes when it does: the budget already governs UI pages only.

## What is tested where

| | runs | proves |
|---|---|---|
| `nix build .#unit-tests` (`tests/mobile_web_*_test.cpp`, `tests/live_runtime_budget_test.cpp`) | seconds, any desktop | what the bridge, the view and the budget decide |
| `nix build .#mobile-bridge-test` | seconds, any desktop with Qt WebEngine | the JavaScript the bridge ships — the poll loop, the chunked sender, the console wrapper — in a real browser |
| the smoke host's `web probe` line | a device or simulator | that THIS platform's webview delivers a request to its interceptor when the page is entered under Qt's separate main stack |
| the smoke host's `web modules` line (`WebModuleRunner`) | a device or simulator | that a `ui_qml` module's `web` variant LOADS through the real core into this platform's webview, what its UI costs to cold-start here, and that showing a second one puts the first over the budget |
| the Shell's `--drive web-budget` pass (`ShellWebBudgetDriver`) | a device or simulator | what the app WEIGHS with one, two and three `web` runtimes live on this device, what a memory warning sheds, and whether the figure the budget reads actually MOVED with the pages (#244) |

### A page's console arrives on a background thread

On Android it does: `shouldInterceptRequest` runs on a Chromium thread, which is
what makes the long poll free. Every sink the bridge calls therefore hops to the
Qt main thread before it touches anything Qt owns. `WebPageProbe` did not, and
appended straight into the host's `QPlainTextEdit`: SIGSEGV inside
`QTextDocumentPrivate::insert`, taking the app down mid-probe. Measured on a
Samsung SM-G990B, 2026-09-12.

The probe is the one that cannot be replaced. It reads:

```
[smoke] web probe: page log: probe page is listening
[smoke] web probe: the page loaded off logos://module/index.html and published a channel (509 ms)
[smoke] web probe: round trip OK -- probe-pong:logos://module/logos-runtime/ (519 ms)
[smoke] web container: PASS
```

and it is what found the Android scheme problem above.

A page opened BEFORE the event loop has turned — which the probe does — finds no
`UIWindow` and says so. The channel works regardless (a `WKWebView` runs its
JavaScript detached) but `requestAnimationFrame` is throttled, so a QML runtime
in such a page would freeze. Modules are loaded from the event loop and are
unaffected.
