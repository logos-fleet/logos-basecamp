# Building a catalog: the publish half of ADR 0007's "one catalog, per-platform
# variants".
#
# nix/bundled-set.nix consumes a catalog; this produces one. They are separate
# files because they are separate jobs with separate lifetimes -- a real Store
# shell build consumes a PINNED release it did not produce, and only the tests
# and the mobile dev set produce one locally. Keeping the producer out of the
# consumer is what stops "the set I built" and "the set I can verify" from being
# the same code path.
#
# WHY THE INDEX IS A NIX VALUE AND NOT A FILE THE CONSUMER READS BACK. Everything
# the closure and the variant check need -- names, versions, dependencies, which
# variants exist -- is known here, at eval, from the spec. Writing it out and
# reading it back would turn every Bundled-set build into an
# import-from-derivation, i.e. would cross-compile three modules before nix
# could tell you that you misspelled one of them.
#
# WHAT IS NOT KNOWN AT EVAL is a package's Merkle root: it exists only once the
# archive does. A locally built catalog therefore publishes UNPINNED entries and
# the consumer falls back to "signed by a declared signer, hashes internally
# consistent". A published release pins the root, which is the stronger promise
# and the one a Store shell build should be using.
{ pkgs, lgx }:

let
  inherit (pkgs) lib;

  # `lgx create` writes a skeleton manifest and there is no `lgx manifest set`,
  # so the fields a catalog needs are patched in the way nix-bundle-lgx patches
  # them: rewrite manifest.json inside the archive before the variants go in.
  # After `lgx add` the hashes are recomputed and `lgx sign` covers the result,
  # so nothing here outlives the signature.
  patchManifest = pkgs.writeText "lgx-patch-manifest.py" ''
    import io, json, sys, tarfile

    lgx_path, fields_path = sys.argv[1], sys.argv[2]
    fields = json.load(open(fields_path))

    with tarfile.open(lgx_path, "r:gz") as tar:
        members = [(m, tar.extractfile(m).read() if m.isfile() else None)
                   for m in tar.getmembers()]

    patched = []
    for member, data in members:
        if member.name == "manifest.json":
            manifest = json.loads(data)
            manifest.update(fields)
            data = json.dumps(manifest, indent=2).encode()
            member.size = len(data)
        patched.append((member, data))

    with tarfile.open(lgx_path, "w:gz", format=tarfile.GNU_FORMAT) as tar:
        for member, data in patched:
            if data is None:
                tar.addfile(member)
            else:
                tar.addfile(member, io.BytesIO(data))
  '';

  # One package, one derivation: $out/<name>.lgx, signed.
  #
  #   variants."<lgx variant>" = { payload = <dir>; main = "<relpath in payload>"; }
  #
  # `payload` is laid out the way the target's loader wants it -- Frameworks/ on
  # iOS, lib/ on Android -- because that layout is what the Bundled set copies
  # verbatim into the app. See bundled-set.nix's embedDirFor.
  mkPackage =
    { name
    , version
    , type ? "core"
    , description ? "${name}"
    , author ? "Logos"
    , category ? "misc"
    , dependencies ? [ ]
    , view ? null
    , icon ? null
    , variants
    , signingKey        # { jwk = <file>; name = "<key name>"; }
    }:
    assert lib.assertMsg (variants != { })
      "logos-basecamp: catalog package '${name}' declares no variants";
    assert lib.assertMsg (type != "ui_qml" || view != null)
      "logos-basecamp: catalog package '${name}' is ui_qml and must declare a `view`";
    assert lib.assertMsg (type != "ui_qml" || icon != null)
      "logos-basecamp: catalog package '${name}' is ui_qml, and the icon contract (manifest 0.4.0+) makes a 256x256 PNG mandatory for it";
    let
      fields = pkgs.writeText "${name}-manifest-fields.json" (builtins.toJSON ({
        inherit name version type description author category dependencies;
      } // lib.optionalAttrs (view != null) { inherit view; }));

      addOne = variant: v: ''
        lgx add "$pkg" \
          -v ${lib.escapeShellArg variant} \
          -f ${v.payload} \
          -m ${lib.escapeShellArg v.main} \
          ${lib.optionalString (view != null) "--view ${lib.escapeShellArg view}"} \
          ${lib.optionalString (icon != null) "--icon ${icon}"} \
          -y
      '';
    in
    pkgs.runCommand "${name}-${version}-lgx"
      {
        nativeBuildInputs = [ lgx pkgs.python3 ];
        passthru.variants = lib.attrNames variants;
      } ''
      set -euo pipefail
      lgx create ${lib.escapeShellArg name}
      pkg="${name}.lgx"
      python3 ${patchManifest} "$pkg" ${fields}
      ${lib.concatStringsSep "\n" (lib.mapAttrsToList addOne variants)}

      # The key is copied out of the store because `lgx sign` reads
      # <keys-dir>/<name>.jwk and the store is read-only 0444 -- and because a
      # signing key in a keys-dir the builder owns is the shape a real signer
      # has, so this step is not special-cased.
      mkdir -p keys
      cp ${signingKey.jwk} "keys/${signingKey.name}.jwk"
      chmod 600 "keys/${signingKey.name}.jwk"
      lgx sign "$pkg" -k ${lib.escapeShellArg signingKey.name} -d keys \
        --name ${lib.escapeShellArg "Logos catalog (${signingKey.name})"}

      lgx verify "$pkg"
      mkdir -p $out
      cp "$pkg" $out/
    '';

  # Restage a logos-module-builder mobile artifact as an LGX variant payload.
  #
  # `bare` and `view` publish an iOS framework under Library/Frameworks/ and an
  # Android shared object under lib/. A variant payload is the same image in the
  # layout the APP carries it in -- Frameworks/ on iOS, lib/ on Android -- so
  # that assembling the Bundled set is a copy and not a second re-layout that
  # could disagree with the manifest's `main`.
  #
  # `extraFiles` is how a `ui_qml` package satisfies the format's `view`
  # contract: `lgx sign` refuses a ui_qml package whose declared view is not a
  # file inside the variant. On iOS the QML the host renders comes out of the
  # framework's own qrc (ADR 0006 -- one image, nothing to install), so the copy
  # in the variant is what a reader of the PACKAGE sees. It is the same file the
  # qrc is built from, named once here, so the two cannot drift.
  mkMobilePayload =
    { drv
    , stem
    , target
    , extraFiles ? { }
    }:
    let
      ios = lib.hasPrefix "ios" target;
      embedDir = if ios then "Frameworks" else "lib";
      main = if ios then "Frameworks/${stem}.framework/${stem}" else "lib/lib${stem}.so";
      copyExtra = rel: file: ''
        mkdir -p "$out/$(dirname ${lib.escapeShellArg rel})"
        cp ${file} $out/${rel}
      '';
    in
    {
      inherit main;
      payload = pkgs.runCommand "${stem}-${target}-payload" { } (''
        set -euo pipefail
        mkdir -p $out/${embedDir}
        ${if ios
          then ''cp -R ${drv}/Library/Frameworks/${stem}.framework $out/Frameworks/''
          else ''cp ${drv}/lib/lib${stem}.so $out/lib/''}
        chmod -R u+w $out
        test -e $out/${main} || {
          echo "error: ${stem} produced no ${main}; the artifact is not shaped like a ${target} variant" >&2
          exit 1
        }
      '' + lib.concatStrings (lib.mapAttrsToList copyExtra extraFiles));
    };

  # A catalog: the .lgx files under packages/, an index.json beside them for
  # anything that reads the directory, and the same index as a Nix value for
  # mkBundledSet, which must not have to build the catalog to evaluate against
  # it.
  mkCatalog =
    { release
    , signers
    , packages         # [ { spec = <mkPackage args>; drv = <mkPackage result>; } ]
    }:
    let
      entry = { spec, ... }: {
        inherit (spec) name version;
        type = spec.type or "core";
        dependencies = spec.dependencies or [ ];
        variants = lib.mapAttrs (_: v: { inherit (v) main; } //
          lib.optionalAttrs (spec ? view && spec.view != null) { inherit (spec) view; })
          spec.variants;
        file = "packages/${spec.name}.lgx";
      };

      index = {
        catalogVersion = "1";
        inherit release signers;
        packages = map entry packages;
      };

      root = pkgs.runCommand "catalog-${release}" { } ''
        set -euo pipefail
        mkdir -p $out/packages
        ${lib.concatMapStringsSep "\n" ({ spec, drv }:
          ''cp ${drv}/${spec.name}.lgx $out/packages/'') packages}
        cp ${pkgs.writeText "index.json" (builtins.toJSON index)} $out/index.json
      '';
    in
    { inherit index root; };

in
{
  inherit mkPackage mkCatalog mkMobilePayload;
}
