# The Bundled set: the modules a Store shell ships INSIDE its app image.
#
# ADR 0007 says a Store shell's native mobile modules are pulled from the SAME
# catalog the Web container installs from, but at BUILD time rather than at
# runtime -- App Store guideline 2.5.2 and Play's .so rule leave no other way to
# put native code on a phone. So this file is the build-time half of an
# installer:
#
#     name list  ->  closure  ->  fetch  ->  verify  ->  extract  ->  embed
#
# and every step is the one `lgpm install` would run, moved to the builder.
#
# WHAT IS PURE AND WHAT IS NOT. The closure and the variant check happen at
# EVAL, off the catalog index alone, because that is the only place an error can
# still name the module a user asked for. By the time a cross toolchain is
# running, "counter_ui needs counter, and counter ships no ios-sim-arm64" has
# decayed into a missing file. Everything after that -- signature, Merkle root,
# extraction -- is a derivation, because it is about bytes.
#
# THE CATALOG IS DATA, NOT A FLAKE INPUT. `index` is a plain attribute set with
# the shape below; `root` is the directory the `file` fields are relative to. A
# pinned release is `readCatalog <dir>`; a locally built one hands its index
# over directly (nix-bundle-lgx's `lib.<system>.mkMobileCatalog`, which is the
# publish half of this file), and that is what keeps a Bundled-set build free
# of import-from-derivation.
#
#   { catalogVersion = "1";
#     release        = "<tag>";          # informational
#     signers        = [ "did:jwk:..." ];# the ONLY DIDs a member may be signed by
#     packages = [ {
#       name         = "counter_ui";
#       version      = "1.0.0";
#       type         = "ui_qml";         # core | ui_qml
#       platform     = false;            # ADR 0009: owns access a webview cannot
#                                        # give it, so a Downloaded module may
#                                        # depend on it only where it is bundled
#       dependencies = [ "counter" ];    # strings, or { name = ...; } entries
#       variants     = { ios-sim-arm64 = { main = "..."; view = "..."; }; ... };
#       file         = "packages/counter_ui.lgx";   # relative to `root`, OR
#       url          = "https://.../counter_ui.lgx";
#       sha256       = "sha256-...";     # required with `url`: the FOD's key
#       rootHash     = "<64 hex>";       # optional Merkle pin, enforced when set
#     } ]; }
{ pkgs, lgx, publisher }:

