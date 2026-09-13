# Basecamp's Shell on a phone

The real UI shell — `src/`, `main_ui`, the same sources the desktop plugin is
built from — running on **both phones** over the app's **Bundled set**. Its
Modules tab lists the set, and its Load/Unload buttons go through the Native
container.

```bash
ws run logos-basecamp --target ios-sim-arm64 --bundle view_counter --app shell

# ...the milestone's own set: the real Chat app over the networking modules,
# against a desktop peer (see the section below)
LOGOS_IOS_TEAM_ID=<team> LOGOS_IOS_DEVICE=<udid> \
  ws run logos-basecamp --target ios-arm64 --app shell \
  --bundle capability_module,libp2p_module,chat_ui \
  -- --chat-peer <the desktop installation's get_address>

# ...and the same Shell on Android. No `chat_ui`: the mobile catalog
# publishes no ui_qml variant for this platform, so the set is the four core
# modules and the sidebar carries no app tile (see "One Shell, two phones").
ws run logos-basecamp --target android-arm64 --app shell \
  --bundle capability_module,libp2p_module,delivery_module,chat_module \
  -- --peer <the desktop libp2p peer's multiaddr> \
     --chat-peer <the desktop installation's get_address>
```

`chat_ui` is one name and three modules: its closure is `chat_ui ->
chat_module -> delivery_module`, read off logos-chat-ui's own metadata.
`capability_module` and `libp2p_module` are named beside it and are not in that
closure on purpose -- neither is a chat_ui dependency (the first is how ANY
module-to-module call mints its token, the second is the transport delivery
dials), and writing ambient infrastructure into a signed manifest would be a
claim the core would later act on.

## What is and is not different from the desktop

Nothing above `IShellHost` is different. The Shell is handed one `IShellHost*`,
the ABI check still runs, and `main_ui` still links Qt and the design system and
nothing else. Three things below it are:

|  | desktop | here |
|---|---|---|
| how the Shell is loaded | `QPluginLoader` opens `main_ui.dylib` | linked in and reached through `QPluginLoader::staticInstances()` — on iOS because Qt is static archives and there is no plugin to open (ADR 0001); on Android because an APK gives an app exactly one directory it may `dlopen` from and a plugin path inside it is a second problem on top of a packaging one |
| where the module set comes from | a modules **directory**, scanned | the Bundled-set **manifest** the build compiled in; a phone may not download native code (ADR 0003) |
| what answers the QML `backend` | `MainUIBackend` over three managers | `ShellModulesBackend`: the Modules tab, the app launcher and the App Manager are live, over the desktop app's own `ModuleInstanceModel` + `CoreModuleManager` + `ICoreRuntime` plus [`../appmanager/`](../appmanager/README.md); repositories say they are not here yet |
| where an app's backend runs | a `ui-host` **subprocess**, over a local socket | in THIS process, with the same typed replica carried over a socketpair — no subprocess a store will accept (ADR 0003) |

The runtime underneath is `BundledSetCoreRuntime`, shared with
[`../liblogos-smoke`](../liblogos-smoke/README.md) — the two apps are the same
host with different UIs on top, which is what makes the probe useful when the
Shell cannot see a module: it says whether the module or the Shell is the
reason.

## What the run prints

The host drives the Modules tab once, after the first frame, and an automated
run reads the verdicts off the console:

```
[shell] shell: IShellHost ABI 3 (host 3)
[shell] COLD START: Shell shown at 812 ms
[shell] shell: Settings -> Module Inspector is on screen
[shell] modules tab rows: bare_counter, capability_module, view_counter
[shell] app ships:        bare_counter, capability_module, view_counter
[shell] SHELL MODULES TAB LISTS WHAT THE APP HAS
[shell] all 3 shipped row(s) are installType 'embedded'; 0 downloaded
[shell] modules tab stats: bare_counter 0.0%/1.4 MB, capability_module 0.0%/0.9 MB, ...
[shell] SHELL MODULES TAB SHOWS THE SET'S STATS
[shell] load bare_counter
[shell]   bare_counter loaded in 6 ms (Native container)
[shell] unload bare_counter
[shell] drive modules: bare_counter not loaded -> loaded -> not loaded
[shell] SHELL MODULES TAB ROUND TRIP OK
```

