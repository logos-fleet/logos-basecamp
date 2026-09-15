# A CATALOG RELEASE A DEVELOPER CAN SERVE, and the server that serves it.
#
# The Bundled-set release next door (`nix build .#bundled-set-release`) is a
# BUILD-TIME artifact: `nix/bundled-set.nix` resolves a closure out of it before
# the app image exists. This is the other consumer of the same `.lgx` files --
# logos-package-downloader, at RUN time, on a phone -- and it wants a different
# file, because it answers a different question.
#
#   a Bundled-set release   packages[] with file/sha256/rootHash/variants
#                           "which members does this closure need, and what do
#                            their bytes hash to"
#   a REPOSITORY            logos-repo.json (indexUrl, trustedSigners, link
#                           templates) + index.json with
#                           packages[].versions[] carrying url/size/sha256/
#                           rootHash/manifest/signature
#                           "what may this device install, from where, and who
#                            signed it"
#
# Same packages, same signatures, two indexes. Nothing here re-signs or repacks:
# each row is READ OUT of the `.lgx` it points at (`lgx manifest --json`), so the
# index cannot describe a package the file is not -- which is precisely what
# lgpd's index->file binding refuses at download time.
#
# THE PORT IS NOT KNOWN AT BUILD TIME, so the URLs cannot be. Both JSON files are
# published with a `@BASEURL@` placeholder and the server substitutes it once it
# has a port. That is also why this is an `apps` entry and not a package: a
# release whose URLs name a port nothing is listening on is not a release.
#
#   nix run .#serve-local-catalog            # 127.0.0.1:8099
#   nix run .#serve-local-catalog -- 9000
#
# It binds LOOPBACK ONLY, over plain http, which is exactly the one case
# logos-package-downloader accepts a non-https repository for -- this curl
# carries no CA bundle at all, so a self-signed local server is not reachable
# however the URL is spelled. The iOS Simulator shares the host's network stack,
# so 127.0.0.1 inside it is this machine's loopback.
{ pkgs
, lgx
, # nix-bundle-lgx's mkMobileCatalog (mkPackage/mkVariantPayload/...).
  catalog
, # { name = "logos-catalog-test"; jwk = <file>; did = "did:jwk:..."; }
  testKey
, # 256x256 PNG. The LGX icon contract makes one mandatory for `ui_qml`.
  icon
, # name -> { drv, type ? "ui_qml", category ? "test", description ? … }.
  #
  # `drv` is a module's `web` output, holding one `<name>_web/` directory: the
  # manifest, the loader page(s) and the wasm image. That directory IS the
  # variant payload -- an installed `web` module is that tree, copied.
  #
  # `type` IS NOT COSMETIC and is why this is a spec rather than a bare
  # derivation. A `ui_qml` package must declare a view and carry an icon (the
  # manifest 0.4.0 contract, asserted by mkPackage); a `core` one declares
  # neither and HAS neither -- the keystore's `web` variant is a Worker and a
  # wasm image, and a catalog that published it as a view would fail at `lgx
  # sign` naming a QML document that was never in it.
  webVariants ? { }
, # name -> a directory holding `<name>.lgx`, published verbatim. The fixture
  # catalog's members come in this way: a NATIVE-ONLY package is what makes
  # "listed as unavailable with the reason, and no install control" observable
  # against a real catalog rather than only in a unit test.
  prebuilt ? { }
}:

