# The local catalog's signing material

A Bundled set is assembled from **signed** `.lgx` packages: `--bundle` resolves
a closure out of a catalog, and every member has to carry a valid Ed25519
signature by a DID the catalog declares before its variant is unpacked into the
app image (`nix/verify-lgx-member.sh`). That is true of the local catalogs this
repo builds for its own tests and for the mobile dev set too — there is no
"unsigned, because it is local" path, precisely so the path the tests exercise
is the path a Store shell build takes.

`keys/logos-catalog-test.{jwk,pub,did}` is the key those local catalogs are
signed with.

**It is a test key and its secret half is in this repository.** A signature by
it proves only that the catalog builder ran. Nothing in this tree ever adds it
to a device keyring, and no published package is signed with it. A real catalog
release is signed by a key that is not in a git repo; the only thing a build
points at is the DID, which is data.

`icon.png` is a 256x256 PNG. The LGX icon contract (manifest 0.4.0+) makes one
mandatory for every `ui_qml` package, and `lgx sign` enforces it — so a catalog
that publishes a view module needs artwork even when nothing renders it.

## `pinned-release/` — a release this repo consumes but did not produce

A Bundled set can be handed its members two ways:

| | entries | what is trusted |
|---|---|---|
| a local catalog | `file` into a directory the build just made | the path |
| a **pinned release** | `url` + `sha256` + `rootHash` | the bytes |

The mobile dev set above is the first kind. A Store shell build is the second:
it consumes a release somebody else published, over a network, possibly
through a mirror. Only the second kind has a fetch to get wrong, and it was
dead code until something consumed one.

`pinned-release/` is that something. It is the fixture catalog
(`nix/bundled-set-fixture.nix`) published by nix-bundle-lgx's `mkRelease`, and
**committed**: the `.lgx` bytes the Bundled-set test admits were produced by a
different build on a different day, which is the only way the fetch path means
anything. `nix/bundled-set-test.nix` rewrites each entry's `file` into a
`file://` URL at eval — a committed index cannot carry an absolute path into
your checkout — and nothing else about the path changes. The fetch is still a
fixed-output derivation keyed by the index's `sha256`, its store-path name
still carries the Merkle root, and `nix/verify-lgx-member.sh` still re-checks
that root against the pin before anything is unpacked.

Regenerate after changing the fixtures:

```bash
nix build .#bundled-set-release
rm -rf mobile/catalog/pinned-release
cp -R --no-preserve=mode result mobile/catalog/pinned-release   # macOS: cp -R && chmod -R u+w
```

A stale `pinned-release/` is not a silent failure: the test resolves the same
closure out of both catalogs and diffs the two manifests and the two embedded
`Frameworks/` trees, so anything the fixtures grow that the release does not
have shows up as a difference.
