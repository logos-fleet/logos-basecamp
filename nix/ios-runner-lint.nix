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

  # Everything but `frameworks` is scenery: the text under test does not care
  # what the toolchain file is called, only how many frameworks it loops over.
  fixture = name: frameworks: mkRunners {
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

  cases = lib.concatLists (lib.mapAttrsToList
    (name: frameworks:
      let runners = fixture name frameworks; in
      [
        { inherit frameworks; drv = runners.runSim; }
        { inherit frameworks; drv = runners.runDevice; }
      ])
    sizes);

  # Via mainProgram rather than by spelling `run-lint-<name>-ios-<kind>` out
  # again here: ios-runner.nix names its own runners, and one copy of that
  # convention is enough.
  scriptOf = c: lib.getExe c.drv;
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
    nativeBuildInputs = [ pkgs.bash ];
    passAsFile = [ "expected" ];
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

  echo "ios-runner-lint: ${toString (lib.length cases)} runner(s) lint clean, sizes ${
    lib.concatStringsSep ", " (lib.mapAttrsToList (_: fws: toString (lib.length fws)) sizes)
  }"
  touch $out
''
