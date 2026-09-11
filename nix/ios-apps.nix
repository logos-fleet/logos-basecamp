# The iOS apps built out of this repo's Bundled set, and the runners that put
# them on a simulator or a device.
#
# Two apps, one pipeline:
#
#   LiblogosSmoke   the bring-up probe -- liblogos_core with the Bundled set
#                   in it, a log on screen and the verdicts on the console.
#   BasecampShell   the same host with Basecamp's REAL UI shell on top of it:
#                   main_ui linked in statically, driven through IShellHost,
#                   its Modules tab listing the set.
#
# They share everything below the UI: the same stage, the same embedded
# frameworks, the same computed export list, the same Xcode step. That is the
# point of having both -- when the Shell cannot see a module, the probe says
# whether the module or the Shell is the reason.
#
# The pure/impure split is forced by the platform: an iOS .app is linked and
# signed by Xcode, which cannot run inside the nix sandbox. Everything up to
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
  # nix/shell-ui-ios.nix: the design system and main_ui as static archives,
  # plus the QML source roots qmlimportscanner has to walk to find the Qt
  # plugins the Shell's compiled-in bytecode imports.
  shellUi,
}:

let
  inherit (pkgs) lib;
  buildPkgs = pkgs.pkgsBuildBuild;
  appleSdk = pkgs.qt6.qtbase.appleSdk;

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
  # What each of those is called once embedded: <stem>.framework.
  bundleNames = map baseNameOf bundleDirs;
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

  smokeStage = pkgs.mkIosCmakeStage {
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

  # The Shell host: the same Native-container host with Basecamp's real UI
  # shell on top. It LINKS smokeStage rather than rebuilding it, so the core,
  # its archives and the Bundled-set manifest compiled into it are one build
  # shared by both apps -- there is no second answer to "what is in the set".
  shellStage = pkgs.mkIosCmakeStage {
    pname = "basecamp-shell-host-ios";
    version = "0.1.0";
    inherit src;
    sourceDir = "mobile/basecamp-shell/stage";
    buildInputs = [ smokeStage ] ++ shellUi.prefixes ++ roots;
    cmakeFlags = [
      "-DCMAKE_FIND_ROOT_PATH=${joined ([ smokeStage ] ++ shellUi.prefixes ++ roots)}"
      "-DCMAKE_PREFIX_PATH=${joined ([ smokeStage ] ++ shellUi.prefixes)}"
    ];
    # Same list, same reason as the smoke stage: the archive exports nothing
    # itself, and carries the flags so the Xcode link can force-load and
    # export what the Bundled set resolves upward.
    exportedSymbolFiles = [ exportedSymbols ];
  };

  # Everything the two apps do NOT differ in -- the embed list, the exported
  # symbols, the /nix/store scan, the Frameworks-versus-manifest diff -- lives
  # in nix/ios-runner.nix, and is therefore the same check on both.
  #
  # That file takes plain strings rather than the derivations they came from,
  # which is what lets nix/ios-runner-lint.nix render the same runners over
  # fixtures and shellcheck them for every Bundled-set size -- including the
  # one-module set that `--bundle <one app>` asks for.
  mkRunners = import ./ios-runner.nix {
    inherit lib;
    inherit (buildPkgs) writeShellApplication;
  };

  # One app: the Xcode half, and the two runners over it. Everything that
  # differs between LiblogosSmoke and BasecampShell is an argument here.
  #
  # The build dir is keyed on the stage's store path, so a rebuilt stage never
  # reuses an Xcode cache from the previous one.
  mkApp = { pname, appName, project, bundleId, appSrcDir, stage, prefixes ? [ ],
            configureFlags ? [ ] }:
    mkRunners {
      inherit pname appName project bundleId appleSdk configureFlags;
      appSrc = "${src}/${appSrcDir}";
      stagePath = "${stage}";
      frameworks = bundleNames;
      frameworkSrcs = bundleDirs;
      toolchainFile = "${pkgs.logosQtCrossToolchainFile}";
      crossCmakeFlags = pkgs.logosQtCrossCmakeFlags;
      symbolExportFlags = stage.logosIosSymbolExports.cmakeFlags;
      prefixPath = joined ([ stage ] ++ prefixes);
      findRootPath = joined ([ stage ] ++ prefixes ++ roots);
      versionGate = pkgs.xcodeWrapper.versionGate;
      runtimeInputs = [ buildPkgs.cmake pkgs.xcodeWrapper ];
    };

  smokeApp = mkApp {
    pname = "liblogos-smoke";
    appName = "LiblogosSmoke";
    project = "LiblogosSmokeIos";
    bundleId = "co.logos.liblogos.smoke";
    appSrcDir = "mobile/liblogos-smoke/app";
    stage = smokeStage;
  };

  shellApp = mkApp {
    pname = "basecamp-shell";
    appName = "BasecampShell";
    project = "BasecampShellIos";
    bundleId = "co.logos.basecamp.shell";
    appSrcDir = "mobile/basecamp-shell/app";
    stage = shellStage;
    prefixes = [ smokeStage ] ++ shellUi.prefixes;
    # qmlimportscanner reads QML SOURCE to decide which static Qt QML plugins
    # the executable needs; the Shell's QML is compiled bytecode inside the
    # archives, so the scan has to be pointed at the trees it came from.
    configureFlags = [
      "-DBASECAMP_QML_SCAN_ROOTS=${lib.concatStringsSep ";" shellUi.qmlScanRoots}"
    ];
  };
in
shellUi.packages
// {
  liblogos-smoke-host-ios = smokeStage;
  basecamp-shell-host-ios = shellStage;
  # The resolved, verified, embedded Bundled set on its own -- what `ws build
  # <repo> --target <variant> --bundle <apps>` builds. An .app needs Xcode and
  # cannot be a nix derivation (ADR 0002), so this is the part of the app image
  # that IS one, and the runners below embed exactly it.
  bundled-set = bundledSet;
}
// lib.optionalAttrs (appleSdk == "iphonesimulator") {
  run-liblogos-smoke-ios-sim = smokeApp.runSim;
  run-basecamp-shell-ios-sim = shellApp.runSim;
}
// lib.optionalAttrs (appleSdk == "iphoneos") {
  run-liblogos-smoke-ios-device = smokeApp.runDevice;
  run-basecamp-shell-ios-device = shellApp.runDevice;
}
