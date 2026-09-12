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
  WebPageProbe               one page, on the device, round-tripping a frame

  IosWebPage.mm              WKWebView + WKURLSchemeHandler
  AndroidWebPage.cpp         + android/src/co/logos/webview/LogosWebPage.java
```

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
per page. A shell that kept one per installed module would be killed by the OS,
so the budget is **one** on a phone: the runtime is alive for the module the
user is looking at and the others give theirs up.

`LiveRuntimeBudget` decides (least recently *visible* first, so flipping between
two modules does not evict the one being flipped to) and
`MobileWebContainerBackend::show()` announces. It does not act: the page belongs
to liblogos' container, which destroys it when the module is unloaded, and a
backend that destroyed one behind the container's back would leave a published
module with a dead channel.

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
