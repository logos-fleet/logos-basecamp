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
| the **ceiling** | a third of the device's memory — under what the OS acts on, not at it | what the app as a whole may weigh before a page is shed on weight alone |

The cap is not about memory: a user flips between two or three apps, and the
pages' real cost is in a renderer process this app cannot weigh (below), so
every page past the third is a bet against a number nobody here can read. A
simulator makes the same point loudly — it reports the Mac's 64 GB.

A run states its own count with `--web-budget <n>` (or
`LOGOS_WEB_RUNTIME_BUDGET`), which is what the `--drive web-budget` pass uses to
measure what two and three live runtimes cost on a device whose policy says one.

**And the measurement is read.** Two entry points, both ending in the same
eviction the count makes:

* `observeMemory()` — what the app weighs right now, against the ceiling. Called
  when a page is shown and on the container's existing 20 s poll-timer cadence,
  which is the only way growth *inside* a page is ever noticed. Over the
  ceiling it gives up ONE page per observation (the reading is the host's, not
  the page's, so the container cannot know which page grew); it relaxes again
  only well under the ceiling, so it cannot oscillate around a number that moves
  by megabytes between frames.
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
footprint, and `show()` prints it. The page's own 185–240 MB is NOT in that
number: both phones run a webview's content in a separate process — WebKit's
WebContent, Chromium's sandboxed renderer — that an embedder cannot ask about
(iOS offers no API for another task's footprint, and Android's renderer runs
under an isolated uid, so its `/proc` is not ours). What a shell controls, and
what the budget states, is how many pages are alive — so the ceiling above is
honestly "the app is getting close", never "that page is the problem".

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
| the Shell's `--drive web-budget` pass (`ShellWebBudgetDriver`) | a device or simulator | what the app WEIGHS with one, two and three `web` runtimes live on this device, and what a memory warning sheds |

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
