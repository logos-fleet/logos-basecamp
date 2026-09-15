# The fixture catalog the Bundled-set tests resolve against.
#
# Split out of nix/bundled-set-test.nix because TWO things need it and they
# must be the same catalog: the test, and `nix build .#bundled-set-release`,
# which publishes it as a PINNED release whose index is then committed under
# mobile/catalog/pinned-release/. A release generated from a second, parallel
# set of fixtures would pin bytes nothing tests -- so flake.nix imports this
# file ONCE and hands the result to both.
#
# Fixture payloads, not real cross-compiled modules, and deliberately so: what
# is under test is the resolve / fetch / verify / extract / embed pipeline and
# its refusals, none of which can tell a framework binary from a text file.
#
# The catalog is the closure the acceptance criterion names --
# counter_ui -> counter -> capability_module -- plus two members that exist to
# be refused: `stale_ui` reaches a dependency with no mobile variant, and
# `desktop_only` is the dependency it reaches.
{ pkgs, catalog, testKey, icon }:

let
  inherit (pkgs) lib;

  signingKey = { inherit (testKey) jwk name; };

  target = "ios-sim-arm64";

  # A fixture variant payload, laid out the way the target's loader wants it:
  # an embedded framework bundle on iOS, a shared object on Android. The BYTES
  # are a stand-in; the LAYOUT is the contract the Bundled set copies verbatim.
  iosVariant = { stem, view ? null }: {
    main = "Frameworks/${stem}.framework/${stem}";
    payload = pkgs.runCommand "${stem}-ios-payload" { } (''
      mkdir -p $out/Frameworks/${stem}.framework
      printf 'fixture image for %s\n' ${stem} > $out/Frameworks/${stem}.framework/${stem}
      printf '<plist><dict><key>CFBundleExecutable</key><string>%s</string></dict></plist>\n' \
        ${stem} > $out/Frameworks/${stem}.framework/Info.plist
    '' + lib.optionalString (view != null) ''
      # `lgx sign` refuses a ui_qml package whose declared `view` is not a file
      # in the variant, so a view module's variant carries its QML entry point
      # beside the framework.
      mkdir -p "$out/$(dirname ${view})"
      printf 'import QtQuick\nItem {}\n' > $out/${view}
    '');
  };

  androidVariant = stem: {
    main = "lib/lib${stem}.so";
    payload = pkgs.runCommand "${stem}-android-payload" { } ''
      mkdir -p $out/lib
      printf 'fixture image for %s\n' ${stem} > $out/lib/lib${stem}.so
    '';
  };

  darwinVariant = stem: {
    main = "lib/lib${stem}.dylib";
    payload = pkgs.runCommand "${stem}-darwin-payload" { } ''
      mkdir -p $out/lib
      printf 'fixture image for %s\n' ${stem} > $out/lib/lib${stem}.dylib
    '';
  };

  specs = {
    capability_module = {
      name = "capability_module";
      version = "1.0.0";
      type = "core";
      dependencies = [ ];
      variants = {
        ios-sim-arm64 = iosVariant { stem = "capability_module_bare"; };
        ios-arm64 = iosVariant { stem = "capability_module_bare"; };
        android-arm64 = androidVariant "capability_module_bare";
      };
      inherit signingKey;
    };
    # A PLATFORM MODULE THIS SET SHIPS (ADR 0009). `platform: true` is the
    # module's own declaration in metadata.json -- it owns access a webview
    # cannot give it -- and the catalog carries it so a consumer can derive a
    # shell's floor from the index. `counter` is the one in the closure, which
    # is what makes the derived floor's `present` half observable at all.
    counter = {
      name = "counter";
      version = "1.0.0";
      type = "core";
      platform = true;
      dependencies = [ "capability_module" ];
      variants = {
        ios-sim-arm64 = iosVariant { stem = "counter_bare"; };
        ios-arm64 = iosVariant { stem = "counter_bare"; };
      };
      inherit signingKey;
    };
    counter_ui = {
      name = "counter_ui";
      version = "1.0.0";
      type = "ui_qml";
      view = "qml/Main.qml";
      inherit icon;
      dependencies = [ "counter" ];
      variants = { ios-sim-arm64 = iosVariant { stem = "counter_ui_view"; view = "qml/Main.qml"; }; };
      inherit signingKey;
    };
    # Pulls in a member that ships desktop variants only. THE point of this
    # fixture: the refusal must name `desktop_only` and the variants it does
    # ship, not the app the user asked for.
    desktop_only = {
      name = "desktop_only";
      version = "2.1.0";
      type = "core";
      # ...AND A PLATFORM MODULE THIS SET DOES NOT SHIP, which is the other half
      # of the floor: a Downloaded module that reached it would install and then
      # have nothing to call (#169).
      platform = true;
      dependencies = [ ];
      variants = {
        darwin-arm64 = darwinVariant "desktop_only";
        linux-x86_64 = darwinVariant "desktop_only";
      };
      inherit signingKey;
    };
    stale_ui = {
      name = "stale_ui";
      version = "1.0.0";
      type = "core";
      dependencies = [ "desktop_only" ];
      variants = { ios-sim-arm64 = iosVariant { stem = "stale_ui_bare"; }; };
      inherit signingKey;
    };
  };

  drvs = lib.mapAttrs (_: catalog.mkPackage) specs;

  local = catalog.mkCatalog {
    release = "fixture";
    signers = [ testKey.did ];
    packages = lib.mapAttrsToList (n: spec: { inherit spec; drv = drvs.${n}; }) specs;
  };

in
{
  inherit target specs drvs local;
}
