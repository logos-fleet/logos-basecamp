# Basecamp's Shell on a phone

The real UI shell — `src/`, `main_ui`, the same sources the desktop plugin is
built from — running on iOS over the app's **Bundled set**. Its Modules tab
lists the set, and its Load/Unload buttons go through the Native container.

```bash
ws run logos-basecamp --target ios-sim-arm64 --bundle view_counter --app shell
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
[shell] shell: Settings -> Module Inspector is on screen
[shell] modules tab rows: bare_counter, capability_module, view_counter
[shell] bundled set:      bare_counter, capability_module, view_counter
[shell] SHELL MODULES TAB LISTS THE BUNDLED SET
[shell] load bare_counter
[shell]   bare_counter loaded in 6 ms (Native container)
[shell] unload bare_counter
[shell] drive modules: bare_counter not loaded -> loaded -> not loaded
[shell] SHELL MODULES TAB ROUND TRIP OK
```

The rows are counted off the **scene**, not off the model: each row's status
badge carries its module's name, so a filter proxy that dropped a row or a
table showing a module the manifest does not account for fails there.

## Known: the Shell's tables are desktop-width

The Module Inspector's table is about a thousand logical pixels of columns and
the Load/Unload column is the last of them, so on a handset it is off the right
edge. The driver measures this and says so:

```
drive: 'moduleRow.loadToggle.bare_counter' is at (1000, 327), outside the
704x763 viewport -- the Shell's desktop table is wider than this screen;
activating the control instead of pressing it
```

When that happens it activates the row's own control instead of pressing it —
still the Shell's signal chain, with only UIKit's delivery of the touch to an
off-screen pixel skipped. Making the Shell's layouts narrow enough for a
handset is a slice of its own.

## Layout

```
src/ShellModulesBackend.*   the QML-facing `backend`
src/BundledSetShellHost.*   IShellHost over it
src/ShellModulesDriver.*    the acceptance pass: open the tab, check the rows,
                            press the toggle twice
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