The rows are counted off the **scene**, not off the model: each row's status
badge carries its module's name, so a filter proxy that dropped a row or a
table showing a module the manifest does not account for fails there. So do
the two lines under it — `installType` is what Basecamp answers "where did
this come from" with everywhere else and `embedded` is its answer for
something the build put there, and the stats are read off the rendered cells
rather than the model, because a model carries cpu and memory whether or not
the table ever drew them.

## Chat, and the two cold-start numbers

With the networking modules in the set the Shell also brings chat up — the
same `NetworkSmokeRunner` the [smoke probe](../liblogos-smoke/README.md) uses,
over the same host — and reports the two numbers slice 22 asks for:

```
[shell] COLD START: Shell shown at 812 ms
[shell] chat_module: health() ok (the Rust core answers)
[shell] chat_module: init ok
[shell]   delivery node online in 1043 ms
[shell] COLD START: Chat usable at 2104 ms
[shell]   two-party group: b3088251e1
[shell]   invited the desktop peer: 2a53f28cdba6...
[shell]   the group committed the desktop peer in 3820 ms (roster: [...])
[shell]   sent 'phone-6631fefa' to the group
[shell]   message from the desktop peer: 'desktop-4471'
[shell] CHAT GROUP MESSAGE ROUND TRIP OK (9633 ms)
```

Chat runs BEFORE the Modules tab is driven, and that ordering is the
measurement: "cold start to Chat usable" is how long a user waits before they
can type, and the tab's own 2.5-second settle in front of it would be reported
as part of the chat bring-up. Both run off the event loop, so the Shell is on
screen and painting throughout.

Standing the desktop peer up is
[`../liblogos-smoke/desktop-peers/`](../liblogos-smoke/desktop-peers/README.md).

## The App Manager

`backend.appManager` is a `StoreAppManager`
([`../appmanager/`](../appmanager/README.md)): the catalog, per-variant
availability, the signer-trust prompt and the per-module consent prompt. Every
decision in it is platform-free and unit-tested on a desktop; what this directory
adds is `src/ShellStoreBackend.cpp`, the one seam, over package_downloader,
package_manager, capability_module and `QDesktopServices`.

`main.cpp` starts it once the set is up, and the run says what it got:

```
[shell] app manager: catalog not in this build, consent armed
```

Two independent halves, and both are normal answers rather than errors. A Store
shell's Bundled set is data (ADR 0007), so a build that carries neither package
module cannot browse a catalog and says so — an empty App Manager would read as
"the catalog has nothing in it", which is a different and much more alarming
claim. Consent is separate because `capability_module` is in every set that calls
anything, so a shell with no catalog still prompts for a Downloaded module an
earlier launch installed.

Two things the device settled, both about LAZY LOADING (SM-G990B, 2026-09-13):

| | |
|---|---|
| **`loadedModules()` is not "does this build have it".** The Native container loads a Bundled member lazily, so gating on it reported `consent unavailable` in a build that carries capability_module, because nothing had called it yet. Presence is `knownModules()`; `onEventWhenAvailable` is built to arm against a module still coming up. | `ShellStoreBackend::isPresent` |
| **Nothing else calls the package modules.** If the App Manager does not load them, nobody does — so `configure()` loads both before answering `hasCatalog()`, rather than waiting for a first call that would fail. | `ShellStoreBackend::ensureLoaded` |

And one that is a C++ trap rather than a platform one: `ShellStoreBackend` is
constructed *from* `m_api` and `m_modules`, and members are initialised in
DECLARATION order whatever the constructor's initialiser list says. Declared
above those two it captured uninitialised pointers and the Shell took a SIGSEGV
inside `CoreModuleManager::loadedModules()` on the first catalog probe — before
the first frame, so the app died with the Shell never on screen.