let
  inherit (pkgs) lib;

  # The module's QML entry, as buildWebViewModule lays it out: everything in the
  # declared directory ships under `view/`. Asserted against the payload below
  # rather than trusted -- `lgx sign` refuses a ui_qml package whose declared
  # view is not a file in the variant, and failing there would name the view and
  # not the assumption that produced it.
  viewEntry = "view/Counter.qml";

  # One entry of `webVariants` with its defaults filled in, so that every
  # question below asks `spec.type` rather than re-deciding what an unspecified
  # one means.
  specFor = name: spec: {
    type = "ui_qml";
    category = "test";
    description = "${name}, published as a `web` variant for a Store shell";
    # WHAT THE MODULE ITSELF DECLARES, and it is not decoration here (#169). A
    # Store shell judges a row against its own Platform floor by WALKING this
    # list: a `web` module that reaches a Platform module the shell did not
    # bundle must be listed unavailable rather than installed and left to die at
    # its first call. A package published with an empty list is a package the
    # floor cannot judge, so the caller passes the module's own
    # `config.dependencies` rather than letting this default stand.
    dependencies = [ ];
  } // spec;

  # One `web` variant, out of a module's `web` output.
  webPayload = name: spec: {
    main = "index.html";
    payload = pkgs.runCommand "${name}-web-payload" { } (''
      set -euo pipefail
      src=$(echo ${spec.drv}/*_web)
      [ -d "$src" ] || { echo "error: ${spec.drv} holds no <module>_web directory" >&2; exit 1; }
      mkdir -p $out
      cp -r "$src"/. $out/
      chmod -R u+w $out

      # The things the rest of this file assumes, each failing HERE with its own
      # name rather than deep inside `lgx sign`.
      test -f $out/manifest.json || { echo "error: ${name}'s web variant has no manifest" >&2; exit 1; }
      grep -q '"name":"${name}"' $out/manifest.json \
        || { echo "error: ${name}'s manifest does not name it" >&2; exit 1; }
      test -f $out/index.html || { echo "error: ${name} has no index.html to be its main" >&2; exit 1; }
    '' + lib.optionalString (spec.type == "ui_qml") ''
      test -f $out/${viewEntry} \
        || { echo "error: ${name} ships no ${viewEntry}; the view contract would fail at sign time" >&2; exit 1; }
    '');
  };

  # A `core` package declares no view and carries no icon, and must not: the
  # manifest 0.4.0 contract asserted by mkPackage makes both mandatory for a
  # `ui_qml` one and meaningless for anything else.
  mkWebPackage = name: entry:
    let spec = specFor name entry; in
    catalog.mkPackage ({
      inherit name;
      inherit (spec) type description category dependencies;
      version = "1.0.0";
      variants.web = webPayload name spec;
      signingKey = { inherit (testKey) jwk name; };
    } // lib.optionalAttrs (spec.type == "ui_qml") {
      inherit icon;
      view = viewEntry;
    });

  webPackages = lib.mapAttrs mkWebPackage webVariants;
  members = lib.mapAttrsToList (name: drv: { inherit name drv; }) (webPackages // prebuilt);

  # Every row's facts, read out of the archive. `lgx manifest --json` is the
  # package's own answer for its Merkle root, so the pin the device re-checks is
  # the one the package states rather than a second computation that could
  # disagree with the one doing the checking.
  release = pkgs.runCommand "logos-local-catalog"
    {
      nativeBuildInputs = [ lgx pkgs.python3 ];
      signerDid = testKey.did;
      passthru.members = map (m: m.name) members;
    } ''
    set -euo pipefail
    mkdir -p $out/packages
    ${lib.concatMapStringsSep "\n"
        ({ name, drv }: ''cp ${drv}/${name}.lgx $out/packages/'') members}
    chmod -R u+w $out/packages

    for pkg in $out/packages/*.lgx; do
      lgx verify "$pkg"
      lgx manifest "$pkg" --json > "$pkg.manifest.json"
    done

    python3 - <<'PY'
    import base64, hashlib, json, os

    out = os.environ["out"]
    did = os.environ["signerDid"]
    pkgdir = os.path.join(out, "packages")

    packages = []
    for filename in sorted(os.listdir(pkgdir)):
        if not filename.endswith(".lgx"):
            continue
        path = os.path.join(pkgdir, filename)
        data = open(path, "rb").read()
        manifest = json.load(open(path + ".manifest.json"))
        name = manifest["name"]
        # THE WHOLE MANIFEST, not a hand-written subset. lgpd binds the
        # downloaded file to the index on name/version/main/dependencies/type,
        # and a subset assembled here would be a second statement of what the
        # package is -- the one case the binding exists to catch.
        packages.append({
            "name": name,
            # A repository declares per-module links as TEMPLATES
            # (logos-repo.json below); these are the per-package overrides, and
            # they are absent on purpose so the templates are what gets
            # exercised.
            "versions": [{
                "releasedAt": "2026-09-13T00:00:00Z",
                "publisherRef": "logos-catalog-test",
                "url": "@BASEURL@/packages/" + filename,
                "size": len(data),
                "sha256": "sha256-" + base64.b64encode(hashlib.sha256(data).digest()).decode(),
                "rootHash": manifest["hashes"]["root"],
                "manifest": manifest,
                # The DID the catalog ADVERTISES. lgpd refuses a download whose
                # file is unsigned, whose signature does not verify, or which
                # verifies under a different DID.
                "signature": {"did": did},
            }],
        })
        print("published %s %s  root %s" % (name, manifest["version"],
                                            manifest["hashes"]["root"][:16]))

    json.dump({"catalogVersion": "1", "release": "local", "packages": packages},
              open(os.path.join(out, "index.json.in"), "w"), indent=2, sort_keys=True)

    json.dump({
        "name": "logos-local",
        "displayName": "Logos local catalog (developer)",
        "description": "A catalog release served off a developer's own machine.",
        "homepage": "https://logos.co",
        "indexUrl": "@BASEURL@/index.json",
        # ADVISORY, and logos-package-downloader treats it as such on purpose: a
        # repository's claim about itself anchors nothing. The device's keyring
        # is the only anchor, and it is entered by an explicit act
        # (`--trust-signer`, i.e. package_manager.addTrustedKey).
        "trustedSigners": [{"did": did, "name": "logos-catalog-test"}],
        # Guideline 4.7.1 and 4.7.4: one report link and one universal link PER
        # MODULE. Declared once as a template -- a catalog with three hundred
        # modules should not repeat a URL three hundred times -- and expanded per
        # row by the client. https, because a Store shell refuses anything else
        # for a link it hands the operating system.
        "reportUrlTemplate":
            "https://github.com/logos-co/logos-workspace/issues/new?title=Report%20{name}%20{version}",
        "universalLinkTemplate": "https://logos.co/m/{name}?v={version}",
    }, open(os.path.join(out, "logos-repo.json.in"), "w"), indent=2, sort_keys=True)
    PY

    rm -f $out/packages/*.manifest.json
  '';

  serve = pkgs.writeShellApplication {
    name = "serve-local-catalog";
    runtimeInputs = [ pkgs.python3 pkgs.coreutils pkgs.gnused ];
    text = ''
      port="''${1:-8099}"
      root=$(mktemp -d)
      trap 'rm -rf "$root"' EXIT
      base="http://127.0.0.1:$port"

      mkdir -p "$root/packages"
      cp ${release}/packages/*.lgx "$root/packages/"
      sed "s|@BASEURL@|$base|g" ${release}/index.json.in      > "$root/index.json"
      sed "s|@BASEURL@|$base|g" ${release}/logos-repo.json.in > "$root/logos-repo.json"
      chmod -R u+w "$root"

      echo "==> local catalog on $base"
      echo "    repository : $base/logos-repo.json"
      echo "    signer     : ${testKey.name}=${testKey.did}"
      echo "    packages   : ${lib.concatStringsSep ", " (map (m: m.name) members)}"
      echo
      echo "    lgpd --config \$(mktemp -d)/lgpd.json repo add $base/logos-repo.json"
      echo "    xcrun simctl launch --stdout=/tmp/shell.out \$UDID co.logos.BasecampShell \\"
      echo "      --repository $base/logos-repo.json \\"
      echo "      --trust-signer ${testKey.name}=${testKey.did} \\"
      echo "      --install <package>"
      echo

      # --bind 127.0.0.1: loopback is the only host a Store shell will fetch a
      # plain-http repository from, and binding anywhere else would publish an
      # unsigned-transport catalog on the network the machine is on.
      exec python3 -m http.server "$port" --bind 127.0.0.1 --directory "$root"
    '';
  };

in
{
  inherit release serve;
}
