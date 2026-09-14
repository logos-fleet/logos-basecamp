# The Android apps built out of this repo's Bundled set, and the runners that
# put them on the attached device.
#
# Two apps, one pipeline -- the same pair as nix/ios-apps.nix:
#
#   LiblogosSmoke   the bring-up probe -- liblogos_core with the Bundled set
#                   in it, a log on screen and the verdicts on the console.
#   BasecampShell   the same host with Basecamp's REAL UI shell on top of it:
#                   main_ui linked in statically, driven through IShellHost,
#                   its Modules tab listing the set.
#
# They share everything below the UI: the same flattened library set, the same
# packaged-but-not-auto-loaded Bundled modules, the same DT_NEEDED assertions,
# the same runner. That is the point of having both -- when the Shell cannot
# see a module, the probe says whether the module or the Shell is the reason.
#
# ONE DERIVATION PER APP, unlike iOS: androiddeployqt and gradle both run
# inside the sandbox, so there is no impure half and no Xcode.
{
  pkgs,
  # logos-liblogos's mobile chain for the aarch64-android set.
  chain,
  src,
  # The app's Bundled set for aarch64-android (nix/bundled-set.nix): every
  # module named on --bundle, pulled out of the catalog and verified, staged as
  #     lib/lib<stem>.so
  #     bundled-set.json
  # Each .so travels in the APK like every other Logos one and lands in the
  # app's native library directory, which since API 29 is the only place
  # Android will dlopen from at all.
  bundledSet,
  # nix/mobile-web-assets.nix: the bundled Qt-wasm QML runtime and the
  # Downloaded `web` modules this build ships. Packaged as APK ASSETS (the only
  # way an APK carries a directory tree) and unpacked into the app's data
  # directory on first launch -- see unpackAndroidWebAssets.
  webAssets,
  # logos-view-module-runtime's source tree. Headers only: the host needs
  # LogosViewPlugin.h to cast the plugin it constructs -- and on this platform
  # it does construct one now, out of the `ui_qml` member's lib<stem>_view.so.
  # BundledSetShellHost is the same file on both phones.
  viewRuntimeSrc,
  # nix/shell-ui-android.nix: the design system and main_ui as static archives,
  # plus the QML source roots qmlimportscanner has to walk to find the Qt QML
  # modules the Shell's compiled-in bytecode imports.
  shellUi,
}:

