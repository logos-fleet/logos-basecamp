# The iOS runners, rendered over fixtures and linted -- for every Bundled-set
# size a `--bundle` can produce.
#
# WHY THIS IS A CHECK AND NOT A COMMENT. `nix/ios-runner.nix` builds its two
# runners with `writeShellApplication`, which runs shellcheck over the RENDERED
# text. So what the app is asked to carry decides whether the runner builds at
# all, and the failure is invisible to every other check in this repo: a
# Bundled set of ONE module made `for fw in one_word` a SC2043 warning,
# shellcheck failed it, and `--bundle <a single app>` -- the smallest thing a
# user can ask for -- could not build. Nothing here noticed, because nothing
# here renders a runner: the real ones live under the iOS cross package set and
# need a toolchain, an SDK and a Mac.
#
# These need none of that. Every argument ios-runner.nix takes is a plain
# string or a list of them, so a fixture is just different strings, and this
# check builds on the host -- Linux included.
{ pkgs }:

let
  inherit (pkgs) lib;

  mkRunners = import ./ios-runner.nix {
    inherit lib;
    inherit (pkgs) writeShellApplication;
  };

  # Everything but `frameworks` and `webAssetsPath` is scenery: the text under
  # test does not care what the toolchain file is called, only how many
  # frameworks it loops over and whether it ships a `web` half.
  fixture = name: frameworks: webAssetsPath: mkRunners {
    inherit webAssetsPath;
    pname = "lint-${name}";
    appName = "Lint";
    project = "LintIos";
    bundleId = "co.logos.lint";
    appSrc = "/fixture/app";
    appleSdk = "iphonesimulator";
    stagePath = "/fixture/stage";
    inherit frameworks;
    frameworkSrcs = map (f: "/fixture/set/${f}") frameworks;
    toolchainFile = "/fixture/toolchain.cmake";
    crossCmakeFlags = [ "-DFIXTURE_CROSS=1" ];
    symbolExportFlags = [ "-DLOGOS_IOS_EXPORTED_SYMBOLS=/fixture/exports.txt" ];
    prefixPath = "/fixture/stage";
    findRootPath = "/fixture/stage";
    versionGate = "# fixture: no Xcode version gate";
  };

  # `one` is the regression. `two` is what the repo's own mobile catalog
  # resolves to today, and `three` is a closure with a dependency in it -- the
  # shape AC 1 names.
  sizes = {
    one = [ "a_bare.framework" ];
    two = [ "a_bare.framework" "b_view.framework" ];
    three = [ "a_bare.framework" "b_view.framework" "c_bare.framework" ];
  };

  # BOTH SHAPES OF THE `web` HALF, and that is a second regression axis rather
  # than thoroughness: the assets path is known at EVAL, so a runtime `[ -n
  # "<literal>" ]` around the assertion is SC2157 and shellcheck refuses the
  # script. An app that ships no `web` assets (the Shell today) and one that
  # does (the smoke host) render different text, and only rendering both
  # catches the one that does not lint.
  webHalves = { none = ""; shipped = "/fixture/web-assets"; };

  # Every size against every `web` half, and both runners of each: the two
  # mapAttrsToList nest three levels of list, flattened here to the flat list of
  # cases the check below reads. `lib.flatten` does not descend into an
  # attribute set, so each case's own `frameworks` list survives it.
  cases = lib.flatten (lib.mapAttrsToList
    (name: frameworks:
      lib.mapAttrsToList
        (half: webAssetsPath:
          let runners = fixture "${name}-${half}" frameworks webAssetsPath; in
          [
            { inherit frameworks; drv = runners.runSim; }
            { inherit frameworks; drv = runners.runDevice; }
          ])
        webHalves)
    sizes);

  # Via mainProgram rather than by spelling `run-lint-<name>-ios-<kind>` out
  # again here: ios-runner.nix names its own runners, and one copy of that
  # convention is enough.
  scriptOf = c: lib.getExe c.drv;

  # The DEVICE runners alone, for the launch-retry case below: the simulator
  # half launches through simctl and has never shown the race.
  deviceCases = lib.flatten (lib.mapAttrsToList
    (name: frameworks:
      lib.mapAttrsToList
        (half: webAssetsPath:
          (fixture "${name}-${half}" frameworks webAssetsPath).runDevice)
        webHalves)
    sizes);
