# Inspector isolation guard for logos-basecamp.
#
# Launches two app instances at once through the test framework's own launch
# path and asserts each one owns the inspector its runner then talks to.
#
# Why this is a check of its own: nothing else can catch the defect. Every
# app-driving check here (integration-test, host-services-test, shutdown-test)
# is a single app in a single derivation, so each is green on its own — the
# defect only exists BETWEEN them, and nix realises them in parallel under
# max-jobs. Four checks were reported failing on the workspace pins for exactly
# this reason. See tests/inspector-isolation-tests.mjs for the full story.
#
# Cheap: no compile, no fixtures, two boots and a getTree each.
{ pkgs, src, appPkg, logosQtMcp, appBin ? "${appPkg}/bin/LogosBasecamp", timeoutSec ? 180 }:

pkgs.runCommand "logos-basecamp-inspector-isolation-test" {
  nativeBuildInputs = [ pkgs.coreutils pkgs.nodejs ]
    ++ pkgs.lib.optionals pkgs.stdenv.isLinux [
      pkgs.qt6.qtbase   # provides the offscreen platform plugin
      pkgs.libGL
      pkgs.libglvnd
    ];
} ''

  mkdir -p $out
  export LOGOS_USER_DIR="$out/app-data"
  mkdir -p "$LOGOS_USER_DIR"

  export QT_QPA_PLATFORM=offscreen
  export QT_FORCE_STDERR_LOGGING=1
  export QT_LOGGING_RULES="qt.*.debug=false;default.debug=true"

  # Deliberately NOT setting QML_INSPECTOR_PORT: an explicit port is an
  # instruction the framework honours verbatim, and pinning one here would make
  # this suite fail by construction. The point is what happens with no port
  # named at all -- which is how every other check runs.

  ${pkgs.lib.optionalString pkgs.stdenv.isLinux ''
    export QT_PLUGIN_PATH="${pkgs.qt6.qtbase}/${pkgs.qt6.qtbase.qtPluginPrefix}"
    export LD_LIBRARY_PATH="${pkgs.libGL}/lib:${pkgs.libglvnd}/lib''${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
  ''}

  export LOGOS_QT_MCP="${logosQtMcp}"

  LOG="$out/inspector-isolation-test.log"

  echo "Running logos-basecamp inspector isolation guard (timeout: ${toString timeoutSec}s)..."

  set +e
  timeout ${toString timeoutSec} \
    ${pkgs.nodejs}/bin/node ${src}/tests/inspector-isolation-tests.mjs ${appBin} \
    > "$LOG" 2>&1
  RUN_CODE=$?
  set -e

  cat "$LOG"

  if [ "$RUN_CODE" -eq 124 ]; then
    echo ""
    echo "FAIL: the guard did not finish within ${toString timeoutSec}s (timeout killed it)."
    exit 1
  fi
  if [ "$RUN_CODE" -ne 0 ]; then
    echo ""
    echo "FAIL: two apps launched side by side did not each get an inspector of"
    echo "      their own. Suites that run in parallel will drive each other's app."
    exit 1
  fi

  echo ""
  echo "Inspector isolation guard passed"
''
