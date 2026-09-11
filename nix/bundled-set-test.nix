# The catalog-driven Bundled-set build, over a LOCAL catalog of fixture
# packages (nix/bundled-set-fixture.nix), and over the same catalog PUBLISHED
# as a pinned release.
#
# The same pipeline over real iOS artifacts is `nix build
# .#packages.aarch64-ios-simulator.bundled-set`, which needs a Mac and a cross
# toolchain; this runs anywhere in seconds and is what a regression trips
# first.
#
# TWO CATALOGS, because a Bundled set can be handed its members two ways and
# only one of them was ever exercised:
#
#   local     `file` entries into a directory this build just produced. What
#             the mobile dev set uses.
#   pinned    `url` + `sha256` + `rootHash` out of a committed release index
#             (mobile/catalog/pinned-release/) -- bytes this build did NOT
#             produce, reached through a fixed-output fetch and re-checked
#             against the Merkle root the index pins. What a Store shell build
#             uses, and until now dead code.
{ pkgs, lgx, bundledSet, fixture, testKey, pinnedRelease }:

let
  inherit (pkgs) lib;

  inherit (fixture) target local;

  set = bundledSet.mkBundledSet {
    catalog = local;
    inherit target;
    apps = [ "counter_ui" ];
    pname = "fixture-bundled-set";
  };

  # ── the same set, out of a PINNED RELEASE ─────────────────────────────────
  # mobile/catalog/pinned-release/index.json is what nix-bundle-lgx's
  # `mkRelease` writes: the catalog index with every member's sha256 and Merkle
  # root filled in. It is COMMITTED, so the bytes here were produced by a
  # different build on a different day -- which is the only way to exercise the
  # path a Store shell build actually takes, where the catalog is a release
  # somebody else published.
  #
  # `file` becomes `url` at eval, because a committed index cannot carry an
  # absolute path into this checkout. That substitution is the only difference
  # from a real release: the fetch is still a fixed-output derivation keyed by
  # the index's sha256, its store-path name still carries the Merkle root, and
  # verify-lgx-member.sh still re-checks that root against the pin before
  # anything is unpacked. Bytes that do not hash to the pin cannot reach the
  # app, wherever they came from.
  pinnedIndex =
    let raw = builtins.fromJSON (builtins.readFile "${pinnedRelease}/index.json"); in
    raw // {
      packages = map (p: (builtins.removeAttrs p [ "file" ]) // {
        url = "file://${pinnedRelease}/${p.file}";
      }) raw.packages;
    };

  pinnedSet = bundledSet.mkBundledSet {
    catalog = { index = pinnedIndex; root = pinnedRelease; };
    inherit target;
    apps = [ "counter_ui" ];
    pname = "pinned-bundled-set";
  };

  # ── eval-time assertions ───────────────────────────────────────────────────
  pinnedIsPinned = lib.all (p:
    if (p.rootHash or "") == "" then throw "FAIL: pinned release entry '${p.name}' has no rootHash"
    else if (p.sha256 or "") == "" then throw "FAIL: pinned release entry '${p.name}' has no sha256"
    else true)
    pinnedIndex.packages;

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
assert pinnedIsPinned;
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
    pinnedSet = "${pinnedSet}";
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

  # ── the pinned release produces the same set ─────────────────────────────
  # Same closure, same load order, same recorded roots and signer -- reached
  # through fetchurl over committed bytes instead of a directory this build
  # made. That the derivation got this far is itself the assertion for the
  # fetch: a sha256 that did not match would have failed the fixed-output
  # derivation, and a rootHash that did not match would have failed admission.
  manifestOf() {
    python3 -c 'import json,os,sys; print(json.dumps(json.load(open(os.path.join(sys.argv[1],"bundled-set.json"))),indent=2,sort_keys=True))' "$1"
  }
  diff -u <(manifestOf "$set") <(manifestOf "$pinnedSet") \
    || fail "the pinned release resolves to a different set than the local catalog"
  diff -r "$set/Frameworks" "$pinnedSet/Frameworks" \
    || fail "the pinned release embeds different images than the local catalog"
  echo "pinned release: same set, fetched and root-checked"

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
