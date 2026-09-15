# The App Manager on a Store shell

Browsing a catalog on a phone, installing a Downloaded module from it, and the
two prompts a store requires before either happens.

```
  CatalogEntry      what one catalog row MEANS here: may it be installed, and
                    which of its links will this shell open
  CatalogSource     what this device was POINTED AT: the repository, the signers
                    to anchor, the row to install -- and what it refuses
  InstallGate       the order an install happens in, and the three places it stops
  ConsentQueue      the 4.7.3 prompt queue: one dialog at a time, one per pair
  ConsentScript     what a LAUNCH was asked to answer that prompt with, off the
                    command line: deny, grant, dismiss, expect-granted
  StoreAppManager   the QML-facing object that owns the three

  ../basecamp-shell/src/ShellStoreBackend.cpp   the one seam, over the real modules
  ../basecamp-shell/src/ShellCatalogDriver.cpp  the run that exercises all of it
  ../basecamp-shell/src/ShellConsentDriver.cpp  the consent half of that run,
                                                across two launches
```

Almost none of it is platform code, and none of it is a module. What a Store
shell contributes is four answers — the catalog, what is installed, the signer,
and "open this URL" — and everything else is plain C++ that a desktop test
drives. `nix build .#unit-tests` covers every class here in under two seconds
(`tests/catalog_entry_test.cpp`, `catalog_source_test.cpp`,
`install_gate_signer_test.cpp`, `consent_queue_test.cpp`,
`store_app_manager_test.cpp`, `store_module_dirs_test.cpp`,
`platform_floor_test.cpp`).

## Where each decision lives, and why it lives there

| | who decides | why not here |
|---|---|---|
| whether a package's variants can run on this build | **package_manager** (`variantAvailability` / `catalogAvailability`) | the variant vocabulary is logos-package's, and a second implementation of "does `darwin-amd64` match `darwin-x86_64`" is a second answer |
| whether a package may be installed as signed | **package_manager** (`signerTrust`) | the prompt and the installer must not be able to disagree, so one method answers both |
| whether a cross-module call needs consent | **capability_module** (`requestModule`) | it is the only place every cross-module authority is minted; a gate anywhere else is one a module can route around |
| whether a Platform module a row needs is in THIS build | **the build** (`nix/platform-floor.nix`), applied here (`PlatformFloor`) | the Bundled set is fixed at build time, so the fact is a property of the app image; the verdict can only be applied here, against a repository the build never saw |
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

## ...and neither has a row that needs a Platform module this build lacks

ADR 0009's second rule, the mirror of ADR 0007's "a Bundled module may depend
only on Bundled modules": a Downloaded module may depend on a **Platform**
module — one that owns access a webview cannot give it, `"platform": true` in
its metadata.json — only where that module is in the Bundled set of the shell it
lands on. A Bundled set cannot gain a member at run time, so there is no remedy
on the device: the install succeeds and the module dies at its first call.

The floor is **derived at build time, never maintained as a list**.
`nix/platform-floor.nix` reads which catalog members are Platform modules
(the flag each module declares, carried into the index by nix-bundle-lgx's
`mkCatalog`) and subtracts the Bundled closure `nix/bundled-set.nix` resolved
for this shell. `nix/bundled-set.nix` writes the answer into
`bundled-set.json`:

```json
"platformFloor": { "present": ["delivery_module"], "absent": ["token_list_module"] }
```

The build compiles that manifest in (`BundledSetManifest.h`), `ShellModulesBackend`
reads the floor out of it and `StoreAppManager` applies it to every catalog row:

    chat_module 0.2.2 -- requires delivery_module, not in this build; install control: absent

**The walk is transitive**, and that is the whole reason the floor is derived
rather than declared. `chat_module` is deliberately *not* a Platform module — it
is built on one, its network comes from `delivery_module` — and `chat_ui` reaches
that two edges down through its own dependency on `chat_module`. A floor that
read only a row's own flag would offer chat_ui on a shell that cannot deliver a
single message. So the refusal names the Platform module, not the intermediate.