## One Shell, two phones

`main_ui` and the design system are cross-built as **static archives** for
both targets — [`../../nix/shell-ui-ios.nix`](../../nix/shell-ui-ios.nix) and
[`../../nix/shell-ui-android.nix`](../../nix/shell-ui-android.nix) — and
`src/main.cpp` above them is one file. Everything that differs is in the two
platform projects (`app/` + the Xcode step on iOS, `android/` + gradle on
Android), and it is three things:

| | iOS | Android |
|---|---|---|
| the app image | an `.app` linked and signed by Xcode, so the build is pure up to one static archive and impure above it (ADR 0002) | one nix derivation: `androiddeployqt` and gradle both run in the sandbox |
| how `import QtQuick` resolves | static Qt QML plugins in the EXECUTABLE, chosen by `qmlimportscanner` at link time | shared Qt QML modules PACKAGED in the APK, chosen by `qmlimportscanner` at deploy time. Same scan, same input — the Shell's QML is compiled bytecode, so both are pointed at the trees it came from (`shellUi.qmlScanRoots`) |
| the SVG icons | the `qsvg` archive linked and `Q_IMPORT_PLUGIN`ed by hand | the `qsvg` plugin named in `QT_PLUGIN_TARGETS` by hand. One cause: Qt finds a module's plugins by globbing its OWN cmake directory, and qsvg's config lives in the qtsvg prefix — so nothing asks for it and every icon renders as an empty square |
| the Bundled set's apps | `ui_qml` members are mounted from a sidebar tile | none: logos-module-builder publishes no `view` output for Android, so no set built here carries one and the launcher is empty |

What is NOT different: the host, the runtime seam, the Modules tab, the chat
bring-up, and both cold-start numbers. On an SM-G990B the Shell is on screen
in ~900 ms and chat is usable in ~2.8 s, against ~300–400 ms and ~1.5–2.4 s on
the iPads — the gap is the phone, not the platform code.

### Unloading a threaded Bare module kills the Android process

Driving the Modules tab's Load/Unload on `chat_module` takes the app down with
no tombstone and no `am_kill`, right after `In-process module stopped`. The
container does the careful thing (`InProcContainer::terminate` asks the module
to unload, stops the dispatch thread, then `dlclose`s), and on iOS `dlclose`
of a framework does not actually unmap — so the same sequence is survivable
there and fatal here, where bionic really unmaps an image whose Rust runtime
still has threads in it. It is not the Shell's: the probe never unloads, which
is why nothing saw it until there was a Modules tab on this platform.

## The app, opened from the sidebar