in
pkgs.runCommand "ios-runner-lint"
  {
    # Building this derivation is most of the check: each script's own build
    # ran shellcheck over it, and a warning there is an error.
    scripts = lib.concatStringsSep " " (map scriptOf cases);
    # Trailing newline on every line, not a separator between them: the reader
    # below is a `while read`, and a last line without one is a case silently
    # not checked.
    expected = lib.concatMapStrings
      (c: "${scriptOf c} ${lib.concatStringsSep " " c.frameworks}\n") cases;
    deviceScripts = lib.concatMapStrings (d: "${lib.getExe d}\n") deviceCases;
    nativeBuildInputs = [ pkgs.bash ];
    passAsFile = [ "expected" "deviceScripts" ];
  } ''
  set -euo pipefail

  while read -r script frameworks; do
    [ -n "$script" ] || continue
    echo "==> $(basename "$script")"

    # Syntax, independently of shellcheck.
    bash -n "$script"

    # The set reached the text, whole and in order. A runner that rendered an
    # empty array would shellcheck clean and embed nothing. Read back by
    # evaluating the declaration the script carries, so this compares the
    # frameworks the runner would actually iterate rather than re-deriving the
    # quoting nix wrote them with.
    unset bundled_frameworks bundled_framework_srcs
    eval "$(grep -m1 '^ *bundled_frameworks=(' "$script")"
    eval "$(grep -m1 '^ *bundled_framework_srcs=(' "$script")"
    got="''${bundled_frameworks[*]}"
    if [ "$got" != "$frameworks" ]; then
      echo "error: $script would embed [$got]," >&2
      echo "       and the fixture asked for [$frameworks]" >&2
      exit 1
    fi
    if [ "''${#bundled_framework_srcs[@]}" -ne "''${#bundled_frameworks[@]}" ]; then
      echo "error: $script copies ''${#bundled_framework_srcs[@]} director(ies)" >&2
      echo "       and embeds ''${#bundled_frameworks[@]} framework(s)" >&2
      exit 1
    fi

    # The rule the regression broke: a framework loop iterates the ARRAY. A
    # literal word list is what shellcheck refuses once the list is one word
    # long, and the one-word case is `--bundle <a single app>`.
    if grep -n 'for fw in ' "$script" | grep -v 'bundled_frameworks\[@\]'; then
      echo "error: $script loops over a literal framework list;" >&2
      echo "       with a one-module Bundled set that is SC2043." >&2
      exit 1
    fi
  done < "$expectedPath"

  # THE POST-INSTALL LAUNCH RETRY, RUN rather than read (issue #154).
  #
  # Under Xcode 27 the FIRST `devicectl device process launch` after an install
  # intermittently answers a freshly installed app with CoreDeviceError 10002 /
  # NSPOSIXErrorDomain 22, and the identical invocation succeeds a moment later.
  # Every flag the runner passes is still accepted by Xcode 27, so there is no
  # flag to assert; what a device run needs is to try again. That is behaviour,
  # not text, so it is exercised here against a stub `xcrun` -- the real one
  # needs a paired iPhone, a signing identity and a Mac, and this check has
  # none of the three.
  mkdir -p stub
  cat > stub/xcrun <<'STUB'
#!/usr/bin/env bash
# One byte per call, so the caller can count them without a shared shell.
printf 'x' >> "$STUB_COUNT"
n=$(wc -c < "$STUB_COUNT" | tr -d ' ')
if [ "$n" -le "$STUB_FAILURES" ]; then
  echo "ERROR: The application failed to launch. (com.apple.dt.CoreDeviceError error 10002 (0x2712))"
  echo "       The operation couldn't be completed. Invalid argument (NSPOSIXErrorDomain error 22 (0x16))"
  exit 1
fi
echo "Launched application with co.logos.lint bundle identifier."
echo "Waiting for the application to terminate…"
STUB
  chmod +x stub/xcrun
  export PATH="$PWD/stub:$PATH"

  # The function, lifted out of the rendered runner the same way the framework
  # arrays above are: what the script would really run, not a copy of it.
  call_launch_console() {
    bash -c '
      set -euo pipefail
      device=fixture-device
      bundle_id=co.logos.lint
      build_dir="$PWD/run"
      . ./launch_console.sh
      launch_console
    '
  }

  while read -r script; do
    [ -n "$script" ] || continue
    echo "==> launch retry: $(basename "$script")"

    sed -n '/launch_console() {/,/^ *}$/p' "$script" > launch_console.sh
    if ! grep -q 'devicectl device process launch' launch_console.sh; then
      echo "error: $script has no launch_console() that launches anything." >&2
      echo "       A single-shot launch ends a whole device run non-zero on the" >&2
      echo "       first post-install EINVAL from CoreDevice -- app installed," >&2
      echo "       app launchable, run reported as failed (issue #154)." >&2
      exit 1
    fi

    # A transient failure, then a success: the run ends with a launched app,
    # and it took more than one call to get there.
    rm -rf run; mkdir -p run; : > run/count
    if ! STUB_COUNT="$PWD/run/count" STUB_FAILURES=2 \
         LOGOS_IOS_LAUNCH_RETRY_DELAY=0 \
         call_launch_console > run/out 2>&1; then
      echo "error: launch_console gave up on a transient CoreDevice failure" >&2
      cat run/out >&2
      exit 1
    fi
    tries=$(wc -c < run/count | tr -d ' ')
    if [ "$tries" != 3 ]; then
      echo "error: launch_console called devicectl $tries time(s), wanted 3" >&2
      cat run/out >&2
      exit 1
    fi
    grep -q 'Launched application with' run/out || {
      echo "error: launch_console returned without the app's console output" >&2
      cat run/out >&2
      exit 1
    }

    # And it gives up: an app really can be unlaunchable, and a runner that
    # retries for ever is worse than one that fails.
    rm -rf run; mkdir -p run; : > run/count
    if STUB_COUNT="$PWD/run/count" STUB_FAILURES=99 \
       LOGOS_IOS_LAUNCH_RETRY_DELAY=0 LOGOS_IOS_LAUNCH_ATTEMPTS=3 \
       call_launch_console > run/out 2>&1; then
      echo "error: launch_console reported success though nothing launched" >&2
      cat run/out >&2
      exit 1
    fi
    tries=$(wc -c < run/count | tr -d ' ')
    if [ "$tries" != 3 ]; then
      echo "error: launch_console made $tries attempt(s), wanted 3" >&2
      exit 1
    fi
  done < "$deviceScriptsPath"

  echo "ios-runner-lint: ${toString (lib.length cases)} runner(s) lint clean, sizes ${
    lib.concatStringsSep ", " (lib.mapAttrsToList (_: fws: toString (lib.length fws)) sizes)
  }; ${toString (lib.length deviceCases)} device runner(s) retry a failed launch and give up"
  touch $out
''
