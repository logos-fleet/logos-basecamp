# liblogos smoke host

liblogos_core running on a phone, with nothing loaded. It starts the core
against an **empty** modules directory and a persistence path inside the app
sandbox, prints the modules listing and the protocol version, and stays alive
until it is asked to quit.

This is a bring-up probe, not a product: no module is installed, no
capability_module is loaded, and `logos_core_get_known_modules()` returning
nothing is the expected result. What it proves is that the core, and the eight
repos it links, run on iOS and Android at all.

## Run it

```bash
# iPhone simulator (boots one if none is booted)
nix run .#run-liblogos-smoke-ios-sim
LOGOS_IOS_SIM=<udid> nix run .#run-liblogos-smoke-ios-sim

# A signed iPhone/iPad. Both variables are required.
LOGOS_IOS_TEAM_ID=<team> LOGOS_IOS_DEVICE=<udid> \
  nix run .#run-liblogos-smoke-ios-device      # xcrun devicectl list devices

# Android (first `adb devices` entry unless ANDROID_SERIAL is set)
nix run .#run-liblogos-smoke-android
```

Each runner builds, installs, launches and then attaches to the console, so
the log below appears in the terminal as well as on the screen.

```
[smoke] base dir: .../Library/Application Support/Logos/LiblogosSmoke/logos
[smoke] protocol: 0.10.0 (abi major 0)
[smoke] logos_core_start: 1 ms
[smoke] modules_info: []
[smoke] known modules: 0 (an empty modules dir is the point)
[smoke] core up, 3 ms since main()
```

## Layout

| | |
|---|---|
| `src/` | `SmokeRunner` (the core bring-up) and `main.cpp` (the window and the shutdown paths) |
| `stage/` | iOS **pure** half: everything but `main.cpp`, built by nix as one static archive with the whole Logos closure attached (`nix/liblogos-smoke-ios.nix`) |
| `app/` | iOS **impure** half: the Xcode-generator link, run outside the nix sandbox because that is where an `.app` is signed |
| `android/` | the whole Android app -- androiddeployqt and gradle run inside the sandbox, so there is no split (`nix/liblogos-smoke-android.nix`) |

The libraries come from `logos-liblogos`'s mobile chain
(`logos-liblogos.lib.mkMobileChains`), which cross-builds liblogos_core and
the eight repos it links: static archives for iOS, shared objects for Android.

## Quitting

The Quit button and `SIGTERM`/`SIGINT` take the same path:
`logos_core_cleanup()`, then exit. The signal path exists so an automated run
can ask for a shutdown and watch it happen.

On iOS the process calls `_Exit(0)` once the core is down instead of returning
through `main()`. Measured on an iPad Air: `app.exec()` returns 0 and the
process then dies with SIGSEGV in the teardown of a fully static iOS image
(global destructors, and Qt's iOS run-loop integration unwinding the separate
stack it runs `main()` on) -- after the core is already down. Android returns
through `main()` normally.