A `ui_qml` member of the Bundled set is an APP, and the Shell opens one the way
it opens a desktop one: a tile in the sidebar, `launchUIModule`,
`IShellHost::loadUiModule`, and the widget handed back through
`onPluginWindowRequested` into the workspace. What is different is only what
happens inside `loadUiModule` — there is no `ui-host` subprocess to start, so
`BundledSetShellHost` brings the module up in this process (`ViewModuleRunner`,
shared with the smoke probe: dlopen the framework, instantiate the backend,
publish it on a socketpair node, acquire the typed replica, load the module's
QML out of the image's own qrc) and hands the Shell a `QQuickWidget`.

```
[shell] shell: the sidebar carries a tile for chat_ui
[shell] drive: press 'sidebar.app.chat_ui' at (44, 268) in 88x1326
[shell] view image opened in 41 ms
[shell] view model remoted: chat_ui/conversationModel (9 roles)
[shell] replica valid: chat_ui
[shell] QML loaded from the framework's resources in 96 ms
[shell] app chat_ui is mounted in the Shell
[shell] shell app: chat_ui's conversationList holds 1 row(s) after 50 ms
[shell] shell app: chat_ui rendered conversationList, newMenuButton in the Shell
[shell] SHELL SHOWS THE BUNDLED APP
[shell] APP SHOWN: chat_ui on screen 468 ms after the sidebar tile was pressed
```

That one row is the group the Shell's own chat bring-up created and exchanged
in a minute earlier, read back through the app's UI — which is the difference
between "the QML rendered" and "the QML is bound to the running core". A view
renders its empty state perfectly well with a null model, and the models reach
QML by a different route from the backend replica, so nothing else here would
catch one that never arrived.

`conversationList` and `newMenuButton` are **chat_ui's own** handles, out of
logos-chat-ui's `ConversationsPane.qml` — nothing this repo draws. That is what
makes the verdict about the app rather than about the host having created a
widget, and it is checked twice more: the widget has to be inside the Shell's
own tree (only `onPluginWindowRequested` could have put it there) and the
launcher row has to have moved to loaded.

One thing had to be added under the replica for a real app to work.
`enableRemoting()` on a `.rep` source carries the backend's properties, signals
and slots and does NOT carry a `QAbstractItemModel` one of those properties
points at — a model is its own child source, named `<module>/<property>` and
acquired with `acquireModel()`. `ui-host` does that scan on the desktop;
`ViewModuleRunner` does it here, and `logos.model(...)` on the bridge is the
other half. Without it chat_ui renders with three empty lists and nothing says
why. view_counter never needed it: it has no models.

## The Settings page at a phone's width

The Shell's Settings page used to be the desktop's at any size: a 200-px
section rail inside 40-px insets, beside a table whose columns want ~700 px
more. A Qt layout handed less than its minimum does not shrink, it overflows —
so on a phone, and on a 13-inch iPad in portrait, the pane ran off the right
edge and took each row's Load/Unload control with it. Qt delivers a press by
coordinate, so that control was not awkward, it was unreachable
(logos-workspace#84).

It adapts now, in two steps that are independent of each other:

| below | what changes | where |
|---|---|---|
| 900 px of page | the section rail becomes a scrolling strip above the pane, and the insets shrink to a phone's | `SettingsView.qml` |
| what the desktop columns ask for — the sum of their `preferredWidth`, 830 for modules and 880 for apps | the table keeps the module and its action; status folds into the module cell (with CPU and memory for a module, the version for an app), the description is dropped, and Interface moves to the row | `ModuleInspectorView.qml`, `AppsInspectorView.qml` |

The second threshold is the PREFERRED total and not the minimum one, which a
physical iPad Air (4th gen) is what settled: its pane is 700 px, the desktop
columns' minimum total to the pixel, and the row overflowed anyway — a RowLayout
squeezed between the two does not shrink every column proportionally. A test
adds the column set up and pins the number to it.

Wider than both, the desktop layout is untouched.

So `tap()` no longer works around an off-screen control — a control the
viewport does not contain is a layout regression and the run says so:

```
WRONG: 'moduleRow.loadToggle.bare_counter' is at (1000, 327), outside the
704x763 viewport -- no touch can reach it on this screen
```

The one thing it does scroll is the section strip, which is a horizontal
scroller by design and which a finger would swipe (`scrollIntoView`).

Two things about the press itself, both found by running this on the iPad:
`settledCentre` waits until the control has not moved for a quarter of a
second of wall clock before aiming — a panel's width lands over several polish
passes, and a press aimed at where the toggle was two passes ago hit the row's
left edge (x=190 instead of x=826) and did nothing. And the press and the
release are a real interval apart: the table rides in a Flickable, which holds
a press until the gesture declares itself and then replays the pair, and back
to back in one event-loop turn that replay was a coin flip. The run prints
where each press went, so a future miss is diagnosable from the log:

```
[shell] drive: press 'moduleRow.loadToggle.bare_counter' at (826, 272) in 928x1326
```

`tests/qml/tst_SettingsMobileLayout.qml` (in `nix build .#qml-tests`, seconds,
no Mac) is the same assertion without a device: it builds the real
`SettingsView` at 402×874, 928×1326 and 1440×900, checks each row's toggle is
inside the window, and then presses it — an off-screen control gets no click
and the signal spy stays at zero.

## Layout

```
src/ShellModulesBackend.*   the QML-facing `backend`
src/ShellStoreBackend.*     the App Manager's one seam, over the real modules
src/BundledSetShellHost.*   IShellHost over it
src/ShellSceneDriver.*      finding, settling and pressing in the Shell's
                            rendered scenes -- shared by the three drivers below
src/ShellModulesDriver.*    the acceptance pass: open the tab, check the rows,
                            their install type and their stats, press the
                            toggle twice
src/ShellAppDriver.*        the other half: press the sidebar tile and check
                            the app's OWN handles are on screen
src/ShellCatalogDriver.*    what the app does NOT ship: add a repository, read
                            the catalog, install a row and open it -- driven by
                            the command line, because the catalog a device is
                            pointed at is not a property of the build
src/main.cpp                core up, shell up, drive, shut down cleanly
stage/CMakeLists.txt        the pure half — one static archive, built by nix
app/CMakeLists.txt          the impure half — the Xcode link, embed and sign
```

## Installing from a catalog

The Shell takes three arguments, and it is the only way a phone can be pointed
at a catalog: a device has no config file a developer can edit (ADR 0003).

```
--repository <url>            a logos-repo.json. https, or http to LOOPBACK --
                              the same rule a catalog row's links go through
                              (appmanager/CatalogEntry.cpp), because this curl
                              carries no CA bundle and a self-signed local
                              server is unreachable however the URL is spelled
--trust-signer <name>=<did>   anchor a publisher in this device's keyring. The
                              Shell's policy is `require`, and a repository's
                              own `trustedSigners` anchors NOTHING -- a
                              downloaded claim is not a trust decision
--install <package>           press Install on that row, log the signer prompt,
                              approve it, and check the module reached the
                              workspace
```

A release to point it at comes out of the same `.lgx` files the Bundled set is
built from (`../../nix/local-catalog.nix`):

```bash
nix run .#serve-local-catalog            # 127.0.0.1:8099, prints the launch line
LOGOS_SHELL_WEB_MODULES=web_counter \
LOGOS_BUNDLE_APPS=capability_module,package_manager,package_downloader \
  nix run --impure .#run-basecamp-shell-ios-sim -- \
    --repository http://127.0.0.1:8099/logos-repo.json \
    --trust-signer logos-catalog-test=did:jwk:... \
    --install web_counter_b
```

`LOGOS_SHELL_WEB_MODULES` is what makes the run mean anything: the app ships
both `web` fixtures by default, and installing one the image already carries
would be indistinguishable from not installing at all. The default build is
unchanged -- `getEnv` is `""` in a pure evaluation, exactly as with
`LOGOS_BUNDLE_APPS`.

Both halves are wired up in [`../../nix/ios-apps.nix`](../../nix/ios-apps.nix),
which builds this app and the smoke probe from one description. The two
runners over it -- `run-basecamp-shell-ios-sim` and `-ios-device` -- are
[`../../nix/ios-runner.nix`](../../nix/ios-runner.nix), a function of plain
strings so that [`../../nix/ios-runner-lint.nix`](../../nix/ios-runner-lint.nix)
can render them over fixture Bundled sets of every size and shellcheck the
result. That check (`nix build .#ios-runner-lint`, seconds, no toolchain) is
what keeps `--bundle <a single app>` buildable: a one-module set used to make
the framework loop a single literal word, which is SC2043, and shellcheck
failed the runner.

`nix build .#checks.<darwin>.ios-shell-host` builds the static archive this
app links -- main_ui, the design system and the Native container. The Xcode
step above it needs a Mac and a device and stays a `ws run`.
