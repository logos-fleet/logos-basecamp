#!/usr/bin/env bash
# Admit one .lgx into a Bundled set, or refuse it.
#
#   verify-lgx-member.sh <package.lgx> <target> <root-pin|-> <out-dir> <signer-did>...
#
# A SEPARATE FILE, not a fragment inlined into bundled-set.nix, so the negative
# test can run THIS -- the admission path itself -- over a tampered package
# rather than re-implementing its checks and asserting against the
# re-implementation.
#
# Three checks, three different tampers:
#
#   lgx verify     recomputes the Merkle tree over the entries actually present
#                  and validates the Ed25519 signature over manifest.json, so a
#                  payload edit fails on the hashes and a manifest edit fails on
#                  the signature.
#   signer gate    the DID must be one the catalog declares. `lgx verify`
#                  REPORTS trust, it does not enforce it: a validly signed
#                  package from any key at all passes it, which for a build-time
#                  installer is no check.
#   root pin       when the catalog pins a Merkle root, the manifest's own root
#                  must equal it -- the difference between a catalog promising
#                  content and a catalog promising a URL.
set -euo pipefail

pkg="$1"; target="$2"; pin="$3"; out="$4"; shift 4
signers=("$@")

[ -f "$pkg" ] || { echo "verify-lgx-member: no such package: $pkg" >&2; exit 1; }
[ "${#signers[@]}" -gt 0 ] || {
  echo "verify-lgx-member: no signer named; there is then no key '$pkg' could be checked against" >&2
  exit 1
}

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
cp "$pkg" "$work/pkg.lgx"
chmod +w "$work/pkg.lgx"

lgx verify "$work/pkg.lgx"
lgx manifest "$work/pkg.lgx" --json > "$work/manifest.json"
lgx signature "$work/pkg.lgx" > "$work/sig.json" || true

python3 - "$work" "$pin" "${signers[@]}" <<'PY'
import json, sys

work, pin, allowed = sys.argv[1], sys.argv[2], sys.argv[3:]
manifest = json.load(open(work + "/manifest.json"))
root = manifest.get("hashes", {}).get("root", "")

if pin != "-" and root != pin:
    sys.exit("verify-lgx-member: Merkle root mismatch: package is %s, catalog pinned %s"
             % (root or "<absent>", pin))

raw = open(work + "/sig.json").read().strip()
if not raw:
    sys.exit("verify-lgx-member: package is unsigned; a Bundled-set member must be "
             "signed by one of: %s" % ", ".join(allowed))
did = json.loads(raw).get("did", "")
if did not in allowed:
    sys.exit("verify-lgx-member: package is signed by %s, which the catalog does not "
             "list.\n                   allowed: %s" % (did, ", ".join(allowed)))
print("    %s %s  root %s" % (manifest["name"], manifest["version"], root))
PY

mkdir -p "$out"
lgx extract "$work/pkg.lgx" -v "$target" -o "$work/extracted"
if [ ! -d "$work/extracted/$target" ]; then
  echo "verify-lgx-member: $pkg ships no '$target' variant" >&2
  exit 1
fi
cp -R "$work/extracted/$target" "$out/variant"
chmod -R u+w "$out/variant"

python3 - "$work" "$target" "$out" <<'PY'
import json, sys

work, target, out = sys.argv[1], sys.argv[2], sys.argv[3]
manifest = json.load(open(work + "/manifest.json"))
sig = json.load(open(work + "/sig.json"))
json.dump({
    "name": manifest["name"],
    "version": manifest["version"],
    "type": manifest.get("type", "core"),
    "dependencies": [d if isinstance(d, str) else d.get("name")
                     for d in manifest.get("dependencies", [])],
    "main": manifest.get("main", {}).get(target),
    "view": manifest.get("view") or None,
    "rootHash": manifest.get("hashes", {}).get("root"),
    "signer": sig.get("did"),
}, open(out + "/info.json", "w"), indent=2, sort_keys=True)
PY
