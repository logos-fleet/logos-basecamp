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
