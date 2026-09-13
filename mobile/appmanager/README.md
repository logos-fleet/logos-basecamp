# The App Manager on a Store shell

Browsing a catalog on a phone, installing a Downloaded module from it, and the
two prompts a store requires before either happens.

```
  CatalogEntry      what one catalog row MEANS here: may it be installed, and
                    which of its links will this shell open
  InstallGate       the order an install happens in, and the three places it stops
  ConsentQueue      the 4.7.3 prompt queue: one dialog at a time, one per pair
  StoreAppManager   the QML-facing object that owns the three

  ../basecamp-shell/src/ShellStoreBackend.cpp   the one seam, over the real modules
```

Almost none of it is platform code, and none of it is a module. What a Store
shell contributes is four answers — the catalog, what is installed, the signer,
and "open this URL" — and everything else is plain C++ that a desktop test
drives. `nix build .#unit-tests` covers all four classes in under two seconds
(`tests/catalog_entry_test.cpp`, `install_gate_signer_test.cpp`,
`consent_queue_test.cpp`, `store_app_manager_test.cpp`).

## Where each decision lives, and why it lives there

| | who decides | why not here |
|---|---|---|
| whether a package's variants can run on this build | **package_manager** (`variantAvailability` / `catalogAvailability`) | the variant vocabulary is logos-package's, and a second implementation of "does `darwin-amd64` match `darwin-x86_64`" is a second answer |
| whether a package may be installed as signed | **package_manager** (`signerTrust`) | the prompt and the installer must not be able to disagree, so one method answers both |
| whether a cross-module call needs consent | **capability_module** (`requestModule`) | it is the only place every cross-module authority is minted; a gate anywhere else is one a module can route around |
| what a row is allowed to offer, and which links to open | **here** (`CatalogEntry`) | it is the shell's decision what it will hand to the operating system |
| the order, and when to stop | **here** (`InstallGate`) | |

## A native-only package has no install control

A Store shell installs `web` variants at runtime and nothing else: a phone may
not download native code (ADR 0003). So most of a desktop catalog is
uninstallable on one, and the entry says so — "available on macOS and Linux, not
in this build" — with **no** install control rather than a disabled one. A
greyed-out button is not an answer to "why can I not install this".

The availability verdict comes from package_manager, and
`ShellStoreBackend::configure` is what makes it the right verdict: it calls
`setInstallableVariants(["web"])`. Without that the module answers from the
**native** variant its loader accepts — which on a phone is the Bundled set's,
fixed at build time — and every native-only catalog entry grows an install
control that cannot work. It also sets `setSignaturePolicy("require")`, because a
Downloaded module is third-party code arriving after review.

A row with **no** availability annotation is not available either. An App Manager
that read a missing annotation as "fine" would offer an install for every
native-only package in the catalog, silently, because the row otherwise looks
complete.

## The install order, and why it is the order

```
  1. may this row be installed HERE?      no traffic if not
  2. download                              network, checksum, Merkle root
  3. package_manager.signerTrust           name, DID, keyring, policy
  4. SHOW THE USER, and wait               nothing is installed behind this
  5. package_manager.installPlugin         the same gate, for real
```

Two orderings are wrong in ways a casual reading does not catch. Installing and
*then* showing the signer: a prompt after the fact is not consent, and the
package is already on disk. Prompting *before* the download: there is no signer
to show, because a catalog's advertised DID is a claim about bytes that have not
arrived.

Step 1 asks nothing on purpose. An unavailable row has no install control, so
reaching the gate with one is a wiring bug — and a gate that trusted its caller
would turn that bug into a download.

The gate does **not** load what it installed. "It did not install" and "it
installed and will not run" want different messages and different remedies, so
`StoreAppManager` emits `moduleInstalled` and the Shell decides.

## Consent, and the three ways the naive queue is wrong

capability_module refuses a call between a Downloaded module and anything else
until the user has decided, and announces `consentRequired`. It cannot wait — it
is on the dispatch path of every cross-module call in the process, so blocking
one on a dialog would hold a module thread for as long as a person takes to
read it. The call fails now, the Shell prompts, and the caller's next attempt is
decided.

That leaves the Shell holding what capability_module deliberately did not solve:

- **A refused caller retries.** capability_module announces once per pair, but a
  user who dismisses still arrives here again, and a second module can ask about
  the same target. `ConsentQueue` keys on the **ordered** pair, with a separator
  a registry name cannot contain — with `-`, `("a", "b-c")` and `("a-b", "c")`
  are one key and the second question is never asked.
- **A second question must not repoint the first dialog.** That would have the
  user answer about a pair they never saw. It queues; the same rule the intent
  broker has.
- **A dismissal is not a denial.** A denial *persists* in capability_module, so
  recording "not now" as "never" would be permanent. `dismiss()` records nothing
  and lets the pair be announced again.

The answer goes back through `decideConsent`, authenticated with
capability_module's own token — the trusted core channel, which the **host**
holds and which core uses the same way for `registerRestriction`. When that token
is not available the Shell says the decision was not recorded rather than
pretending it was: the module will ask again, and a silent failure looks to the
user like a decision that did not stick for no reason.

## A link from a catalog is not opened unchecked

"The Shell opens both" (guideline 4.7.1's report link and 4.7.4's universal link)
means the Shell hands the operating system a URL an index author chose. So the
scheme is checked once, on the way in, in `CatalogEntry`:

- **https** always.
- **http** only to a loopback host — which is the local catalog release a
  developer serves while building one, and the only reason plain http survives
  at all. A report reaching its destination in clear text is the one thing a
  report link must not do.
- everything else refused: `file:`, `javascript:`, `data:`, `about:`, an app's
  own custom scheme registered by whatever installed it.

A refused link is **absent** (no affordance) and says why, so a catalog doing
something odd produces one line in the console rather than a report button that
is silently missing. A row with no link at all says nothing: most of a catalog
will have none.

## What is not here yet

The one criterion this does not discharge is a `web` variant installed at
runtime actually *running* in the Web container on a device. Two things are
missing and neither is in this directory:

1. **The package modules are not in any mobile Bundled set.** They have no
   `ios-arm64` / `android-arm64` variant in the mobile catalog —
   package_downloader is libcurl over the network and package_manager is the lgx
   library, and both need a Bare build before a phone can carry them. Until then
   `hasCatalog()` is false on a device and the App Manager says so, which is the
   honest state rather than a broken one.
2. **The Web container does not scan the install directory.** `web-modules` is
   laid out by `nix/mobile-web-assets.nix` at build time
   (`../webview/README.md`); a module installed into the app's writable data
   directory at runtime has to be found there too.
