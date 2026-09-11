# The catalog-driven Bundled-set build, over a LOCAL catalog of fixture
# packages.
#
# Fixture payloads, not real cross-compiled modules, and deliberately so: what
# is under test here is the resolve / fetch / verify / extract / embed pipeline
# and its refusals, none of which can tell a framework binary from a text file.
# The same pipeline over real iOS artifacts is `nix build
# .#packages.aarch64-ios-simulator.bundled-set`, which needs a Mac and a cross
# toolchain; this runs anywhere in seconds and is what a regression trips first.
#
# The catalog is the closure the acceptance criterion names --
# counter_ui -> counter -> capability_module -- plus two members that exist to
# be refused: `stale_ui` reaches a dependency with no mobile variant, and
# `foreign_ui` is signed by a key the catalog does not list.
{ pkgs, lgx, bundledSet, catalog, testKey, icon }:

let
  inherit (pkgs) lib;

  target = "ios-sim-arm64";

  signingKey = { jwk = testKey.jwk; name = testKey.name; };

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
    counter = {
      name = "counter";
      version = "1.0.0";
      type = "core";
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

  set = bundledSet.mkBundledSet {
    catalog = local;
    inherit target;
    apps = [ "counter_ui" ];
    pname = "fixture-bundled-set";
  };

  # ── eval-time assertions ───────────────────────────────────────────────────
  closureNames = map (e: e.name) (bundledSet.resolveClosure {
    inherit (local) index;
    inherit target;
    apps = [ "counter_ui" ];
  });

  wantClosure = [ "capability_module" "counter" "counter_ui" ];

  closureOk =
    if closureNames == wantClosure then true
    else throw ("FAIL: closure of counter_ui on ${target} is "
      + lib.concatStringsSep ", " closureNames + "; expected "
      + lib.concatStringsSep ", " wantClosure);

  evalFails = what: expr:
    let attempt = builtins.tryEval (builtins.deepSeq expr "forced"); in
    if attempt.success then throw "FAIL: ${what} must not evaluate" else true;

  missingVariantRefused = evalFails
    "a Bundled set whose closure reaches a module with no ${target} variant"
    (bundledSet.mkBundledSet {
      catalog = local;
      inherit target;
      apps = [ "stale_ui" ];
    });

  unknownAppRefused = evalFails
    "a --bundle naming a module the catalog does not hold"
    (bundledSet.mkBundledSet {
      catalog = local;
      inherit target;
      apps = [ "counter_iu" ];
    });

  emptyBundleRefused = evalFails
    "an empty --bundle"
    (bundledSet.mkBundledSet { catalog = local; inherit target; apps = [ ]; });

  # ...and what the refusal SAYS. tryEval reports that an evaluation failed and
  # never why, so without this the three assertions above would still pass if
  # the message named the wrong module.
  refusalText = bundledSet.refusals.noVariant {
    entry = {
      name = "desktop_only";
      version = "2.1.0";
      variants = { darwin-arm64 = { }; linux-x86_64 = { }; };
    };
    via = [ "stale_ui" ];
    inherit target;
  };

  refusalNames = word:
    if lib.hasInfix word refusalText then true
    else throw "FAIL: the missing-variant refusal does not mention '${word}':\n${refusalText}";

in
assert closureOk;
assert missingVariantRefused;
assert unknownAppRefused;
assert emptyBundleRefused;
assert refusalNames "desktop_only";
assert refusalNames "darwin-arm64";
assert refusalNames "linux-x86_64";
assert refusalNames "stale_ui -> desktop_only";
pkgs.runCommand "bundled-set-tests"
  {
    nativeBuildInputs = [ lgx pkgs.python3 ];
    inherit target;
    set = "${set}";
    catalogRoot = "${local.root}";
    verifyScript = "${./verify-lgx-member.sh}";
    goodSigner = testKey.did;
  } ''
  set -euo pipefail
  fail() { echo "FAIL: $*" >&2; exit 1; }

  # ── the manifest is the resolved closure, in load order ──────────────────
  python3 - <<'PY'
  import json, os
  m = json.load(open(os.path.join(os.environ["set"], "bundled-set.json")))
  want = ["capability_module", "counter", "counter_ui"]
  got = [x["name"] for x in m["modules"]]
  assert got == want, "manifest lists %s, expected %s" % (got, want)
  assert m["target"] == os.environ["target"], m["target"]
  assert m["requested"] == ["counter_ui"], m["requested"]
  assert m["embedDir"] == "Frameworks", m["embedDir"]
  ui = [x for x in m["modules"] if x["name"] == "counter_ui"][0]
  assert ui["type"] == "ui_qml", ui
  assert ui["view"] == "qml/Main.qml", ui
  assert ui["dependencies"] == ["counter"], ui
  # Every member records WHAT WAS VERIFIED, not just what was asked for.
  for x in m["modules"]:
      assert len(x["rootHash"]) == 64, x
      assert x["signer"] == os.environ["goodSigner"], x
  print("manifest: %s" % ", ".join(got))
  PY

  # ── the Frameworks directory matches it, exactly ─────────────────────────
  # "Exactly" is the assertion: an app that carries a framework its manifest
  # does not list has a module the host can never load and the store review
  # can always find.
  ls "$set/Frameworks" | sort > got-fw.txt
  printf '%s\n' capability_module_bare.framework counter_bare.framework counter_ui_view.framework \
    | sort > want-fw.txt
  diff -u want-fw.txt got-fw.txt || fail "Frameworks/ does not match the resolved closure"
  for fw in $(cat want-fw.txt); do
    test -f "$set/Frameworks/$fw/''${fw%.framework}" || fail "$fw has no binary"
  done
  test -f "$set/bundled-set.json" || fail "no bundled-set.json in the set"
  echo "Frameworks/: $(tr '\n' ' ' < got-fw.txt)"

  # ── a tampered payload is refused at the fetch step ──────────────────────
  # The real admission path (nix/verify-lgx-member.sh), run over a package whose
  # payload was edited after signing. This is what stands between a catalog
  # mirror and the app image.
  refuse() {
    local what="$1"; shift
    if bash "$verifyScript" "$@" > refuse.log 2>&1; then
      cat refuse.log >&2
      fail "$what was ADMITTED into the Bundled set"
    fi
    echo "refused ($what): $(grep -m1 -iE 'error|mismatch|fail|not list|unsigned' refuse.log || true)"
  }

  cp "$catalogRoot/packages/counter.lgx" good.lgx
  chmod +w good.lgx

  python3 - <<'PY'
  import io, tarfile
  for src, dst, what in [("good.lgx", "tampered-payload.lgx", "payload"),
                         ("good.lgx", "tampered-manifest.lgx", "manifest")]:
      with tarfile.open(src, "r:gz") as tar:
          members = [(m, tar.extractfile(m).read() if m.isfile() else None)
                     for m in tar.getmembers()]
      out = []
      for m, d in members:
          if what == "payload" and m.name.startswith("variants/") and m.isfile():
              d = b"PWNED\n"; m.size = len(d)
          if what == "manifest" and m.name == "manifest.json":
              d = d.replace(b'"1.0.0"', b'"9.9.9"')
              m.size = len(d)
          out.append((m, d))
      with tarfile.open(dst, "w:gz", format=tarfile.GNU_FORMAT) as tar:
          for m, d in out:
              tar.addfile(m) if d is None else tar.addfile(m, io.BytesIO(d))
  PY

  bash "$verifyScript" good.lgx "$target" - ok-out "$goodSigner" \
    || fail "the untampered package was refused; the negative tests below prove nothing"
  echo "admitted: the untampered package"

  refuse "an edited payload"  tampered-payload.lgx  "$target" - out1 "$goodSigner"
  refuse "an edited manifest" tampered-manifest.lgx "$target" - out2 "$goodSigner"
  refuse "a signer the catalog does not list" good.lgx "$target" - out3 \
    "did:jwk:eyJjcnYiOiJFZDI1NTE5Iiwia3R5IjoiT0tQIiwieCI6Im5vdC10aGUtc2lnbmVyIn0"
  refuse "a Merkle root the catalog did not pin" good.lgx "$target" \
    0000000000000000000000000000000000000000000000000000000000000000 out4 "$goodSigner"

  mkdir -p $out
  cp "$set/bundled-set.json" $out/
  echo "PASS"
''