A build whose manifest carries no `platformFloor` declares none, and refuses
nothing: emptying the App Manager of a shell that works is a worse failure than
the one the floor prevents.

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

### The origin is the host's to declare, and nothing else knows it

The whole gate is a function of where each module came from, and capability_module
is told rather than asked. It cannot ask the module (it has every reason to lie),
the core does not know (a Downloaded module is discovered in a scanned directory
exactly as a shipped one is), and capability_module deliberately does not persist
it -- an app image can change under a device between launches, so a remembered
origin would outlive the fact.

So `ShellModulesBackend::declareModuleOrigins` calls `setModuleOrigin` for every
module the app image carries and every module the user installed: at startup,
because a module installed by an EARLIER launch is already on disk and its first
call this launch must still be gated, and again before each new install is
loaded, because a `web` module starts calling out while it comes up.

**Forgetting it fails silently and open.** An undeclared module is `bundled`,
which is the right default for the desktop and for every build that has never
installed anything -- and which also means the gate simply never fires. A Store
shell that never prompted looked exactly like one where nobody had installed
anything.

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

## Which catalog, and whose signature

Neither is a property of the build, and on a phone neither can be a config file
a developer edits (ADR 0003). Both arrive on the Shell's command line, through
`CatalogSource`:

```
--repository <url>            added to package_downloader
--trust-signer <name>=<did>   added to package_manager's keyring
--install <package>           the row to install once the catalog is up
```

**A repository's own `trustedSigners` anchors nothing.** It is a claim the
repository makes about itself, fetched from the repository, and wiring it into an
install decision would let a catalog authorise its own packages — which is
exactly the failure the field's name invites. logos-package-downloader parses it
and consults it for nothing, deliberately; the only anchor is the local keyring,
and `--trust-signer` is the explicit act that enters one.

**Nothing else enters that keyring, and that is the milestone-1 answer to "who
anchors, on a phone" — logos-workspace ADR 0008.** No shipped vendor anchor: a
Store build that carried one would silently authorise every first-party-signed
package on every device, and the only candidate key in this repo is a committed
fixture whose private half is in the tree. No tap-to-trust in the signer prompt
yet either — that is the destination, but a Trust button on a phone with no
out-of-band way to check a DID is trust-on-first-use with the verification
removed. So a Store shell in a user's hands browses the catalog, downloads,
verifies, computes the prompt, and *refuses*. **That refusal is the feature**,
not a gap.

Because `--trust-signer` is the only route in, a refusal has to say which DID it
refused: it is the one actionable thing in the message and a device has no
`lgx keyring` to ask afterwards. `InstallGate::refusedSigner()` carries the
signer's name and DID out of the refusal, and `StoreAppManager` logs them;
`signerPrompt()` stays empty, because identity survives a refusal and the
Install button does not.

**The keyring is a named directory** (`ModuleDirectories::keyringDir`), not lgx's
default. That default is derived from `$XDG_CONFIG_HOME` or `$HOME` — variables a
phone app does not set and has no claim on — and under the `require` policy a
keyring that landed anywhere but where `addTrustedKey` wrote refuses every
install with a message about the PACKAGE rather than about this device's trust.

**The URL goes through `CatalogEntry::linkRefusal`**, the same rule a row's
report link does. It is fetched rather than opened, but it is the same kind of
input from the same kind of author, and a second implementation of "which URLs
will this shell touch" is a second answer — the laxer of which decides.

A release to point it at is [`../../nix/local-catalog.nix`](../../nix/local-catalog.nix):
the same signed `.lgx` files the Bundled set is built from, published as a
*repository* (`logos-repo.json` + `packages[].versions[]` with
url/size/sha256/rootHash/manifest/signature) and served on loopback by
`nix run .#serve-local-catalog`.
