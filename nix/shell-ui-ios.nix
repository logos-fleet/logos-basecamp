# The Basecamp Shell, cross-built for iOS as static archives.
#
# Two stages and nothing else: the design system's QML modules, and `main_ui`
# itself. Neither carries a line of Logos runtime -- main_ui links Qt and the
# design system, reaches its host only through IShellHost, and nix/symbol-gate
# guards that on the desktop. So this file has no logos input either, and the
# Shell that runs on a phone is the same source the desktop plugin is built
# from.
#
# STATIC, not a plugin: Qt for iOS is a set of static archives, so there is no
# dlopen of main_ui to be had. The host imports it with Q_IMPORT_PLUGIN and
# reaches it through QPluginLoader::staticInstances(), which keeps the
# IShellView contract and its ABI check exactly as they are on the desktop
# (src/CMakeLists.txt's MAIN_UI_STATIC).
#
# The design system is built here from its SOURCE rather than through its own
# flake: that flake enumerates desktop systems only, and adding a mobile
# pseudo-system to it would make every consumer carry a cross toolchain it
# does not want. Its nix/ is a two-file library, so importing it costs one
# line and stays honest about which package set built it.
{ pkgs, src, version, designSystemSrc }:

let
  designSystem = pkgs.mkIosCmakeStage {
    pname = "logos-design-system-ios";
    version = "1.0.0";
    src = designSystemSrc;
  };

  mainUi = pkgs.mkIosCmakeStage {
    pname = "logos-basecamp-main-ui-ios";
    inherit version src;
    sourceDir = "src";
    buildInputs = [ designSystem ];
    cmakeFlags = [
      "-DMAIN_UI_STATIC=ON"
      "-DLogosDesignSystem_DIR=${designSystem}/lib/cmake/LogosDesignSystem"
    ];
  };
in
{
  # Flake outputs, and ONLY derivations: everything below this attribute is
  # build inputs for a host, and a non-derivation under `packages` breaks
  # `nix flake show` and `nix flake check` for every other output too.
  #
  # Named as the desktop outputs are, with the target in the name: `nix build
  # .#packages.aarch64-ios-simulator.main-ui-plugin` is the same shell as
  # `.#main-ui-plugin`, for another platform.
  packages = {
    design-system = designSystem;
    main-ui-plugin = mainUi;
  };

  # What a host needs to LINK them, in the order a CMAKE_PREFIX_PATH wants.
  # Kept here so a consumer never has to know that the design system is a
  # separate prefix from main_ui.
  prefixes = [ mainUi designSystem ];

  # qmlimportscanner decides which static Qt QML plugins an app must link by
  # reading QML SOURCE. The archives carry compiled bytecode and no .qml, so
  # the scan has to be pointed at the trees they were compiled from -- without
  # this an app links no QtQuick plugin and every `import QtQuick` fails at
  # runtime with "module is not installed".
  qmlScanRoots = [ "${src}/src/Basecamp" "${designSystemSrc}/src/qml" ];
}
