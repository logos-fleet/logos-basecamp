# The two runners over one iOS app: configure it with Xcode, build it, check
# the result, and put it on a simulator or a device.
#
# A FUNCTION OF PLAIN VALUES, not of the derivations they came from. Every
# store path the script needs arrives as a string and every list as a list of
# strings, so the same text can be rendered over fixtures by a check that owns
# no cross toolchain (nix/ios-runner-lint.nix). That matters because these are
# `writeShellApplication`s: shellcheck runs over the RENDERED text, so what a
# runner is asked to carry changes whether it builds at all. A one-module
# Bundled set is the case that proves it -- `for fw in one_word` is SC2043, and
# the smallest set a user can name was the one that could not build.
{ lib, writeShellApplication }:

{ pname                # "basecamp-shell" -- names the runners and the build dir
, appName              # the Xcode target, and <appName>.app
, project              # <project>.xcodeproj
, bundleId
, appSrc               # the CMake source directory of the .app
, appleSdk             # "iphonesimulator" | "iphoneos"
, stagePath            # the static stage the app links; keys the build dir
, frameworks           # [ "bare_counter_bare.framework" ... ] -- base names
, frameworkSrcs        # the directories those are copied FROM, in the same order
, toolchainFile
, crossCmakeFlags      # the cross toolchain's own flags
, symbolExportFlags    # logos_ios_export_symbols()'s two flags, from the stage
, webAssetsPath ? ""  # nix/mobile-web-assets.nix's output, copied into <App>.app
, configureFlags ? [ ] # per-app extras
, prefixPath           # CMAKE_PREFIX_PATH, already ;-joined
, findRootPath         # CMAKE_FIND_ROOT_PATH, already ;-joined
, versionGate          # shell snippet that refuses too old a host Xcode
, runtimeInputs ? [ ]
}:

