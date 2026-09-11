# The liblogos smoke host for iOS: its pure half as a static archive
# (pkgs.mkIosCmakeStage), plus run-liblogos-smoke-ios-sim /
# run-liblogos-smoke-ios-device -- the impure Xcode-generator link and the
# simctl / devicectl step.
#
# The split is forced by the platform: an iOS .app is linked and signed by
# Xcode, which cannot run inside the nix sandbox. Everything up to
# "one static archive with everything in it" is pure and cached; the impure
# runner does the link, the install and the launch.
{
  pkgs,
  # logos-liblogos's mobile chain for THIS package set: { liblogos, all, ... }.
  chain,
  # The basecamp source tree (this flake's ./.).
  src,
  # The Bundled Bare module for THIS target: an .framework bundle under
  # Library/Frameworks/ (logos-module-builder's `bare` output on an iOS
  # package set).
  bareModule,
  # The Bundled VIEW module for THIS target: logos-module-builder's `view`
  # output — one embedded framework carrying a ui_qml module's Qt backend and
  # its QML, with Qt and LogosAPI bound upward into this app.
  viewModule,
  # logos-view-module-runtime's source tree. Headers only: the host needs
  # LogosViewPlugin.h to cast the plugin it constructs, and nothing else from
  # that repo — ui-host and its library are a desktop concern.
  viewRuntimeSrc,
}:

