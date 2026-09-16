# Basecamp's Shell on a phone

The real UI shell — `src/`, `main_ui`, the same sources the desktop plugin is
built from — running on **both phones** over the app's **Bundled set**. Its
Modules tab lists the set, and its Load/Unload buttons go through the Native
container.

```bash
# A PLAIN LAUNCH DRIVES NOTHING. The app comes up and waits for whoever is
# holding the phone -- which is what makes it worth testing by hand.
ws run logos-basecamp --target ios-sim-arm64 --bundle view_counter --app shell

# ...and a run that is proving something says so (see "Driving is a per-run
# choice" below).
ws run logos-basecamp --target ios-sim-arm64 --bundle view_counter --app shell \
  -- --drive apps,modules

# ...the milestone's own set: the real Chat app over the networking modules,
# against a desktop peer (see the section below)
LOGOS_IOS_TEAM_ID=<team> LOGOS_IOS_DEVICE=<udid> \
  ws run logos-basecamp --target ios-arm64 --app shell \
  --bundle capability_module,libp2p_module,chat_ui \
  -- --drive chat,apps --chat-peer <the desktop installation's get_address>

# ...and the same Shell on Android. No `chat_ui`: the mobile catalog
# publishes no ui_qml variant for this platform, so the set is the four core
# modules and the sidebar carries no app tile (see "One Shell, two phones").
ws run logos-basecamp --target android-arm64 --app shell \
  --bundle capability_module,libp2p_module,delivery_module,chat_module \
  -- --drive chat,modules \
     --peer <the desktop libp2p peer's multiaddr> \
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

## Driving is a per-run choice

The Shell knows how to drive itself through seven acceptance passes, and it runs
**none of them** unless the launch asks:

```
--drive chat        bring chat up over the set's networking modules and report
                    the two cold-start numbers
--drive apps        press the sidebar tile of a `ui_qml` member and check the
                    app's OWN handles are on screen
