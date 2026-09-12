# Basecamp's Shell on a phone

The real UI shell — `src/`, `main_ui`, the same sources the desktop plugin is
built from — running on iOS over the app's **Bundled set**. Its Modules tab
lists the set, and its Load/Unload buttons go through the Native container.

```bash
ws run logos-basecamp --target ios-sim-arm64 --bundle view_counter --app shell

# ...and with chat in it, against a desktop peer (see the section below)
LOGOS_IOS_TEAM_ID=<team> LOGOS_IOS_DEVICE=<udid> \
  ws run logos-basecamp --target ios-arm64 --app shell \
  --bundle capability_module,chat_module,delivery_module,libp2p_module \
  -- --chat-peer <the desktop installation's get_address>
```

## What is and is not different from the desktop

Nothing above `IShellHost` is different. The Shell is handed one `IShellHost*`,
the ABI check still runs, and `main_ui` still links Qt and the design system and
nothing else. Three things below it are:

|  | desktop | here |
|---|---|---|
| how the Shell is loaded | `QPluginLoader` opens `main_ui.dylib` | linked in and reached through `QPluginLoader::staticInstances()` — Qt for iOS is static archives, there is no plugin to open (ADR 0001) |
| where the module set comes from | a modules **directory**, scanned | the Bundled-set **manifest** the build compiled in; a phone may not download native code (ADR 0003) |
| what answers the QML `backend` | `MainUIBackend` over three managers | `ShellModulesBackend`: the Modules tab is live, over the desktop app's own `ModuleInstanceModel` + `CoreModuleManager` + `ICoreRuntime`; apps, packages and repositories say they are not here yet |

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
[shell] bundled set:      bare_counter, capability_module, view_counter
[shell] SHELL MODULES TAB LISTS THE BUNDLED SET
[shell] all 3 rows are installType 'embedded' -- no Downloaded module
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
src/BundledSetShellHost.*   IShellHost over it
src/ShellModulesDriver.*    the acceptance pass: open the tab, check the rows,
                            their install type and their stats, press the
                            toggle twice
src/main.cpp                core up, shell up, drive, shut down cleanly
stage/CMakeLists.txt        the pure half — one static archive, built by nix
app/CMakeLists.txt          the impure half — the Xcode link, embed and sign
```

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