let
  inherit (pkgs) lib;
  buildPkgs = pkgs.pkgsBuildBuild;
  appleSdk = pkgs.qt6.qtbase.appleSdk;
  bundleId = "co.logos.liblogos.smoke";

  # `chain.all` is the whole link set, third-party tail included: every
  # prefix whose lib/*.a is linked and whose include/ is compiled against.
  roots = chain.all;
  joined = lib.concatMapStringsSep ";" toString;

  # ── the Bundled modules ─────────────────────────────────────────────────
  bareModuleName = "bare_counter_bare";
  bareFramework = "${bareModule}/Library/Frameworks/${bareModuleName}.framework";
  viewModuleName = "view_counter_view";
  viewFramework = "${viewModule}/Library/Frameworks/${viewModuleName}.framework";

  # The symbols the app must FORCE-LOAD and EXPORT so the module resolves them
  # upward at dlopen (ADR 0006).
  #
  # Computed, never listed. It is the module image's own undefined symbols
  # INTERSECTED with what the Logos archives define -- so it is exactly the set
  # that has to come from the app and nothing else. Everything the module gets
  # from /usr/lib (libc++, libSystem) drops out of the intersection on its own,
  # because no archive here defines it. A hand-written list would be right
  # until the module gained a call.
  #
  # AND QT IS IN THE INTERSECTION NOW, not just the Logos archives. A Bare
  # module contains no Qt by definition, so `chain.all` was the whole universe
  # of what it could resolve upward. A VIEW framework is the opposite case: it
  # is nothing but Qt calls and carries none of them, so most of its undefined
  # set is QtCore/QtGui/QtQml/QtQuick/QtRemoteObjects -- which is only bindable
  # because logos-nix builds the iOS Qt with `reduce_exports` OFF (slice 13),
  # so those archives actually define 17k external symbols rather than hiding
  # them. Without Qt here the app would export none of them and every view
  # framework would fail at dlopen naming one mangled Qt symbol.
  #
  # Each Qt module is a static archive INSIDE a framework directory
  # (lib/QtCore.framework/QtCore), which is why the scan cannot just glob
  # lib/*.a.
  qtRoots = [
    pkgs.qt6.qtbase
    pkgs.qt6.qtdeclarative
    pkgs.qt6.qtshadertools
    pkgs.qt6.qtsvg
    pkgs.qt6.qtremoteobjects
  ];

  exportedSymbols = buildPkgs.runCommandLocal "liblogos-smoke-ios-module-symbols.txt" {
    nativeBuildInputs = [ buildPkgs.darwin.cctools ];
  } ''
    set -euo pipefail
    : > undefined.txt
    for image in "${bareFramework}/${bareModuleName}" "${viewFramework}/${viewModuleName}"; do
      nm -guj "$image" >> undefined.txt
    done
    sort -u -o undefined.txt undefined.txt

    : > defined.txt
    for root in ${lib.concatStringsSep " " (map toString roots)}; do
      for a in "$root"/lib/*.a; do
        [ -e "$a" ] || continue
        nm -gUj "$a" 2>/dev/null >> defined.txt || true
      done
    done
    for root in ${lib.concatStringsSep " " (map toString qtRoots)}; do
      for fw in "$root"/lib/*.framework; do
        [ -d "$fw" ] || continue
        bin="$fw/$(basename "$fw" .framework)"
        [ -e "$bin" ] || continue
        nm -gUj "$bin" 2>/dev/null >> defined.txt || true
      done
      for a in "$root"/lib/qt-6/qml/**/*.a "$root"/lib/*.a; do
        [ -e "$a" ] || continue
        nm -gUj "$a" 2>/dev/null >> defined.txt || true
      done
    done
    sort -u -o defined.txt defined.txt

    comm -12 undefined.txt defined.txt > $out
    if [ ! -s $out ]; then
      echo "error: the Bundled modules resolve NOTHING upward from the app." >&2
      echo "That cannot be right for a module that speaks the logos-protocol" >&2
      echo "C ABI -- it means the intersection is being computed against the" >&2
      echo "wrong archives, and the app would export nothing." >&2
      echo "--- undefined in the modules:" >&2; cat undefined.txt >&2
      exit 1
    fi
    echo "app must export $(wc -l < $out) symbol(s) for ${bareModuleName} + ${viewModuleName}" >&2
  '';

  stage = pkgs.mkIosCmakeStage {
    pname = "liblogos-smoke-host-ios";
    version = "0.1.0";
    inherit src;
    sourceDir = "mobile/liblogos-smoke/stage";
    buildInputs = roots;
    cmakeFlags = [
      "-DCMAKE_FIND_ROOT_PATH=${joined roots}"
      "-DLOGOS_IOS_LIB_ROOTS=${joined roots}"
      "-DLOGOS_IOS_INCLUDE_ROOTS=${joined roots}"
      # LogosViewPlugin.h, and the stem the runner dlopens.
      "-DLOGOS_VIEW_RUNTIME_INCLUDE=${viewRuntimeSrc}/include"
      "-DLOGOS_VIEW_MODULE_STEM=${viewModuleName}"
    ];
    # Carried by the stage only so its passthru hands the app the two flags
    # logos_ios_export_symbols() needs; the stage is a static archive and
    # exports nothing itself.
    exportedSymbolFiles = [ exportedSymbols ];
  };

  # Shared by both runners. The build dir is keyed on the stage's store path,
  # so a rebuilt stage never reuses an Xcode cache from the previous one.
  buildApp = ''
    ${pkgs.xcodeWrapper.versionGate}

    app_src=${src}/mobile/liblogos-smoke/app
    bundle_id=${bundleId}
    build_dir="''${LOGOS_IOS_SMOKE_BUILD_DIR:-''${TMPDIR:-/tmp}/liblogos-smoke-ios/$(basename ${stage})}"
    app="$build_dir/Debug-${appleSdk}/LiblogosSmoke.app"

    # Xcode's copy phase signs the framework IN PLACE after copying it, and a
    # store path is read-only all the way down -- ditto preserves that, so
    # codesign would fail on the copy. Stage a writable one first.
    stage_framework() {
      rm -rf "$build_dir/embed"
      mkdir -p "$build_dir/embed"
      cp -R "${bareFramework}" "${viewFramework}" "$build_dir/embed/"
      chmod -R u+w "$build_dir/embed"
      embed_fw="$build_dir/embed/$(basename ${bareFramework})"
      embed_view_fw="$build_dir/embed/$(basename ${viewFramework})"
      echo "==> bundled Bare module: $embed_fw"
      echo "==> bundled view module: $embed_view_fw"
    }

    configure_app() {
      mkdir -p "$build_dir"
      stage_framework
      echo "==> configure ($build_dir)"
      cmake -S "$app_src" -B "$build_dir" -G Xcode \
        -DCMAKE_TOOLCHAIN_FILE=${pkgs.logosQtCrossToolchainFile} \
        ${lib.escapeShellArgs pkgs.logosQtCrossCmakeFlags} \
        ${lib.escapeShellArgs stage.logosIosSymbolExports.cmakeFlags} \
        "-DLOGOS_IOS_BARE_MODULE_FRAMEWORK=$embed_fw" \
        "-DLOGOS_IOS_VIEW_MODULE_FRAMEWORK=$embed_view_fw" \
        "-DCMAKE_PREFIX_PATH=${stage}" \
        "-DCMAKE_FIND_ROOT_PATH=${stage};${joined roots}" \
        "$@"
    }

    xcodebuild_app() {
      echo "==> xcodebuild (${appleSdk}; full log: $build_dir/xcodebuild.log)"
      rm -rf "$app"
      set +e
      xcodebuild -project "$build_dir/LiblogosSmokeIos.xcodeproj" -target LiblogosSmoke \
        -configuration Debug -sdk ${appleSdk} -arch arm64 "$@" \
        build 2>&1 | tee "$build_dir/xcodebuild.log" | grep -E '^\*\*|error:|warning: .*ld'
      xcode_status=''${PIPESTATUS[0]}
      set -e
      if [ "$xcode_status" -ne 0 ]; then
        echo "xcodebuild failed; see $build_dir/xcodebuild.log" >&2
        exit 1
      fi
      [ -d "$app" ] || { echo "xcodebuild produced no $app" >&2; exit 1; }
      echo "==> app: $app ($(du -sk "$app" | cut -f1) KB; executable $(stat -f %z "$app/LiblogosSmoke") bytes)"
      # ADR 0002's rule for anything that leaves the machine: a store path in a
      # shipped binary is a path that does not exist on the device.
      if strings "$app/LiblogosSmoke" | grep -q /nix/store; then
        echo "error: /nix/store reference in the app executable" >&2
        strings "$app/LiblogosSmoke" | grep -m5 /nix/store >&2
        exit 1
      fi
      echo "==> no /nix/store reference in the executable"

      # AC: the framework is INSIDE <App>.app/Frameworks/ and signed. Asserted
      # here rather than trusted, because a copy phase that silently did
      # nothing leaves an app that starts, runs the core and only then reports
      # a missing module -- three steps from the cause.
      embedded="$app/Frameworks/$(basename ${bareFramework})"
      [ -d "$embedded" ] || { echo "error: no $embedded in the app bundle" >&2; exit 1; }
      [ -f "$embedded/${bareModuleName}" ] || { echo "error: $embedded has no binary" >&2; exit 1; }
      echo "==> embedded framework: $embedded"
      embedded_view="$app/Frameworks/$(basename ${viewFramework})"
      [ -d "$embedded_view" ] || { echo "error: no $embedded_view in the app bundle" >&2; exit 1; }
      [ -f "$embedded_view/${viewModuleName}" ] || { echo "error: $embedded_view has no binary" >&2; exit 1; }
      echo "==> embedded view framework: $embedded_view"
      # Only on a real signing identity. A simulator build is put together
      # with CODE_SIGNING_ALLOWED=NO and still ends up carrying an ad-hoc
      # signature with no _CodeSignature/CodeResources beside it, so
      # --verify --deep fails there for a reason that has nothing to do with
      # the framework ("code has no resources but signature indicates they
      # must be present"). Making the check conditional on the DEVICE runner
      # is what keeps it meaningful rather than routinely ignored.
      if [ -n "''${LOGOS_IOS_VERIFY_SIGNATURE:-}" ]; then
        codesign --verify --deep --strict --verbose=2 "$app" \
          || { echo "error: codesign --verify --deep failed" >&2; exit 1; }
        for bundle in "$embedded" "$embedded_view"; do
          codesign -dv "$bundle" 2>&1 | grep -E 'Identifier|TeamIdentifier|Signature' || true
        done
        echo "==> codesign --verify --deep: ok (app and every nested bundle)"
      else
        echo "==> unsigned simulator build; codesign --verify --deep is not meaningful here"
      fi
    }
  '';

  runSim = buildPkgs.writeShellApplication {
    name = "run-liblogos-smoke-ios-sim";
    runtimeInputs = [
      buildPkgs.cmake
      pkgs.xcodeWrapper
    ];
    text = ''
      ${buildApp}
      configure_app -DLOGOS_IOS_DEVELOPMENT_TEAM=
      xcodebuild_app CODE_SIGNING_ALLOWED=NO CODE_SIGNING_REQUIRED=NO CODE_SIGN_IDENTITY=

      udid="''${LOGOS_IOS_SIM:-$(xcrun simctl list devices booted | grep -o -m1 '[0-9A-F-]\{36\}' || true)}"
      if [ -z "$udid" ]; then
        udid=$(xcrun simctl list devices available | grep -m1 'iPhone' | grep -o '[0-9A-F-]\{36\}')
        echo "==> no simulator booted; booting $udid"
        xcrun simctl boot "$udid"
      fi
      open -a Simulator --args -CurrentDeviceUDID "$udid"
      xcrun simctl bootstatus "$udid" -b >/dev/null

      echo "==> install + launch on $udid (console attached)"
      xcrun simctl install "$udid" "$app"
      xcrun simctl launch --console-pty "$udid" "$bundle_id" "$@"
    '';
  };

  runDevice = buildPkgs.writeShellApplication {
    name = "run-liblogos-smoke-ios-device";
    runtimeInputs = [
      buildPkgs.cmake
      pkgs.xcodeWrapper
    ];
    text = ''
      device="''${LOGOS_IOS_DEVICE:-}"
      team="''${LOGOS_IOS_TEAM_ID:-}"
      [ -n "$team" ] || { echo "LOGOS_IOS_TEAM_ID is unset" >&2; exit 1; }
      [ -n "$device" ] || { echo "LOGOS_IOS_DEVICE is unset (xcrun devicectl list devices)" >&2; exit 1; }
      ${buildApp}
      configure_app "-DLOGOS_IOS_DEVELOPMENT_TEAM=$team"
      LOGOS_IOS_VERIFY_SIGNATURE=1 xcodebuild_app -allowProvisioningUpdates
      echo "==> install + launch on $device (console attached)"
      xcrun devicectl device install app --device "$device" "$app"
      xcrun devicectl device process launch --activate --console --terminate-existing \
        --device "$device" "$bundle_id" "$@"
    '';
  };
in
{
  liblogos-smoke-host-ios = stage;
}
// lib.optionalAttrs (appleSdk == "iphonesimulator") {
  run-liblogos-smoke-ios-sim = runSim;
}
// lib.optionalAttrs (appleSdk == "iphoneos") {
  run-liblogos-smoke-ios-device = runDevice;
}