let
  buildApp = ''
    ${versionGate}

    app_src=${appSrc}
    bundle_id=${bundleId}
    build_dir="''${LOGOS_IOS_BUILD_DIR:-''${TMPDIR:-/tmp}/${pname}/$(basename ${stagePath})}"
    app="$build_dir/Debug-${appleSdk}/${appName}.app"

    # The Bundled set, resolved at EVAL by mkBundledSet and carried here as two
    # parallel ARRAYS: where each framework is copied from, and what it is
    # called once embedded. Arrays rather than literal word lists because a
    # Bundled set of one is a legitimate `--bundle`, and a `for` over a single
    # literal word is SC2043 -- which fails this very script's shellcheck.
    bundled_framework_srcs=(${lib.escapeShellArgs frameworkSrcs})
    bundled_frameworks=(${lib.escapeShellArgs frameworks})

    # Xcode's copy phase signs the framework IN PLACE after copying it, and a
    # store path is read-only all the way down -- ditto preserves that, so
    # codesign would fail on the copy. Stage a writable one first.
    stage_frameworks() {
      rm -rf "$build_dir/embed"
      mkdir -p "$build_dir/embed"
      cp -R "''${bundled_framework_srcs[@]}" "$build_dir/embed/"
      chmod -R u+w "$build_dir/embed"
      embed_fws=""
      for fw in "''${bundled_frameworks[@]}"; do
        embed_fws="''${embed_fws:+$embed_fws;}$build_dir/embed/$fw"
        echo "==> bundled: $fw"
      done
    }

    configure_app() {
      mkdir -p "$build_dir"
      stage_frameworks
      echo "==> configure ($build_dir)"
      cmake -S "$app_src" -B "$build_dir" -G Xcode \
        -DCMAKE_TOOLCHAIN_FILE=${toolchainFile} \
        ${lib.escapeShellArgs crossCmakeFlags} \
        ${lib.escapeShellArgs symbolExportFlags} \
        "-DLOGOS_IOS_BUNDLED_FRAMEWORKS=$embed_fws" \
        "-DLOGOS_IOS_WEB_ASSETS=${webAssetsPath}" \
        "-DLOGOS_IOS_APP_BUNDLE_DIR=$app" \
        "-DCMAKE_PREFIX_PATH=${prefixPath}" \
        "-DCMAKE_FIND_ROOT_PATH=${findRootPath}" \
        ${lib.escapeShellArgs configureFlags} \
        "$@"
    }

    xcodebuild_app() {
      echo "==> xcodebuild (${appleSdk}; full log: $build_dir/xcodebuild.log)"
      rm -rf "$app"
      set +e
      xcodebuild -project "$build_dir/${project}.xcodeproj" -target ${appName} \
        -configuration Debug -sdk ${appleSdk} -arch arm64 "$@" \
        build 2>&1 | tee "$build_dir/xcodebuild.log" | grep -E '^\*\*|error:|warning: .*ld'
      xcode_status=''${PIPESTATUS[0]}
      set -e
      if [ "$xcode_status" -ne 0 ]; then
        echo "xcodebuild failed; see $build_dir/xcodebuild.log" >&2
        exit 1
      fi
      [ -d "$app" ] || { echo "xcodebuild produced no $app" >&2; exit 1; }
      echo "==> app: $app ($(du -sk "$app" | cut -f1) KB; executable $(stat -f %z "$app/${appName}") bytes)"
      # ADR 0002's rule for anything that leaves the machine: a store path in a
      # shipped binary is a path that does not exist on the device.
      if strings "$app/${appName}" | grep -q /nix/store; then
        echo "error: /nix/store reference in the app executable" >&2
        strings "$app/${appName}" | grep -m5 /nix/store >&2
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
      printf '%s\n' "''${bundled_frameworks[@]}" \
        | sort > "$build_dir/want-frameworks.txt"
      if ! diff -u "$build_dir/want-frameworks.txt" "$build_dir/got-frameworks.txt"; then
        echo "error: <App>.app/Frameworks does not match the Bundled set" >&2
        exit 1
      fi
      for fw in "''${bundled_frameworks[@]}"; do
        bin="$app/Frameworks/$fw/''${fw%.framework}"
        [ -f "$bin" ] || { echo "error: $fw has no binary at $bin" >&2; exit 1; }
      done
      echo "==> embedded, and matching bundled-set.json: $(tr '\n' ' ' < "$build_dir/got-frameworks.txt")"

      # THE `web` HALF, asserted for the same reason as the Frameworks list: a
      # copy phase that silently did nothing leaves an app that starts, opens a
      # page and only then says the runtime is missing -- three steps from the
      # cause. Both names are what iosQmlRuntimeDir() and iosWebModulesDir()
      # look for, so a rename on either side fails here rather than on a device.
      #
      # RENDERED AWAY rather than tested at run time when this app ships none:
      # the path is known at eval, so `[ -n "<a literal>" ]` is SC2157 and
      # writeShellApplication refuses the script (nix/ios-runner-lint.nix
      # renders both shapes for exactly this reason).${lib.optionalString (webAssetsPath != "") ''

      for want in logos-runtime/logos_qml_runtime.js web-modules; do
        [ -e "$app/$want" ] || {
          echo "error: <App>.app/$want is missing; the app ships no $want" >&2
          exit 1
        }
      done
      echo "==> web assets: $(du -sk "$app/logos-runtime" | cut -f1) KB runtime, modules: $(find "$app/web-modules" -mindepth 1 -maxdepth 1 -exec basename {} \; | sort | tr '\n' ' ')"''}
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
        for fw in "''${bundled_frameworks[@]}"; do
          codesign -dv "$app/Frameworks/$fw" 2>&1 \
            | grep -E 'Identifier|TeamIdentifier|Signature' || true
        done
        echo "==> codesign --verify --deep: ok (app and every nested bundle)"
      else
        echo "==> unsigned simulator build; codesign --verify --deep is not meaningful here"
      fi
    }
  '';

  runSim = writeShellApplication {
    name = "run-${pname}-ios-sim";
    inherit runtimeInputs;
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

  runDevice = writeShellApplication {
    name = "run-${pname}-ios-device";
    inherit runtimeInputs;
    text = ''
      device="''${LOGOS_IOS_DEVICE:-}"
      team="''${LOGOS_IOS_TEAM_ID:-}"
      [ -n "$team" ] || { echo "LOGOS_IOS_TEAM_ID is unset" >&2; exit 1; }
      [ -n "$device" ] || { echo "LOGOS_IOS_DEVICE is unset (xcrun devicectl list devices)" >&2; exit 1; }
      ${buildApp}

      # THE FIRST LAUNCH AFTER AN INSTALL IS NOT RELIABLE (issue #154).
      #
      # Under Xcode 27 `devicectl device process launch` intermittently answers
      # a just-installed app with
      #   CoreDeviceError 10002 / NSPOSIXErrorDomain 22 (Invalid argument)
      # and the identical invocation succeeds a second later -- measured on the
      # venue's iPad Air, where one run failed at install+0 and the very next
      # attempt launched and streamed the console. It is a race in CoreDevice's
      # post-install bookkeeping, not a renamed option: `--activate`,
      # `--console` and `--terminate-existing` are all still in Xcode 27's
      # `devicectl device process launch --help`, and all three together launch
      # that app when it is not fresh out of an install.
      #
      # RETRIED ON "DID NOT LAUNCH", NEVER ON A NON-ZERO EXIT. With --console
      # devicectl waits for the app and exits with what the app made of itself,
      # so retrying a failed exit would start a second copy of an app that ran.
      # "Launched application with" is devicectl's own line, printed before it
      # starts waiting, and it is the only thing that separates the two.
      launch_console() {
        local attempt attempts delay status
        attempts=''${LOGOS_IOS_LAUNCH_ATTEMPTS:-5}
        delay=''${LOGOS_IOS_LAUNCH_RETRY_DELAY:-3}
        attempt=1
        while [ "$attempt" -le "$attempts" ]; do
          set +e
          xcrun devicectl device process launch --activate --console --terminate-existing \
            --device "$device" "$bundle_id" "$@" 2>&1 | tee "$build_dir/launch.log"
          status=''${PIPESTATUS[0]}
          set -e
          if grep -q 'Launched application with' "$build_dir/launch.log"; then
            return "$status"
          fi
          # Silent on the last attempt: announcing a retry that is not coming,
          # and then sleeping before the error below, is two ways of saying
          # something untrue about what the runner is about to do.
          if [ "$attempt" -lt "$attempts" ]; then
            echo "==> attempt $attempt/$attempts did not start $bundle_id; retrying in ''${delay}s" >&2
            sleep "$delay"
          fi
          attempt=$(( attempt + 1 ))
        done
        echo "error: $bundle_id did not launch in $attempts attempt(s)." >&2
        echo "       It IS installed on $device. To start it without a console:" >&2
        echo "       xcrun devicectl device process launch --device $device $bundle_id" >&2
        return 1
      }

      configure_app "-DLOGOS_IOS_DEVELOPMENT_TEAM=$team"
      LOGOS_IOS_VERIFY_SIGNATURE=1 xcodebuild_app -allowProvisioningUpdates
      echo "==> install + launch on $device (console attached; log: $build_dir/launch.log)"
      xcrun devicectl device install app --device "$device" "$app"
      launch_console "$@"
    '';
  };
in
{ inherit runSim runDevice; }