let
  inherit (pkgs) lib;
  activity = "org.qtproject.qt.android.bindings.QtActivity";

  # The Bundled set's .so names. `lib` prefix because an APK carries only
  # lib*.so; the `_bare` stem suffix is how liblogos tells a Bare module from a
  # Qt plugin. Read off the SAME eval-time resolution the manifest was written
  # from -- nothing here globs the staged set.
  bundledSos = map (m: baseNameOf m.image) bundledSet.modules;

  # ── the ONE view module, if the set carries one ─────────────────────────
  # Same derivation as nix/ios-apps.nix, off the same eval-time resolution, and
  # with the same refusal when a set carries more than one: the host renders a
  # view module into a single QQuickWidget, and which one that is has to be a
  # build's answer rather than a guess at runtime. Empty when the set has no
  # `ui_qml` member -- the Shell then asks the manifest and shows no app tile,
  # which is the honest state and not a failure.
  #
  # lib<stem>.so -> <stem>: the runner dlopens by stem and re-decorates.
  viewModules = lib.filter (m: m.type == "ui_qml") bundledSet.modules;
  stemOf = m: lib.removeSuffix ".so" (lib.removePrefix "lib" (baseNameOf m.image));
  viewModuleName =
    if viewModules == [ ] then ""
    else if lib.length viewModules == 1 then stemOf (lib.head viewModules)
    else throw ("logos-basecamp: the Android host renders ONE view module into "
      + "its single QQuickWidget, and this Bundled set carries "
      + lib.toString (lib.length viewModules) + ": "
      + lib.concatMapStringsSep ", " (m: m.name) viewModules);

  # lib.getLib: nixpkgs' openssl, fmt and icu default to their bin or dev
  # output, and an APK built from those carries no .so at all -- mkQtAndroidApk
  # then fails with "these DT_NEEDED sonames are neither in the APK nor
  # provided by Android".
  # `chain.all` carries the third-party tail liblogos_core links; the extra
  # entries are what those link in turn (fmt, icu, zlib) and libsodium, which
  # only lgx uses.
  libRoots = lib.unique (
    map lib.getLib (
      chain.all
      ++ [
        pkgs.fmt
        pkgs.libsodium
        pkgs.icu
        pkgs.zlib
      ]
    )
  );
  # spdlog too: the iOS chain carries it in `all`, the Android one does not,
  # and the host compiles against it to hang a logcat sink on the core's
  # channels (BundledModuleRunner). Same prefix the chain linked, so there is
  # no second spdlog.
  # lib.getDev: nixpkgs' spdlog splits its headers into a `dev` output, and the
  # default one carries lib/ alone -- passing it here adds an include root that
  # exists and holds nothing.
  # ...and fmt beside it: this spdlog is built against an external fmt
  # (SPDLOG_FMT_EXTERNAL), so spdlog/fmt/fmt.h includes <fmt/format.h>.
  includeRoots = chain.all ++ map lib.getDev [ pkgs.spdlog pkgs.fmt ];
  joined = lib.concatMapStringsSep ";" toString;

  # An APK carries only files named lib<name>.so, and nixpkgs' cross libraries
  # are versioned (libspdlog.so.1.17, libssl.so.3, libicuuc.so.76) with the
  # Logos libraries referencing those sonames. Flatten: copy under the
  # unversioned name and rewrite SONAME and every DT_NEEDED to match. Qt's own
  # libraries are androiddeployqt's business and are excluded.
  apkLibs = pkgs.pkgsBuildBuild.runCommand "logos-android-apk-libs" {
    nativeBuildInputs = [ pkgs.pkgsBuildBuild.patchelf ];
  } ''
    # The unversioned name a library travels under in the APK. Android ships
    # private libicu*, libssl and libcrypto; a DT_NEEDED by the same name
    # resolves to THOSE, not to ours, and the app dies at dlopen with a
    # missing C++-mangled ICU symbol. Rename ours so no collision is possible.
    apk_name() {
      local stem="''${1%%.so*}"
      case "$stem" in libicu*|libssl|libcrypto) stem="''${stem}_lg" ;; esac
      echo "$stem.so"
    }
    mkdir -p $out/lib
    for root in ${lib.concatStringsSep " " (map toString libRoots)}; do
      for f in "$root"/lib/lib*.so*; do
        [ -e "$f" ] || continue
        [ -L "$f" ] && continue
        name=$(basename "$f")
        case "$name" in libQt6*|*.a|*.la) continue ;; esac
        base=$(apk_name "$name")
        # Several prefixes stage the same library (liblogos and the package
        # manager copy their closure beside themselves); one copy is enough.
        [ -e "$out/lib/$base" ] && continue
        cp "$f" "$out/lib/$base"
        chmod u+w "$out/lib/$base"
        patchelf --set-soname "$base" "$out/lib/$base"
      done
    done
    for l in $out/lib/*.so; do
      for n in $(patchelf --print-needed "$l"); do
        base=$(apk_name "$n")
        [ "$n" = "$base" ] || patchelf --replace-needed "$n" "$base" "$l"
      done
    done
    ls -la $out/lib
  '';

  # ── the Bundled module, PACKAGED BUT NOT AUTO-LOADED ────────────────────
  # It is dropped into androiddeployqt's output directory AFTER the tool has
  # written res/values/libs.xml, so gradle packages it (jniLibs.srcDirs is
  # `libs`) and QtLoader never hears about it.
  #
  # This is not tidiness. QT_ANDROID_EXTRA_LIBS is a LOAD list: QtLoader
  # System.load()s every entry on the qtMainLoopThread before any Logos code
  # runs. A Bare module leaves every `lp_*` undefined by design, and that
  # early load resolves them against nothing -- measured on an SM-G990B, the
  # app died before its first frame with
  #   java.lang.UnsatisfiedLinkError: dlopen failed: cannot locate symbol
  #   "lp_token_save" referenced by ".../libbare_counter_bare.so"
  # The module must be opened by the Native container, once the core has
  # registered it.
  stageBundledSet = ''
    for so in ${lib.escapeShellArgs bundledSos}; do
      install -Dm755 "${bundledSet}/lib/$so" \
        "android-build/libs/${pkgs.androidPkgs.abi}/$so"

      # A module names the host's protocol image in its own DT_NEEDED
      # (logos-module-builder does that at link time -- on Android bionic
      # offers a dlopen'd library no other way to reach an app library's
      # symbols). Assert both halves here, because the APK is where they have
      # to meet: a module whose dependency the APK does not carry is
      # unloadable, and the device says so only at load.
      needed=$(patchelf --print-needed \
        "android-build/libs/${pkgs.androidPkgs.abi}/$so")
      grep -qx liblogos_protocol.so <<< "$needed" || {
        echo "error: $so does not name liblogos_protocol.so in DT_NEEDED;" >&2
        echo "its lp_* would resolve against nothing on this platform. NEEDED was:" >&2
        printf '  %s\n' $needed >&2
        exit 1
      }

      # libs.xml is what QtLoader reads. If a module ever appears in it, the
      # app is back to the crash above -- and it would look like a Qt problem.
      if grep -q "$so" android-build/res/values/libs.xml; then
        echo "error: $so is in libs.xml; QtLoader would load it eagerly" >&2
        exit 1
      fi
    done

    [ -f "android-build/libs/${pkgs.androidPkgs.abi}/liblogos_protocol.so" ] || {
      echo "error: liblogos_protocol.so is not in the APK, and the Bundled set needs it" >&2
      exit 1
    }
    echo "==> Bundled set packaged, not auto-loaded: ${lib.concatStringsSep " " bundledSos}"
  '';

  adb = "${pkgs.androidPkgs.androidsdk}/bin/adb";

  # One app: the APK, and the runner over it. Everything that differs between
  # LiblogosSmoke and BasecampShell is an argument here.
  mkApp =
    {
      pname,
      # The qt_add_executable target; androiddeployqt keys its files on it.
      target,
      packageName,
      # The directory holding this app's CMakeLists.txt, relative to src.
      appDir,
      # What the host prefixes its console lines with, so the runner shows them.
      consoleTag,
      qtModules,
      cmakeFlags ? [ ],
      # Shell snippet run after androiddeployqt has laid the gradle project
      # out and before gradle packages it, for whatever THIS app has to be
      # true about its deployment. Runs beside the Bundled-set staging, which
      # every app shares.
      deployChecks ? "",
    }:
    let
      apk = (pkgs.mkQtAndroidApk {
        inherit pname target packageName src qtModules;
        version = "0.1.0";
        buildInputs = libRoots ++ [ apkLibs ];
        cmakeFlags = [
          "-DCMAKE_BUILD_TYPE=Release"
          "-DLOGOS_LIB_ROOTS=${apkLibs}"
          "-DLOGOS_INCLUDE_ROOTS=${joined includeRoots}"
          # The Bundled-set manifest, compiled into the host: what it carries,
          # in load order, with each image's path. It is the ONLY thing the
          # host knows about its set -- see mobile/liblogos-smoke/cmake.
          "-DLOGOS_BUNDLED_SET_MANIFEST=${bundledSet}/bundled-set.json"
        ]
        ++ cmakeFlags;
        meta.description = "${target}, packaged as an Android APK";
      }).overrideAttrs (old: {
        setSourceRoot = "sourceRoot=$(echo */${appDir})";
        gradleFlags = (old.gradleFlags or [ ]) ++ [ "--stacktrace" ];
        nativeBuildInputs = (old.nativeBuildInputs or [ ]) ++ [ pkgs.pkgsBuildBuild.patchelf ];
        preBuild = (old.preBuild or "") + stageBundledSet + deployChecks;
      });
      apkFile = "${apk}/${apk.apkName}";

      runner = pkgs.buildPackages.writeShellScriptBin "run-${pname}-android" ''
        set -euo pipefail
        adb=${adb}; apk=${apkFile}; pkg=${packageName}
        export ANDROID_SERIAL="''${ANDROID_SERIAL:-$("$adb" devices | awk 'NR > 1 && $2 == "device" { print $1; exit }')}"
        echo "run-${pname}-android: installing $apk on $ANDROID_SERIAL ($(du -k "$apk" | cut -f1) KB)"
        "$adb" install -r "$apk" >/dev/null || { "$adb" uninstall "$pkg" >/dev/null; "$adb" install -r "$apk"; }
        "$adb" logcat -c || true
        # The app's own command line, if this runner was given one. Qt reads it
        # off the `extraappparams` intent extra, base64 of a plain argument
        # string, and only in a debuggable build -- which an `assembleDebug`
        # APK is (QtActivityLoader: "Not in debug mode! It is not allowed to
        # use extra arguments in non-debug mode."). It is the Android half of
        # the trailing arguments simctl and devicectl take, and it is how the
        # host is told which desktop peer to dial.
        if [ "$#" -gt 0 ]; then
          params=$(printf '%s ' "$@" | ${pkgs.buildPackages.coreutils}/bin/base64 | tr -d '\n')
          echo "run-${pname}-android: app arguments: $*"
          "$adb" shell am start -W -n "$pkg/${activity}" --es extraappparams "$params" >/dev/null
        else
          "$adb" shell am start -W -n "$pkg/${activity}" >/dev/null
        fi
        pid=$("$adb" shell pidof "$pkg" | tr -d '\r')
        echo "run-${pname}-android: $pkg pid $pid; console follows (Ctrl-C to stop)"
        "$adb" logcat --pid="$pid" -v raw '*:V' \
          | grep --line-buffered -E '^\[${consoleTag}\]|^\[qt\] .*(warning|error|fatal|Fatal)|libc|DEBUG|FATAL'
      '';
    in
    {
      inherit apk runner;
    };

  smokeApp = mkApp {
    pname = "liblogos-smoke";
    target = "LiblogosSmoke";
    packageName = "co.logos.liblogos.smoke";
    appDir = "mobile/liblogos-smoke/android";
    consoleTag = "smoke";
    qtModules = with pkgs.qt6; [
      qtbase
      qtremoteobjects
    ];
    cmakeFlags = [
      "-DQT_ADDITIONAL_PACKAGES_PREFIX_PATH=${pkgs.qt6.qtremoteobjects}"
      # The Web container's half of the app image. Both apps ship it: the
      # probe DRIVES the app's own `web` modules as an acceptance pass, and the
      # Shell RUNS what a user installed from the catalog into it.
      "-DLOGOS_ANDROID_WEB_ASSETS=${webAssets}"
    ];
  };

  # The Shell. It is the probe's host plus main_ui and the design system, so
  # the extra arguments are exactly those two prefixes and the two things a
  # real Qt Quick UI needs on this platform: the QML roots androiddeployqt
  # scans, and qtdeclarative/qtsvg in the one prefix it reads.
  #
  # `find_package(<pkg> CONFIG)` is given its directory rather than a search
  # path: the NDK toolchain file sets CMAKE_FIND_ROOT_PATH_MODE_PACKAGE to
  # ONLY, so a prefix that is not under CMAKE_FIND_ROOT_PATH is not searched
  # at all and the failure reads as a missing package rather than a
  # cross-compilation rule.
  shellApp = mkApp {
    pname = "basecamp-shell";
    target = "BasecampShell";
    packageName = "co.logos.basecamp.shell";
    appDir = "mobile/basecamp-shell/android";
    consoleTag = "shell";
    qtModules = with pkgs.qt6; [
      qtbase
      qtdeclarative
      qtshadertools
      qtsvg
      qtremoteobjects
    ];
    cmakeFlags = [
      ("-DQT_ADDITIONAL_PACKAGES_PREFIX_PATH="
        + joined (with pkgs.qt6; [
          qtdeclarative
          qtshadertools
          qtsvg
          qtremoteobjects
        ]))
      ("-DBasecampMainUI_DIR="
        + "${shellUi.packages.main-ui-plugin}/lib/cmake/BasecampMainUI")
      ("-DLogosDesignSystem_DIR="
        + "${shellUi.packages.design-system}/lib/cmake/LogosDesignSystem")
      "-DBASECAMP_QML_SCAN_ROOTS=${lib.concatStringsSep ";" shellUi.qmlScanRoots}"
      "-DLOGOS_VIEW_RUNTIME_INCLUDE=${viewRuntimeSrc}/include"
      # Which view image the runner opens, resolved out of the Bundled set the
      # same way the iOS app resolves it. Empty when the set carries no `ui_qml`
      # member -- the host asks the manifest rather than assuming one is there.
      "-DLOGOS_VIEW_MODULE_STEM=${viewModuleName}"
      # The Web container's half of the app image, which the Shell ships now
      # too: a Store shell INSTALLS `web` variants and nothing else, so without
      # the QML runtime a module it installed from the catalog has nowhere to
      # run.
      "-DLOGOS_ANDROID_WEB_ASSETS=${webAssets}"
    ];
    # Every icon in the Shell is an SVG in a qrc, and the plugin that decodes
    # one is not in any list Qt builds by itself -- see the CMakeLists for
    # why. Missing, the app installs, launches and draws empty squares, so
    # the only place to notice is here.
    deployChecks = ''
      _svg=android-build/libs/${pkgs.androidPkgs.abi}/libplugins_imageformats_qsvg_${pkgs.androidPkgs.abi}.so
      [ -f "$_svg" ] || {
        echo "error: the APK carries no SVG image-format plugin." >&2
        echo "Every icon in the Shell is an SVG in a qrc; without it they render" >&2
        echo "as empty squares and the app looks fine otherwise. Deployed plugins:" >&2
        ls android-build/libs/${pkgs.androidPkgs.abi}/libplugins_* >&2 || true
        exit 1
      }
      echo "==> SVG image-format plugin deployed"
    '';
  };
in
shellUi.packages
// {
  liblogos-smoke-android = smokeApp.apk;
  basecamp-shell-android = shellApp.apk;
  # The resolved, verified, staged Bundled set on its own -- what `ws build
  # <repo> --target android-arm64 --bundle <apps>` builds. Both APKs above
  # package exactly it.
  bundled-set = bundledSet;
  # The `web` half on its own, so `nix build` can weigh it without an APK.
  web-assets = webAssets;
  run-liblogos-smoke-android = smokeApp.runner;
  run-basecamp-shell-android = shellApp.runner;
}
