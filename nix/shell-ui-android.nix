# The Basecamp Shell, cross-built for Android as static archives.
#
# The same two stages as nix/shell-ui-ios.nix and for the same reasons: the
# design system's QML modules, and `main_ui` itself. Neither carries a line of
# Logos runtime -- main_ui links Qt and the design system, reaches its host
# only through IShellHost, and nix/symbol-gate guards that on the desktop. So
# this file has no logos input either, and the Shell that runs on a phone is
# the same source the desktop plugin is built from.
#
# STATIC ON ANDROID TOO, and here that IS a choice rather than the platform's.
# Qt for Android is a set of shared objects, so a `main_ui.so` would link and
# QPluginLoader would happily dlopen it -- but an APK has exactly one directory
# an app may dlopen from (its native library dir, the only one since API 29),
# and a Qt plugin dropped in there is a plugin PATH problem on top of a
# packaging one. Importing the archive with Q_IMPORT_PLUGIN gives both phones
# ONE host: mobile/basecamp-shell/src/main.cpp resolves the shell through
# QPluginLoader::staticInstances() on either, with the IShellView contract and
# its ABI check exactly as they are on the desktop (src/CMakeLists.txt's
# MAIN_UI_STATIC).
#
# The design system is built here from its SOURCE rather than through its own
# flake, same as on iOS: that flake enumerates desktop systems only.
{ pkgs, src, version, designSystemSrc }:

let
  inherit (pkgs) lib;

  # A CMake project cross-built for this Android set and installed as static
  # archives. pkgs.mkIosCmakeStage is logos-nix's answer to the same question
  # on the other phone; there is no Android equivalent there because until now
  # the only thing built for Android was an APK, which is one derivation
  # (nix/android-apps.nix) and needs no stage at all.
  mkStage =
    {
      pname,
      src,
      sourceDir ? ".",
      buildInputs ? [ ],
      cmakeFlags ? [ ],
    }:
    pkgs.stdenv.mkDerivation {
      inherit pname version src;

      nativeBuildInputs = [
        pkgs.cmake
        pkgs.ninja
      ];
      # qtshadertools: a qt_add_qml_module compiles any .frag/.vert it is given
      # through it, and the design system is where a Logos shader would land.
      buildInputs = [
        pkgs.qt6.qtbase
        pkgs.qt6.qtdeclarative
        pkgs.qt6.qtshadertools
        pkgs.qt6.qtsvg
      ]
      ++ buildInputs;

      cmakeDir = "../${sourceDir}";
      cmakeFlags = [
        "-DCMAKE_TOOLCHAIN_FILE=${pkgs.logosQtCrossToolchainFile}"
        # GNUInstallDirs is asked for a libdir by both projects below, and a
        # consumer resolves ${prefix}/lib/cmake/... by name. Android's
        # toolchain file leaves it at `lib` today; saying so keeps that from
        # being a fact about the toolchain.
        "-DCMAKE_INSTALL_LIBDIR=lib"
      ]
      ++ pkgs.logosQtCrossCmakeFlags
      ++ cmakeFlags;

      # No Qt app is installed by either stage -- the wrapper hook would look
      # for one and find a cross-built archive.
      dontWrapQtApps = true;

      # Static is the contract: a shared object here is an archive that
      # silently became a plugin, and the APK would package it without
      # QtLoader or the host ever opening it.
      postInstall = ''
        _dynamic=$(find $out -name '*.so' -o -name '*.so.*')
        if [ -n "$_dynamic" ]; then
          echo "error: shared object in a static-only Android stage:" >&2
          echo "$_dynamic" >&2
          exit 1
        fi
        [ -n "$(find $out -name '*.a' -print -quit)" ] || {
          echo "error: no static archive installed" >&2; exit 1; }
      '';

      meta.platforms = lib.platforms.all;
    };

  designSystem = mkStage {
    pname = "logos-design-system-android";
    src = designSystemSrc;
  };

  mainUi = mkStage {
    pname = "logos-basecamp-main-ui-android";
    inherit src;
    sourceDir = "src";
    buildInputs = [ designSystem ];
    cmakeFlags = [
      "-DMAIN_UI_STATIC=ON"
      "-DLogosDesignSystem_DIR=${designSystem}/lib/cmake/LogosDesignSystem"
    ];
  };
in
{
  # Named as nix/shell-ui-ios.nix names them, and for the same reason it
  # gives: the target is in the attribute path, so
  # `legacyPackages.<host>.mobile.aarch64-android.main-ui-plugin` is the same
  # shell as `.#main-ui-plugin`, for another platform.
  packages = {
    design-system = designSystem;
    main-ui-plugin = mainUi;
  };

  # androiddeployqt decides which Qt QML modules to PACKAGE by running
  # qmlimportscanner over the app's QML roots -- the same question
  # qmlimportscanner answers for the iOS link, and the same problem: the
  # Shell's QML is compiled bytecode inside these archives, so the scan has to
  # be pointed at the trees they were compiled from. Without it the APK ships
  # no QtQuick and every `import QtQuick` fails at runtime with "module is not
  # installed" (the Android spelling of the iOS blank pane).
  qmlScanRoots = [
    "${src}/src/Basecamp"
    "${designSystemSrc}/src/qml"
  ];
}
