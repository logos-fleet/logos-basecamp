# liblogos smoke host

liblogos_core running on a phone with **one Bundled module** in it. It starts
the core against an empty modules directory and a persistence path inside the
app sandbox, then brings up the Bare counter that ships inside the app and
calls `add(1, 2)`.

This is a bring-up probe, not a product: nothing is *installed*, no
capability_module is loaded, and `logos_core_get_known_modules()` returning
nothing before the bundled module is registered is the expected result. What it
proves is that the core and the eight repos it links run on iOS and Android,
and that a protocol-free module image loads in the Native container there.

## The Bundled module

`mobile/bare-counter` is a leaf `universal` module built by
logos-module-builder; the app carries its `bare` output.

| | |
|---|---|
| iOS | `<App>.app/Frameworks/bare_counter_bare.framework`, embedded by Xcode with **Code Sign On Copy** so it carries the app's own signature. dyld will not load an unsigned image on a device, which is why it cannot be staged into the sandbox at runtime. |
| Android | `lib/arm64-v8a/libbare_counter_bare.so`, packaged by gradle but deliberately **absent from `libs.xml`** — everything Qt lists there is `System.load()`ed before any Logos code runs, and a Bare module cannot be opened that early. Since API 29 the app's native library directory is also the only place Android will `dlopen` from. |

Neither directory has room for a manifest beside the image, so the manifest
(`modules/bare_counter/manifest.json`) is compiled into the host and handed to
`logos_core_add_bare_module()` together with the path.

**How the module reaches the host's `lp_*`.** The iOS framework is linked
`-undefined dynamic_lookup` and the app force-loads and exports the symbols it
needs (three, computed from the module's own `nm -u` — see
`nix/liblogos-smoke-ios.nix`). On Android the artifact records
`NEEDED liblogos_protocol.so`, which is the only mechanism bionic has: it
resolves a dlopen'd library against its own DT_NEEDED closure and the linker
namespace's global group, and an app's libraries are never in the latter.

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
[smoke] bundled image: .../LiblogosSmoke.app/Frameworks/bare_counter_bare.framework/bare_counter_bare
[smoke] registered: bare_counter
[smoke] loaded bare_counter in 12 ms (Native container)
[smoke] add(1, 2) = 3
[smoke] BUNDLED BARE MODULE OK
```

The per-image `dlopen` time is reported by the core itself
(`Bare module image <name> opened in N ms`). On Android that line reaches
logcat only because the host hangs a logcat sink on liblogos's spdlog channels
before loading — liblogos logs to stderr, which the platform discards.
Measured: 4.7 ms on the iPhone 16 Pro simulator, 20.5 ms on an iPad Air,
1.7 ms on an SM-G990B.

## Layout

| | |
|---|---|
| `src/` | `SmokeRunner` (the core bring-up), `BundledModuleRunner` (the Bare module) and `main.cpp` (the window and the shutdown paths) |
| `modules/` | the Bundled module's manifest, compiled into the host |
| `../bare-counter/` | the Bundled module itself |
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