let
  inherit (pkgs) lib;

  # The variant vocabulary and the payload layout come from the PUBLISHER
  # (nix-bundle-lgx's lib.<system>.mkMobileCatalog), not from a second copy
  # here. A consumer that restated them would be free to drift: a set embedded
  # under `lib/` out of a payload laid out for `Frameworks/` is a build that
  # succeeds and an app whose loader finds nothing.
  #
  #   variantForSystem   aarch64-ios-simulator -> ios-sim-arm64, ...
  #   embedDirFor        the ONE directory this platform's loader opens from
  inherit (publisher) variantForSystem systemForVariant embedDirFor;

  verifyMemberScript = ./verify-lgx-member.sh;

  # THE PLATFORM FLOOR (#169), derived from this shell's own catalog and its own
  # closure. Kept in its own file because the runtime half reads the answer back
  # out of the manifest written below -- see nix/platform-floor.nix.
  platformFloor = import ./platform-floor.nix { inherit lib; };

  # The two spellings a catalog index allows, read in ONE place (above).
  inherit (platformFloor) depNamesOf;

  # A published catalog is a directory with an index.json beside the packages.
  # Reading a DERIVATION this way is import-from-derivation; reading a checked-in
  # or fetched directory is not.
  readCatalog = dir: {
    index = builtins.fromJSON (builtins.readFile "${dir}/index.json");
    root = dir;
  };

  # ── the refusals, as text ──────────────────────────────────────────────────
  # Built here rather than inline in the throw so a test can assert what they
  # SAY. `builtins.tryEval` reports that an evaluation failed and never why, so
  # an eval-failure test alone cannot tell "counter ships no ios-sim-arm64" from
  # a typo in the thrower.
  refusals = {
    missing = { release, catalogNames, name, via, webModules ? [ ] }: ''
      logos-basecamp: no module '${name}' in catalog '${release}'.
      ${if via == [ ] then "It was named on --bundle." else "It is a dependency of ${lib.concatStringsSep " -> " via}."}
      The catalog holds: ${lib.concatStringsSep ", " catalogNames}
      ${if webModules == [ ]
        then "This build ships no `web` modules, so there was no second half to look in."
        else "The app image's `web` half ships: ${lib.concatStringsSep ", " webModules}"}
    '';

    noVariant = { entry, via, target }: ''
      logos-basecamp: the Bundled set for '${target}' cannot be built.

        module      ${entry.name} ${entry.version}
        reached by  ${lib.concatStringsSep " -> " (via ++ [ entry.name ])}
        ships       ${
          let vs = lib.attrNames (entry.variants or { });
          in if vs == [ ] then "no variants at all" else lib.concatStringsSep ", " vs
        }

      A Bundled set is assembled at build time and shipped inside the app
      (ADR 0007), so a member that does not ship '${target}' cannot be filled in
      on the device later. Either publish a '${target}' variant of ${entry.name},
      or drop whatever pulls it in from --bundle.
    '';

    cycle = { release, name, via }: ''
      logos-basecamp: dependency cycle in catalog '${release}': ${lib.concatStringsSep " -> " (via ++ [ name ])}
    '';
  };

  # ── closure ────────────────────────────────────────────────────────────────
  # Post-order depth-first, so the result is in load order: a module appears
  # after everything it depends on. `via` is the path that reached a member, and
  # it is carried for one reason only -- an error names the chain rather than the
  # leaf, which is the difference between "counter ships no ios-sim-arm64" and
  # "you asked for counter_ui; it needs counter; counter ships no
  # ios-sim-arm64".
  #
  # THE APP IMAGE HAS TWO HALVES, AND ONLY ONE OF THEM IS THIS CATALOG (#183).
  #
  # Beside the Bundled set an app carries `web` modules -- wasm and JavaScript
  # laid out as lgpm installs them (nix/mobile-web-assets.nix), discovered by the
  # core and run in the Web container. A page cannot be registered on the host's
  # provider registry, so the container registers a WebModuleGlue instead: an
  # ordinary provider object that relays to the page. Every consumer -- another
  # module included -- reaches a `web` module exactly as it reaches a subprocess
  # one, and nothing above the container learns it is JavaScript.
  #
  # So a Bundled member CAN depend on one, and `keystore_module` is the case
  # that forced the question: the phone's keystore is a `web` variant with an
  # idbfs vault (#147), while `wallet_backend_module` and `railgun_module` are
  # native and name it in `dependencies`. Resolving only the catalog refused
  # both by name for a module that was running on the device.
  #
  # `webModules` is what THIS build ships in that other half. A dependency found
  # there is SATISFIED and is NOT a member: it is not fetched, verified or
  # embedded here, because the web half is a different stage of the same image.
  # It is recorded instead -- `webSatisfied` in bundled-set.json -- because the
  # device reads that manifest and nothing else, and a set that is complete only
  # alongside those assets has to say so.
  #
  # WHAT THIS DOES NOT DO: walk the web module's own dependencies. They are not
  # in this index (it is not a catalog member), and the core resolves them on
  # the device from the installed layout. The ADR 0007 guarantee is unchanged
  # for everything this file does place in the image -- a Bundled member is
  # still never filled in later -- and a `web` module is in the image too; what
  # is not checked here is the half that the core, not the builder, resolves.
  #
  # A name in BOTH halves is a Bundled member: the loader brings that one up,
  # and a rule that preferred the page would silently move a dependency.
  # A name in the web half ALONE cannot be named on --bundle: `--bundle` names
  # members of the Bundled set, and a `web` module has no variant to embed.
  resolveSet = { index, target, apps, webModules ? [ ] }:
    let
      byName = lib.listToAttrs (map (p: { name = p.name; value = p; }) index.packages);
      catalogNames = lib.attrNames byName;
      release = index.release or "<unnamed>";

      step = acc: { name, via }:
        if lib.elem name acc.order then acc
        # The other half, and only for a DEPENDENCY (`via != [ ]`, so never a
        # --bundle name) this catalog does not hold (so a name in both halves
        # stays a member). Recorded, and not walked: see the note above.
        else if via != [ ] && !(byName ? ${name}) && lib.elem name webModules
        then acc // { webSatisfied = lib.unique (acc.webSatisfied ++ [ name ]); }
        else if lib.elem name via
        then throw (refusals.cycle { inherit release name via; })
        else
          let
            entry = byName.${name}
              or (throw (refusals.missing { inherit release catalogNames name via webModules; }));
            ok = if (entry.variants or { }) ? ${target} then true
                 else throw (refusals.noVariant { inherit entry via target; });
            via' = via ++ [ name ];
            withDeps = lib.foldl' step acc (map (d: { name = d; via = via'; }) (depNamesOf entry));
          in
          assert ok;
          withDeps // { order = withDeps.order ++ [ name ]; };

      resolved = lib.foldl' step { order = [ ]; webSatisfied = [ ]; }
        (map (a: { name = a; via = [ ]; }) apps);
    in
    {
      members = map (name: byName.${name}) resolved.order;
      inherit (resolved) webSatisfied;
    };

  # The members alone: the closure a caller wants when it is not building an
  # image and so has no web half to resolve against -- a Platform floor out of a
  # catalog (nix/platform-floor.nix takes exactly this list), or a test.
  resolveClosure = args: (resolveSet args).members;

  # ── fetch + verify ─────────────────────────────────────────────────────────
  # One derivation per member, and it is the ONLY place a `.lgx` is opened.
  #
  # With `url` the source is a fixed-output derivation whose store-path name
  # carries the catalog's Merkle root and whose output hash is the catalog's
  # sha256 over the archive bytes -- so a re-download or a substitution is the
  # package the catalog named, or the build stops before anything unpacks it.
  # With `file` (a local catalog, which is how the tests and the mobile dev set
  # run) there is nothing to fetch and the same checks run over bytes already in
  # the store.
  #
  # The checks themselves are nix/verify-lgx-member.sh -- a separate file so the
  # negative test can run the real admission path over a tampered package.
  verifyPackage = { root, signers, target }: entry:
    let
      pinned = entry.rootHash or null;
      shortRoot = if pinned == null then "unpinned" else builtins.substring 0 16 pinned;

      source =
        if entry ? url then
          pkgs.fetchurl {
            inherit (entry) url;
            hash = entry.sha256 or (throw
              "logos-basecamp: catalog entry '${entry.name}' has a url and no sha256; a fetch with no pin is not a fetch this build can trust");
            name = "${entry.name}-${entry.version}-${shortRoot}.lgx";
          }
        else if entry ? file then "${root}/${entry.file}"
        else throw "logos-basecamp: catalog entry '${entry.name}' has neither `file` nor `url`";
    in
    pkgs.runCommand "${entry.name}-${entry.version}-${target}-verified"
      {
        nativeBuildInputs = [ lgx pkgs.python3 ];
        passthru = { inherit entry; };
      } ''
      set -euo pipefail
      echo "==> admitting ${entry.name} ${entry.version} into the ${target} set"
      bash ${verifyMemberScript} ${source} ${target} \
        ${if pinned == null then "-" else pinned} $out \
        ${lib.escapeShellArgs signers}
    '';

  # ── the set ────────────────────────────────────────────────────────────────
  # The images land in the ONE directory their platform will dlopen from, and
  # bundled-set.json lands beside them. The host reads that manifest and nothing
  # else: it is the only record of what was bundled, because the flat,
  # read-only Frameworks/ and nativeLibraryDir carry no package structure.
  mkBundledSet =
    { catalog          # { index; root; } -- see readCatalog / nix/catalog.nix
    , target           # an LGX variant name, e.g. "ios-sim-arm64"
    , apps             # module names, the way a user spelled them on --bundle
    , webModules ? [ ] # the `web` modules the SAME app image ships (#183)
    , pname ? "bundled-set"
    }:
    let
      index = catalog.index;
      signers = index.signers or (throw
        "logos-basecamp: catalog '${index.release or "<unnamed>"}' declares no `signers`; there is then no key a member could be checked against");
      resolved = resolveSet { inherit index target apps webModules; };
      closure = resolved.members;
      verified = map (verifyPackage { inherit (catalog) root; inherit signers target; }) closure;
      embedDir = embedDirFor target;

      # What this shell's Bundled closure makes possible for a DOWNLOADED module
      # (ADR 0009, #169): the Platform modules it ships, and the ones this
      # catalog knows about that it does not. Derived here because this is the
      # one place both facts are known at once, and written into the manifest
      # because the device -- browsing a repository this build never saw -- is
      # the only place the verdict can be applied.
      floor = platformFloor.floorOf { inherit index closure; };
    in
    assert lib.assertMsg (apps != [ ])
      "logos-basecamp: an empty --bundle produces an app with no Bundled set; name at least one module";
    pkgs.runCommand "${pname}-${target}"
      {
        nativeBuildInputs = [ pkgs.python3 ];
        members = lib.concatStringsSep " " (map toString verified);
        requested = lib.concatStringsSep " " apps;
        inherit target embedDir;
        webSatisfied = lib.concatStringsSep " " resolved.webSatisfied;
        platformFloorJson = builtins.toJSON floor;
        passthru = {
          inherit target embedDir floor;
          # The dependencies this set did NOT bundle because the app image's
          # `web` half carries them (#183). Read by the caller that builds that
          # half, so the two are resolved together rather than separately.
          inherit (resolved) webSatisfied;
          members = verified;
          # The closure, AT EVAL. Everything here is in the catalog index, so a
          # consumer that has to name the images -- the iOS app's embed list,
          # the symbol scan, the view module's stem -- reads them from the same
          # resolution the set was built from instead of globbing the result and
          # hoping the two agree.
          modules = map (e: rec {
            inherit (e) name version;
            type = e.type or "core";
            image = e.variants.${target}.main;
            # <embedDir>/<bundle>/<binary> on iOS; the image itself elsewhere.
            bundle = if lib.hasPrefix "ios" target then builtins.dirOf image else image;
          }) closure;
        };
      } ''
      set -euo pipefail
      mkdir -p "$out/$embedDir"

      for m in $members; do
        cp -R "$m/variant/$embedDir/." "$out/$embedDir/"
      done
      chmod -R u+w "$out/$embedDir"

      python3 - <<'PY'
      import json, os

      out = os.environ["out"]
      target = os.environ["target"]
      embed = os.environ["embedDir"]
      members = os.environ["members"].split()

      modules = []
      for m in members:
          info = json.load(open(os.path.join(m, "info.json")))
          # `main` is bundle-relative and already starts with the embed dir --
          # that is what mkMobilePayload lays a variant out for -- so it names
          # the image in the set directly.
          image = info.pop("main")
          if not image:
              raise SystemExit("error: %s ships no main for %s" % (info["name"], target))
          if not os.path.exists(os.path.join(out, image)):
              raise SystemExit(
                  "error: %s declares main '%s' for %s, and it is not in the embedded set.\n"
                  "       A mobile variant's payload must be laid out as %s/<image>, which is\n"
                  "       the only directory the platform loader will look in." % (
                      info["name"], image, target, embed))
          info["image"] = image
          modules.append(info)

      manifest = {
          "bundledSetVersion": "1",
          "target": target,
          "embedDir": embed,
          "requested": os.environ["requested"].split(),
          "modules": modules,
          # THE OTHER HALF (#183): dependencies of the members above that this
          # image carries as `web` modules rather than Bundled ones. Recorded
          # because the device reads this manifest and nothing else, and a set
          # whose closure is only closed alongside the web assets has to say so.
          "webSatisfied": os.environ["webSatisfied"].split(),
          # THE PLATFORM FLOOR, as this build derived it (nix/platform-floor.nix).
          # The app compiles this manifest in (BundledSetManifest.h) and the App
          # Manager reads the floor back out of it, which is what lets a shell
          # judge a repository it has never seen: `absent` names the Platform
          # modules a Downloaded module may not depend on HERE.
          "platformFloor": json.loads(os.environ["platformFloorJson"]),
      }
      json.dump(manifest, open(os.path.join(out, "bundled-set.json"), "w"),
                indent=2, sort_keys=True)
      print("==> Bundled set for %s: %s" % (target, ", ".join(m["name"] for m in modules)))
      if manifest["webSatisfied"]:
          print("==> satisfied by this image's web half: %s"
                % ", ".join(manifest["webSatisfied"]))
      PY
    '';

in
{
  inherit variantForSystem systemForVariant embedDirFor readCatalog refusals
    resolveSet resolveClosure mkBundledSet;
  inherit platformFloor;
}
