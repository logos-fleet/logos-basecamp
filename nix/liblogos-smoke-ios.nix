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
  # The app's Bundled set (nix/bundled-set.nix): every module named on
  # --bundle plus its closure, pulled out of the catalog, signature- and
  # Merkle-checked, and staged as
  #     Frameworks/<stem>.framework/...
  #     bundled-set.json
  # Its `modules` passthru is the same resolution AT EVAL, which is what lets
  # the embed list, the symbol scan and the view module's stem all be derived
  # from one answer instead of three.
  bundledSet,
  # logos-view-module-runtime's source tree. Headers only: the host needs
  # LogosViewPlugin.h to cast the plugin it constructs, and nothing else from
  # that repo -- ui-host and its library are a desktop concern.
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

  # ── the Bundled set ─────────────────────────────────────────────────────
  # Resolved at eval by mkBundledSet, so nothing here globs the result: the
  # list the app embeds and the list the manifest records are the same list.
  bundledModules = bundledSet.modules;
  bundleDirs = map (m: "${bundledSet}/${m.bundle}") bundledModules;
  bundleImages = map (m: "${bundledSet}/${m.image}") bundledModules;
  viewModules = lib.filter (m: m.type == "ui_qml") bundledModules;
  # <stem>.framework -> <stem>. The runner dlopens by stem.
  stemOf = m: lib.removeSuffix ".framework" (baseNameOf m.bundle);
  viewModuleName =
    if viewModules == [ ] then ""
    else if lib.length viewModules == 1 then stemOf (lib.head viewModules)
    else throw ("logos-basecamp: the iOS smoke host renders ONE view module "
      + "into its single QQuickWidget, and this Bundled set carries "
      + lib.toString (lib.length viewModules) + ": "
      + lib.concatMapStringsSep ", " (m: m.name) viewModules);

  # The symbols the app must FORCE-LOAD and EXPORT so the modules resolve them
  # upward at dlopen (ADR 0006).
  #
  # Computed, never listed. It is the module images' own undefined symbols
  # INTERSECTED with what the Logos archives define -- so it is exactly the set
  # that has to come from the app and nothing else. Everything the modules get
  # from /usr/lib (libc++, libSystem) drops out of the intersection on its own,
  # because no archive here defines it. A hand-written list would be right
  # until a module gained a call.
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
    for image in ${lib.escapeShellArgs bundleImages}; do
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
      echo "error: the Bundled set resolves NOTHING upward from the app." >&2
      echo "That cannot be right for modules that speak the logos-protocol" >&2
      echo "C ABI -- it means the intersection is being computed against the" >&2
      echo "wrong archives, and the app would export nothing." >&2
      echo "--- undefined in the set:" >&2; cat undefined.txt >&2
      exit 1
    fi
    echo "app must export $(wc -l < $out) symbol(s) for ${
      lib.concatMapStringsSep " + " (m: m.name) bundledModules}" >&2
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
      # The Bundled-set manifest, compiled into the host: what it carries, in
      # load order, with each image's bundle-relative path. It is the ONLY
      # thing the host knows about its set -- see mobile/liblogos-smoke/cmake.
      "-DLOGOS_BUNDLED_SET_MANIFEST=${bundledSet}/bundled-set.json"
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
    stage_frameworks() {
      rm -rf "$build_dir/embed"
      mkdir -p "$build_dir/embed"
      cp -R ${lib.escapeShellArgs bundleDirs} "$build_dir/embed/"
      chmod -R u+w "$build_dir/embed"
      embed_fws=""
      for fw in ${lib.escapeShellArgs (map baseNameOf bundleDirs)}; do
        embed_fws="''${embed_fws:+$embed_fws;}$build_dir/embed/$fw"
        echo "==> bundled: $fw"
      done
    }

    configure_app() {
      mkdir -p "$build_dir"
      stage_frameworks
      echo "==> configure ($build_dir)"
      cmake -S "$app_src" -B "$build_dir" -G Xcode \
        -DCMAKE_TOOLCHAIN_FILE=${pkgs.logosQtCrossToolchainFile} \
        ${lib.escapeShellArgs pkgs.logosQtCrossCmakeFlags} \
        ${lib.escapeShellArgs stage.logosIosSymbolExports.cmakeFlags} \
        "-DLOGOS_IOS_BUNDLED_FRAMEWORKS=$embed_fws" \
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

      # AC: the app's Frameworks/ directory IS the resolved closure -- every
      # member of the Bundled set, and nothing else. Asserted here rather than
      # trusted, because a copy phase that silently did nothing leaves an app
      # that starts, runs the core and only then reports a missing module --
      # three steps from the cause. And an EXTRA framework is worse than a
      # missing one: it is native code in the app image that the manifest does
      # not account for.
      find "$app/Frameworks" -mindepth 1 -maxdepth 1 -exec basename {} \; \
        | sort > "$build_dir/got-frameworks.txt"
      printf '%s\n' ${lib.escapeShellArgs (map baseNameOf bundleDirs)} \
        | sort > "$build_dir/want-frameworks.txt"
      if ! diff -u "$build_dir/want-frameworks.txt" "$build_dir/got-frameworks.txt"; then
        echo "error: <App>.app/Frameworks does not match the Bundled set" >&2
        exit 1
      fi
      for fw in ${lib.escapeShellArgs (map baseNameOf bundleDirs)}; do
        bin="$app/Frameworks/$fw/''${fw%.framework}"
        [ -f "$bin" ] || { echo "error: $fw has no binary at $bin" >&2; exit 1; }
      done
      echo "==> embedded, and matching bundled-set.json: $(tr '\n' ' ' < "$build_dir/got-frameworks.txt")"
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
        for fw in ${lib.escapeShellArgs (map baseNameOf bundleDirs)}; do
          codesign -dv "$app/Frameworks/$fw" 2>&1 \
            | grep -E 'Identifier|TeamIdentifier|Signature' || true
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
  # The resolved, verified, embedded Bundled set on its own -- what `ws build
  # <repo> --target <variant> --bundle <apps>` builds. An .app needs Xcode and
  # cannot be a nix derivation (ADR 0002), so this is the part of the app image
  # that IS one, and the runners below embed exactly it.
  bundled-set = bundledSet;
}
// lib.optionalAttrs (appleSdk == "iphonesimulator") {
  run-liblogos-smoke-ios-sim = runSim;
}
// lib.optionalAttrs (appleSdk == "iphoneos") {
  run-liblogos-smoke-ios-device = runDevice;
}
