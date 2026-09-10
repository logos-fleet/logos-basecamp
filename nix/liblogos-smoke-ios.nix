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
}:

let
  inherit (pkgs) lib;
  buildPkgs = pkgs.pkgsBuildBuild;
  appleSdk = pkgs.qt6.qtbase.appleSdk;
  bundleId = "co.logos.liblogos.smoke";

  libRoots = chain.all ++ [
    pkgs.boost
    pkgs.openssl
    pkgs.spdlog
  ];
  includeRoots = libRoots ++ [ pkgs.nlohmann_json ];
  joined = l: lib.concatStringsSep ";" (map toString l);

  stage = pkgs.mkIosCmakeStage {
    pname = "liblogos-smoke-host-ios";
    version = "0.1.0";
    inherit src;
    sourceDir = "mobile/liblogos-smoke/stage";
    buildInputs = libRoots;
    cmakeFlags = [
      "-DCMAKE_FIND_ROOT_PATH=${joined libRoots}"
      "-DLOGOS_IOS_LIB_ROOTS=${joined libRoots}"
      "-DLOGOS_IOS_INCLUDE_ROOTS=${joined includeRoots}"
    ];
  };

  # Shared by both runners. The build dir is keyed on the stage's store path,
  # so a rebuilt stage never reuses an Xcode cache from the previous one.
  buildApp = ''
    ${pkgs.xcodeWrapper.versionGate}

    app_src=${src}/mobile/liblogos-smoke/app
    bundle_id=${bundleId}
    build_dir="''${LOGOS_IOS_SMOKE_BUILD_DIR:-''${TMPDIR:-/tmp}/liblogos-smoke-ios/$(basename ${stage})}"
    app="$build_dir/Debug-${appleSdk}/LiblogosSmoke.app"

    configure_app() {
      mkdir -p "$build_dir"
      echo "==> configure ($build_dir)"
      cmake -S "$app_src" -B "$build_dir" -G Xcode \
        -DCMAKE_TOOLCHAIN_FILE=${pkgs.logosQtCrossToolchainFile} \
        ${lib.escapeShellArgs pkgs.logosQtCrossCmakeFlags} \
        "-DCMAKE_PREFIX_PATH=${stage}" \
        "-DCMAKE_FIND_ROOT_PATH=${stage};${joined libRoots}" \
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
      xcodebuild_app -allowProvisioningUpdates
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