--drive packages    open the Package Manager section and report what the page
                    says (#145)
--drive popups      open a Popup, a modal Dialog and a Menu in EVERY scene the
                    Shell has on screen, and read the pixels: does the surface
                    draw them? (#187) Needs no module and no network
--drive keyboard    open the app's own new-conversation field and report whether
                    the on-screen keyboard reaches it (#152), and whether the
                    menu and the dialog on the way to it are drawn at all (#187)
--drive web-apps    open a `web` app, check its page is inset to the workspace,
                    and LEAVE it again (#110)
--drive web-input   open a `web` app and TYPE into its form, with real pointer
                    and key events at the page, then read the field back (#174)
--drive modules     the Modules tab: the rows, their install type, their stats,
                    and a Load/Unload round trip
--drive all         every pass, in the order above
```

Composable, on one flag or several: `--drive apps,modules` and `--drive apps
--drive modules` are the same run. An unknown name is refused on the console,
by name, and the passes beside it still run.

**Why it is not the default.** It used to be: every launch fired all of them
from one timer, gated only by whether the BUILD carried something drivable, so
a build carrying everything drove everything. That manufactured state before
anyone looked at the app -- a hand-driven build had an app loaded and a web app
opened *and closed* before the tester reached it, and several defects reported
from manual testing turned out to be driver aftermath rather than the app's
resting state. It also made every device run pay for every pass, including an
agent that wanted one measurement, and it created failures of its own timing.
`src/DriveScript.h` carries the whole account (logos-workspace#155).

The passes themselves are unchanged and print exactly what they printed before,
because roughly ten issues quote their assertions as on-device evidence. What
an acceptance run names on `--drive` is now also what the command says it is
proving.

`--repository` / `--trust-signer` / `--install`, `--call` and `--consent` are
**not** passes and are not listed above: each is its own script with work to do
only when a flag asked for some, which is the shape this generalises.

## What the run prints

With `--drive modules` the host drives the Modules tab once, after the first
frame, and an automated run reads the verdicts off the console:

```
[shell] shell: IShellHost ABI 3 (host 3)
[shell] COLD START: Shell shown at 812 ms
[shell] drive: modules
[shell] shell: Settings -> Apps Inspector is on screen
[shell] apps tab rows:   view_counter
[shell] sidebar tiles:   view_counter
[shell] SHELL APPS TAB LISTS THE APPS (view_counter), EACH ALSO KNOWN TO THE RUNTIME
[shell] shell: Settings -> Module Inspector is on screen
[shell] modules tab rows: bare_counter, capability_module, view_counter
[shell] app ships:        bare_counter, capability_module, view_counter
[shell] SHELL MODULES TAB LISTS WHAT THE APP HAS
[shell] all 3 shipped row(s) are installType 'embedded'; 0 downloaded
[shell] modules tab stats: bare_counter 0.0%/1.4 MB, capability_module 0.0%/0.9 MB, ...
[shell] SHELL MODULES TAB SHOWS THE SET'S STATS
[shell] load bare_counter
[shell]   bare_counter loaded in 6 ms
[shell] unload bare_counter
[shell] drive modules: bare_counter not loaded -> loaded -> not loaded
[shell] SHELL MODULES TAB ROUND TRIP OK (bare_counter)
```

Settings draws **two** inspectors and the pass reads both (#146). Apps Inspector
is "UI plugins available in this installation" and had nothing behind it on a
phone -- a Store shell has no UI-plugin directory to scan (ADR 0003) -- so a
`web` app was listed under Modules and nowhere else while the same Shell drew it
a sidebar tile and mounted it in the dock. The apps pane now lists exactly the
apps the sidebar carries tiles for, and the Modules pane still lists everything
the core knows, apps included: it is titled *Core modules known to the runtime*
and that is the set it is for.

Every row the CORE is in charge of is driven, not just the first — a row the
HOST mounts is instantiated in this process rather than run by the core (ADR
0006), so it is skipped. The names are printed rather than a count because which
modules were actually unloaded is the whole of what #96 was about.

**Which row is whose** is the row's own `hostLoaded`, not its type, and the two
are different questions (logos-workspace#149). A `web` app's row is a `ui_qml`
row — it has a user interface and the sidebar carries a tile for it — and its
MODULE is the core's, because its page lives in the Web container. Asking for
the type here skipped every `web` app, so the one Unload a user complained
about was also the one nothing had ever pressed. A `web` row's unload is
checked further than a Bare one's: the container must have let its page go, the
Shell must not still hold a tab onto it, and its tile must stay on the sidebar
— the app is installed either way, and pressing the tile is what brings it back
(#123).

One thing this deliberately does NOT drive: unloading a `web` app while the
Shell still holds its tab. Not because the case does not matter — it is the
state logos-workspace#151 was reported in — but because this is a loop over
every row, and an app docked in the middle of it belongs to one of them. The
web-apps pass drives it instead, on the app it already has open, and checks the
container's BOOKS as well as its pages:

```
[shell] web app: web_counter_b is open again; 1 live runtime(s), and now it is unloaded from under the user
[shell] UNLOADING AN OPEN WEB APP TAKES ITS PAGE, ITS TAB AND ITS RUNTIME WITH IT (web_counter_b: 0 live runtime(s) held, nothing visible)
```

The runtime half is what #151 is left as. A module with no page still in the
live set spends the phone's single 290 MB slot on nothing, so the next app the
user opens has to evict a dead one — and the console said both things at once,
`web_counter is visible; 1 live runtime(s)` from the container beside `app
web_counter is not mounted` from the Shell.

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
[shell] app manager: catalog not in this build, capability authority up, consent armed
```

Three independent halves, and all three are normal answers rather than errors. A
Store shell's Bundled set is data (ADR 0007), so a build that carries neither
package module cannot browse a catalog and says so — an empty App Manager would
read as "the catalog has nothing in it", which is a different and much more
alarming claim. Consent is separate because `capability_module` is in every set
that calls anything, so a shell with no catalog still prompts for a Downloaded
module an earlier launch installed.

Three things the device settled, all about LAZY LOADING (SM-G990B and an iPad Air
13-inch simulator, 2026-09-13):

| | |
|---|---|
| **`loadedModules()` is not "does this build have it".** The Native container loads a Bundled member lazily, so gating on it reported `consent unavailable` in a build that carries capability_module, because nothing had called it yet. Presence is `knownModules()`; `onEventWhenAvailable` is built to arm against a module still coming up. | `ShellStoreBackend::isPresent` |
| **Nothing else calls the package modules.** If the App Manager does not load them, nobody does — so `configure()` loads both before answering `hasCatalog()`, rather than waiting for a first call that would fail. | `ShellStoreBackend::ensureLoaded` |
| **capability_module must not be lazy.** It is the authority every cross-module call is authorised through, so "nobody has asked for it yet" and "no call can be authorised" are the same state — and the Shell itself never calls it, so nothing ever would. A `web` module asking keystore_module for accounts had `requestModule` fail to acquire capability_module after 0 ms of its 20 s budget and the target refuse with `auth token not recognized`; nothing in that chain names the module that is missing. | `ShellStoreBackend::ensureCapabilityAuthority` |

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

### Unloading a threaded Bare module used to kill the Android process

Driving the Modules tab's Load/Unload on `chat_module` took the app down with
no tombstone and no `am_kill`, right after `In-process module stopped`. The
container did the careful thing (`InProcContainer::terminate` asks the module
to unload, stops the dispatch thread, then closed the image), and on iOS
`dlclose` of a framework does not actually unmap — so the same sequence was
survivable there and fatal here, where bionic really unmaps an image whose Rust
runtime still has threads in it. It was never the Shell's: the probe never
unloads, which is why nothing saw it until there was a Modules tab on this
platform.

Fixed in logos-liblogos (#96): an image a module has RUN in is never unmapped,
on any platform — see `closeBareModule` for why the host cannot know whether a
module's language core still has threads up. The Modules driver here rounds
every core row's toggle now, not just the first one, because the first usable
toggle in this set is never one of the two modules that own threads.

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

## Typing into the app

An app's fields have to raise a keyboard, and on an iPad none of chat_ui's did
(logos-workspace#152): the field took the caret and nothing came up. The two
focus notions an embedded QML scene has are why.

A `QQuickWidget` renders into an **offscreen** `QQuickWindow`. That scene has
its own focus object — the `TextArea` with `activeFocus` and a blinking cursor
— while the platform input context is only ever told about
`QGuiApplication::focusObject()`, which for a widget window is its focus
**widget**. `QQuickWidget` bridges the two, answering `Qt::ImEnabled` and every
other input-method query out of its scene, so the chain works as soon as the
widget holds the window's focus. Nothing on iOS ever gives it that from a
touch:

* `QIOSIntegration` reports `SetFocusOnTouchRelease = true`, so
  `giveFocusAccordingToFocusPolicy` defers on `TouchBegin` and waits for the
  release;
* `QApplication::notify` only applies that policy on touch **begin** — there is
  no `TouchEnd` case — so the release never comes;
* and the synthesised mouse press that would otherwise have carried the click
  focus never happens, because `QQuickWidget` sets `WA_AcceptTouchEvents` and
  accepts the touch itself.

`QInputMethod::show()`, which `QQuickTextInput` calls on focus-in, is documented
as a no-op on iOS ("keyboard controlled fully by platform based on focus"), so
there is no second route. `QIOSInputContext::update()` reads
`inputMethodAccepted()` off the focus object, finds a widget that wants no
keyboard, and resigns its text responder.

`QuickWidgetKeyboardFocus` restores the missing half. It is installed on the
**application** in `main.cpp`, before any scene exists, and watches every
`QQuickWidget` in the process — including the one an app is mounted into long
after startup, which is why it is not handed a list of surfaces. Whenever a
scene focuses something that answers `Qt::ImEnabled`, its widget takes the
window's focus. It follows the SCENE's focus change rather than the tap on
purpose: chat_ui's New DM dialog focuses its address field from `onOpened`,
with no tap on the field at all.

### A driver only ever presses a scene that is on screen (#187)

The Shell can hold **two copies of one app's scene**. `--drive apps` closes a
Bundled app and opens it again to prove the second mount is live; closing takes
the widget out of its dock and `deleteLater()`s it, and a deferred delete is
never delivered while the drivers run -- they work off `processEvents()` inside
a `QTimer::singleShot` and never return to the event loop that posted it. So
the closed app's `QQuickWidget` is still a child of the Shell, off screen, with
its whole QML scene alive in it.

On the venue's physical iPad the keyboard pass found the dead copy's
`newMenuButton` -- `findChildren` answers in construction order -- and every
step after it succeeded on a scene nobody could see: the menu opened, the dialog
opened, the address field took `activeFocus` and iOS drew a full software
keyboard over a conversations pane that never changed.

`basecamp::shell::sceneIsOnScreen()` is the rule that came out of it, and every
lookup in `ShellSceneDriver` goes through it. An off-screen scene is not a
weaker kind of present, it is absent: a control whose only copy is in one is not
found at all. `dumpNames()` still lists those scenes, marked `NOT ON SCREEN`, so
a lookup that fails for this reason says so in the same breath. Covered by
`tests/shell_scene_scope_test.cpp`.

`ShellKeyboardDriver` is the `--drive keyboard` pass. It asks the question from
inside the app, because there is no way to ask it from outside — Xcode 27 removed `SimulatorKit`, so `idb ui tap`
refuses HID on a modern simulator and `simctl` has no input verb. It walks the
operator's path (the app's "+", New DM, the address field) and prints the three
facts the platform reads, in order:

```
[shell] keyboard: 'newDmMenuItem' opened the dialog holding 'convAddressField'
[shell] drive: press 'convAddressField' at (362, 560) in 723x1116
[shell] keyboard: 'convAddressField' activeFocus=true
[shell] keyboard: app focus object QQuickWidget(-) over scene ChatView.qml, focusing LogosTextArea(convAddressField), accepts input method: yes
[shell] keyboard: QInputMethod isVisible=true, panel 820x69 of a 820x1180 screen after 799 ms
[shell] keyboard: that is a shortcut bar, not a keyboard -- this device has a
        hardware keyboard connected, so iOS draws no panel. The input method is
        still ON the field
[shell] KEYBOARD REACHES THE FIELD
```

Those three also settle what #152 asks to rule out first, and the run above is
the answer for a simulator: 69 points of panel on a 1180-point screen is the
**shortcut bar** iOS draws instead of a keyboard while a hardware keyboard is
connected, which the Simulator does by default. That is a setting, not a
defect — and reaching it at all is the fix, because the same iPad printed this
before:

```
[shell] keyboard: 'convAddressField' activeFocus=true
[shell] keyboard: app focus object QQuickWidget(-), accepts input method: no
[shell] keyboard: QInputMethod isVisible=false, panel 0x0 after 3001 ms
[shell] WRONG: the caret is in 'convAddressField' and the object the input
        context is handed is QQuickWidget(-), which wants no keyboard --
        nothing the user does can raise one
```

The menu entries and the dialog's field are inside `Popup`s, which QtQuick
parents to the scene's `Overlay` beside the view's root object rather than under
it. `ShellSceneDriver`'s lookups therefore take a `Scope`: every existing driver
keeps the narrow walk, and only one that has to reach into a menu asks for the
wider one.

The rule is stated without a phone in
[`tests/quick_widget_keyboard_focus_test.cpp`](../../tests/quick_widget_keyboard_focus_test.cpp),
which sets up the state an iOS tap leaves behind — an item with `activeFocus`
inside a surface that is not the window's focus widget — and asserts the focus
widget and that the object handed to the input context answers `Qt::ImEnabled`.

### And the other picture, from a device (#170)

Every simulator in the fleet connects a hardware keyboard, and turning that off
is a Simulator UI control with no `simctl` verb — so the panel above is all a
simulator can ever show. The venue's **physical iPad Air (4th generation)**
(iOS 26.5.2) has no keyboard attached, and prints the other half of the answer
on the same build:

```
[shell] keyboard: 'convAddressField' activeFocus=true
[shell] keyboard: app focus object QQuickWidget(-) over scene ChatView.qml, focusing LogosTextArea(convAddressField), accepts input method: yes
[shell] keyboard: QInputMethod isVisible=true, panel 820x337 of a 820x1180 screen after 0 ms
[shell] keyboard: that is a full software keyboard -- 337 of 1180 points, 29% of the screen
[shell] KEYBOARD REACHES THE FIELD
[shell] keyboard: the panel is on screen; holding it for 4000 ms so a screenshot can see it
```

Same field, same build, 337 points instead of 69: a keyboard rather than a bar.
The rule that tells those two apart is
[`KeyboardPanel.h`](src/KeyboardPanel.h) — a panel shorter than a sixth of the
screen has no keys on it — and both measured shapes are pinned against it in
[`tests/keyboard_panel_test.cpp`](../../tests/keyboard_panel_test.cpp), because
they came from two device runs that no check can repeat. A panel the platform
reports with no geometry is neither: it is `Unmeasured`, and saying "shortcut
bar" for it would put a connected hardware keyboard in the log of a run that
measured nothing.

The last line is the capture handle. Taking the picture is a command on the
HOST, so the pass leaves the panel standing for four seconds after it has
measured it rather than dismissing the dialog straight away:

```bash
# in one shell: build, install, and drive the device
LOGOS_IOS_DEVICE=<udid> LOGOS_IOS_TEAM_ID=<team> \
  ws run logos-basecamp --target ios-arm64 --app shell \
  --bundle capability_module,libp2p_module,chat_ui -- --drive chat,apps,keyboard

# in another: shoot as soon as the panel is up (Xcode 27; ~0.7 s a frame)
xcrun devicectl device capture screenshot --device <udid> --destination shot.png
# a simulator answers the same question with
xcrun simctl io <udid> screenshot shot.png
```

`--drive chat` is not optional on that line: chat_ui's "+" is
`enabled: root.online`, so a run that did not bring the delivery node up presses
a disabled button and the pass reports a menu with no entries.

What the picture does NOT show is the dialog. On the device the keyboard is up
and the field behind it holds the caret, and neither the "+" menu nor the New DM
dialog is painted — every `Popup` in the mounted app's scene is missing from the
screen while its items exist, have geometry and take focus. That is a separate
defect from the one this pass measures, and the pass is unaffected by it:
logos-workspace#187.

## Typing into a `web` app's page

`--drive web-input`. The pass above is about the Shell's own QML; this one is
about a form drawn **inside a page**, which is a different problem with a
different answer.

A `web` variant's UI is pixels in a canvas. There is no widget to press, no
text node to read and no `QQuickItem` a driver can find by `objectName` —
`ShellSceneDriver`, which is how every other pass presses something, sees
nothing in there. And as of this venue's Xcode 27 nothing OUTSIDE the app can
put a key in one either: `idb ui text` and `idb ui tap` are accepted and
silently dropped (measured against Safari's own address bar and `ui button
HOME`, with the companion logging `hid succeeded` every time), `simctl` has
never had an input verb, and `--call` answers `(no value)` for a `ui_qml`
module's `.rep` SLOTs. So a wallet flow whose input is typed — the seed-phrase
import, a Send's recipient and amount, the Advanced tab's custom RPC — could
not be driven on a device at all (logos-workspace#174).

**A control is found by whichever handle it has.** A field by its
`objectName`, asked of the bundled QML runtime (`logosViewItem`, which is
`LogosWebRuntime::describeItem`): it answers the rect in the window's
coordinates and **the text the item now holds**, which is the module's own
state and the only honest readback there is. A button by its accessible name,
off Qt for WebAssembly's own DOM mirror of the scene inside
`.qt-window-a11y-container` — which Qt builds lazily, so the tree is EMPTY
until something clicks the hidden button it leaves there for a screen-reader
user.

The runtime is asked first, and that is the shape of the whole thing: Qt's wasm
bridge publishes a text editor as an `<input aria-hidden="true">` with no name
at all, so the one control a typed flow is about is exactly the one the
accessibility tree cannot be asked for. A Logos button, conversely, carries no
objectName and its text is its name.

The events themselves are real, and they are the same calibrated pointer and
key events the browser end-to-end drives — a Qt wasm window reads a local point
off `offsetX`, which cannot be set and is derived differently by each engine, so
the script measures the relation with two probe moves rather than believing
either. Only the LOCATING is done through the accessibility tree.

```
[shell] web input: pressed wallet_ui's 'Advanced'
[shell] web input: typed 12 character(s) into wallet_ui's 'Account label'
[shell] TYPED TEXT REACHES A WEB APP'S PAGE: wallet_ui's 'Account label' holds
        'issue174', put there by real key events at the page
```

So the wallet's flow names `Advanced` and `Import` (their text) and
`advSeedField`, `advAcctLabelField` and `advAcctPwField` (their objectNames,
which wallet_ui already carries for the desktop inspector). An accessible name
is matched from the front, so `Refresh` finds `Refresh balances`.

The script and the console vocabulary its answers come back in are
[`mobile/webview/WebPageInput.h`](../webview/WebPageInput.h); the walk is
[`src/ShellWebInputDriver.h`](src/ShellWebInputDriver.h), and the reading of
those answers is stated without a webview in
[`tests/web_page_input_test.cpp`](../../tests/web_page_input_test.cpp).

This pass does not scroll: a field below the fold is reported as "on the page
and not reachable" rather than worked around, because a form whose fields are
off the page is a finding.

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

So `tap()` no longer works around an off-screen control — a control a finger
could not reach is a layout regression and the run says so:

```
WRONG: 'moduleRow.loadToggle.bare_counter' is at (630, 354) of a 704x763 view,
at (714, 445) on a 402x874 screen -- no touch can reach it on this screen
```

Two rectangles, and the second is not implied by the first (`pressIsReachable`).
The pane containing the control proves nothing about the phone containing the
pane: a Qt layout handed less room than its minimum does not shrink, it
overflows. The iPhone 16 Pro is where that mattered — `MainContainer` asked for
a desktop's 800×600 floor, so the Shell laid itself out 784 pt wide on a 402-pt
screen, the Settings pane inside it was 704 pt wide, and the toggle sat
comfortably inside its own pane and ~300 pt past the edge of the display. The
driver pressed it by coordinate (the one thing a person cannot do) and reported
the tab green. `shellWindowFloor()` caps that floor at the screen now, and the
second rectangle is what keeps the verdict honest if anything else ever
overflows (logos-workspace#87).

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
`SettingsView` at 306×834, 724×1140, 928×1326 and 1440×900, checks each row's
toggle is inside the window, and then presses it — an off-screen control gets
no click and the signal spy stays at zero. Those are PANE sizes, not screen
sizes: `MainContainer` spends 96 px of the width on the sidebar and its insets
and about 40 of the height on the tab bar before a Settings view sees any of
it, so an iPhone 16 Pro's 402×874 arrives there as 306×834 and an iPad Air 13's
1024×1366 as 928×1326.

## Layout

```
src/ShellModulesBackend.*   the QML-facing `backend`
src/ShellModuleRows.*       what the Modules tab and the sidebar show, as a
                            pure function of the facts that decide it
src/InstalledPackages.*     what the packages ON THIS DEVICE declare, read off
                            their manifests with nothing loaded -- which is how
                            an installed app has a tile before it runs (#123)
src/ShellStoreBackend.*     the App Manager's one seam, over the real modules
src/BundledSetShellHost.*   IShellHost over it
src/WebAppSurface.*         the placeholder a `web` app is DOCKED as, so the
                            Shell's chrome ends up beside its page rather than
                            under it (#110)
src/DriveScript.*           which acceptance passes THIS RUN asked for, off
                            the command line. Nothing, unless it said so (#155)
src/ShellSceneDriver.*      finding, settling and pressing in the Shell's
                            rendered scenes -- shared by the drivers below
src/ShellModulesDriver.*    the acceptance pass: open the tab, check the rows,
                            their install type and their stats, press the
                            toggle twice
src/ShellAppDriver.*        the other half: press the sidebar tile and check
                            the app's OWN handles are on screen
src/QuickWidgetKeyboardFocus.*
                            the fix: a scene that focuses a text item gives its
                            QQuickWidget the window's focus, which is the only
                            focus the platform input context is told about (#152)
src/ShellKeyboardDriver.*   and the pass that asks it on the device: the app's
                            "+", New DM, the field -- then the three facts the
                            platform reads, in order
src/ShellWebAppDriver.*     the same for a `web` app, plus the step that check
                            could never make: LEAVE it again. The page is inset
                            to the workspace, navigating away puts the Shell
                            back, and closing the app leaves the module running
src/ShellCatalogDriver.*    what the app does NOT ship: add a repository, read
                            the catalog, install a row and open it -- driven by
                            the command line, because the catalog a device is
                            pointed at is not a property of the build
src/ShellConsentDriver.*    the 4.7.3 prompt, answered: deny, watch the
                            Downloaded module's next call still fail, grant,
                            watch it succeed -- and on a SECOND launch, that the
                            grant was already there
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

IT DOES NOT GET TO MAKE THE BUNDLED SET INCOHERENT (#183). A Bundled member may
depend on a `web` module this image carries -- `railgun_module` names
`keystore_module`, which reaches a phone only as a `web` variant -- so the
Bundled closure is resolved against the `web` modules this build can ship, and
the ones it leaned on are added to the assets whether or not this variable names
them. `bundled-set.json` records them under `webSatisfied`. The variable widens
what a developer asks for; the closure decides what the image must carry.

A `core` module's `web` variant installs the same way and ends differently, and
that difference is a criterion rather than a gap: it has no view, so it gets no
sidebar tile, and the run says so --

```
[shell] web container: keystore_module runs in a page and declares no UI -- headless, and no app tile
[shell] CATALOG INSTALL OK: keystore_module came from the catalog and is running headless in the Web container -- no UI, so no tile
```

Every `web` variant gets a PAGE, because a wasm image needs a document to live
in; only a `ui_qml` one's page is a user interface. The package's declared
`type` is what tells them apart (`MobileWebModuleView::servesUi`), and reading
a page as evidence of a UI gave the first headless module a tile onto a blank
document.

## An app you installed, on the NEXT launch

A Downloaded module is loaded exactly once: by the install that brought it
(`ShellModulesBackend::onModuleInstalled`). On every later launch the core
discovers the same package in the same scanned directory and waits to be asked,
and a Store shell's cold start deliberately does not ask -- one `web` page is
290 MB of QML runtime and seconds of it, and the container's budget is ONE live
runtime, so loading every installed app at startup would evict the one the user
actually wanted before they could reach it.

So the app is brought up when its TILE IS PRESSED, and the tile is there before
it runs. What makes a tile is no longer the Web container having opened a page
-- evidence that only exists while the module is up -- but the package's own
`type`, read off the manifest.json beside it (`src/InstalledPackages.h`). That
is the same field the container reads to decide whether a page serves a UI
(`MobileWebModuleView::servesUi`), so a tile and a page cannot disagree about
what a module is, and a `core` module's `web` variant still gets no tile.

Relaunching the app from the run above, with no `--install` and no arguments at
all:

```
[shell] app manager: 2 package(s) on this device declare a UI: web_counter, web_counter_b
[shell] web app: web_counter_b has a tile and is NOT running -- the press is what has to load it
[shell] bringing up web_counter_b, which this device has and nothing has asked for yet
[shell]   web_counter_b loaded in 953 ms
[shell] web app web_counter_b is on screen
[shell] web app: web_counter_b's page is at 88,33 927x1302 inside a 1024x1366 window
```

The Modules tab agrees: a Downloaded row reads as a view (`ui_qml`) from the
same fact, rather than as a `core` module until something opens its page.

A page can also go away while the Shell still holds the app's tab -- the
live-runtime budget unloads a module that ships no headless document, or the
Modules tab unloads one. The tab goes with it (`BundledSetShellHost::
dropWebSurface`), because a dock the Shell would raise onto no page is a tab
that shows nothing; the TILE stays, and pressing it loads the module again.

## Answering the consent prompt

```
--consent <caller>.<target>=<step>[,<step>...]     repeatable
```

capability_module refuses a call between a Downloaded module and anything else
until the user has decided (App Store guideline 4.7.3), announces
`consentRequired`, and lets the caller's NEXT attempt carry the answer -- it
cannot hold a dispatch thread open for as long as a person takes to read a
dialog. The steps are the answers this launch gives:

```
deny             wait for the prompt, answer NO, and assert the caller's next
                 attempt is refused with capability_module's own reason
grant            record a grant and assert the next attempt succeeds. No prompt
                 is waited for: a pair with a decision is never re-announced, so
                 changing a denial into a grant is a decision, not a dialog
dismiss          "not now" -- drop the prompt, record nothing, and assert the
                 pair is still undecided
expect-granted   assert the pair is ALREADY granted at startup, that no prompt
                 appears, and that the call goes through
```

TWO LAUNCHES, because the last criterion is a store on disk and only a second
launch of the app reads it:

```bash
# 1. install it, deny, then grant
nix run --impure .#run-basecamp-shell-ios-sim -- \
  --repository http://127.0.0.1:8099/logos-repo.json \
  --trust-signer logos-catalog-test=did:jwk:... \
  --install web_counter_b \
  --consent web_counter_b.package_manager=deny,grant

# 2. relaunch -- nothing installs, nothing is answered, and the call just works
xcrun simctl launch --console-pty "$UDID" co.logos.basecamp.shell \
  --consent web_counter_b.package_manager=expect-granted
```

`web_counter_b`'s QML calls `package_manager.getInstalledPackages` and RETRIES
while it is being refused (logos-module-builder's `web-view-counter` fixture),
which is what makes a decision observable at all: the refusal, the denial and
the grant are three different answers to the same call.

**The origins are the Shell's to declare.** capability_module's gate is a
function of where each module came from, it does not persist that (an app image
can change between launches), and a module cannot be asked -- so
`ShellModulesBackend::declareModuleOrigins` tells it, at startup and again
before each newly installed module is loaded. Undeclared, every module is
`bundled`, every call is allowed, and no prompt ever appears.

## Calling a module from the command line

```
--call <module>.<method>(<arg>,...)     repeatable, run IN ORDER
```

The on-device `logoscore call`, and the only way to reach a `core` module here:
it has no UI by definition, and a phone has no second process to call it from.
The driver loads each module it names, waits for it to become reachable -- a
`web` module is published by its PAGE, a beat after the core reports it loaded
-- and prints one line per answer.

```bash
xcrun simctl launch --console-pty "$UDID" co.logos.basecamp.shell \
  --call 'keystore_module.list_accounts'
# [shell] CALL OK keystore_module.list_accounts -> {"accounts":["0x5a3a5A89…"],"ok":true,…}
```

**Two things it cannot reach, and both are easier to hit than to diagnose.**

A `--call` arrives at its target as the HOST ANCHOR -- one undifferentiated
credential covering the shells, `core_service` and every relayed CLI token -- and
`keystore_module`'s gate admits it at no tier. Reading is ungated on purpose, so
`list_accounts` answers; every mutation is Tier D and belongs to the configured
custodian, so `create_unrelated_account` answers `not authorized` however it is
spelled.

Driving the module that HOLDS that role is not a way round it. A `ui_qml`
module's `.rep` SLOTs are its VIEW's contract, published to the page's QML rather
than as a LogosAPI module surface, so the call is accepted and answers nothing.
Measured on an iPad Air 13-inch simulator, with the page up and its contract
already answered (`contract query answered: 30 method(s)`):

```
[shell] CALL OK wallet_ui.createAccount(hunter2,main) -> (no value)
```

No refusal, no error, and not one line on the page's console. `(no value)` is
also what a `void` SLOT answers, which is why this is written down here rather
than left to be re-measured.

**Arguments are strings unless they say otherwise** -- `int:42`, `bool:true`,
`json:{"chainId":1}`, and `str:` to be explicit. That is the opposite of what
`logoscore call` does and is deliberate: a type inferred from the spelling makes
a well-formed hex address a NUMBER, and every address-taking method on a
keystore then answers `null` with status ok
([#106](https://github.com/logos-fleet/logos-workspace/issues/106)).

Run it TWICE across a restart and it is a persistence test, which is the only
honest shape for one: a `web` module's store lives in its page and the page dies
with the process, so nothing inside a single run can tell a durable write from
one that merely has not been lost yet.

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
