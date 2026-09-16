# A DECLARED PLATFORM MODULE MUST READ AS ONE IN THE CATALOG.
#
# ADR 0009's flag is written in one repo and acted on in another: a module says
# `"platform": true` in its own metadata.json, and what turns that into a
# refusal on a device is logos-basecamp's catalog index and the floor derived
# from it (nix/platform-floor.nix). Between the two sits the module's flake,
# which has to FORWARD the parsed document -- `inherit (module) config` -- for
# the catalog's `(module.config or { }).platform or false` to see anything.
#
# A flake that forwards nothing is not an error anywhere. The `or false`
# fallback wins, the module reads as ordinary, the catalog builds, every test
# passes, and the only symptom is a Downloaded module offered on a shell that
# cannot run it -- which is the exact failure ADR 0007's honest-availability
# rule exists to prevent. logos-workspace#207: eth_rpc_module was in that state
# for three weeks with `platform: true` committed and nothing downstream able
# to read it.
#
# So this check asks the question TWICE, from two different places, and refuses
# to let the answers differ:
#
#   read       what the catalog spec computed -- through the module's flake,
#              the value that reaches the signed index and the floor.
#   declared   metadata.json, read straight off the module's SOURCE TREE. It
#              cannot be affected by what the flake publishes, which is the
#              whole point: it is the declaration, not a second copy of it.
#
# `declared = null` means the entry has no module source to read (a fixture
# built out of this repo, say), and such an entry is skipped rather than
# assumed false: this check reports a DISAGREEMENT between two readings, and
# where there is only one reading there is nothing to disagree about.
#
# ...and THE SECOND HALF, because a flag nothing acts on is the same defect
# one step further along: the check also derives this catalog's Platform FLOOR
# for a shell that bundles only its default apps, and prints the sentence each
# row would carry. A declared Platform module that no default shell ships has
# to come out in `absent`, and every row that reaches it has to be refused BY
# NAME -- which is what makes the declaration mean something to a user.
#
# A failing BUILD rather than a failing eval, deliberately: an eval throw here
# would take out every other output of this flake at once and say so from
# inside a stack trace, and the reader needs the table, not the trace.
{ pkgs, catalog }:

let
  inherit (pkgs) lib;

  inherit (catalog) platformAudit bundledSetLib target defaultApps;
  inherit (bundledSetLib) platformFloor;

  index = catalog.catalog.index;

  nameList = ns: if ns == [ ] then "(none)" else lib.concatStringsSep ", " ns;

  # ── the two readings, and where they disagree ─────────────────────────────
  readable = lib.filter (e: e.declared != null) platformAudit;
  mismatched = lib.filter (e: e.declared != e.read) readable;
  platformNames = map (e: e.name) (lib.filter (e: e.read) readable);

  mismatchRow = e: "  ${e.name}: catalog reads ${lib.boolToString e.read}, metadata.json declares ${lib.boolToString e.declared}";

  # ── the floor a DEFAULT shell derives from this catalog ───────────────────
  # `defaultApps` is what a build with no `--bundle` ships, so every Platform
  # module in this catalog that the default set does not reach is `absent` --
  # and a row whose closure touches one of those is refused by name.
  closure = bundledSetLib.resolveClosure {
    inherit index target;
    apps = defaultApps;
  };
  floor = platformFloor.floorOf { inherit index closure; };
  dependencies = platformFloor.dependenciesOf index;

  verdictOf = name:
    let missing = platformFloor.missingFor { inherit floor dependencies name; }; in
    if missing == null then "offered"
    else platformFloor.reasonFor missing;

  verdicts = map (p: { inherit (p) name; verdict = verdictOf p.name; }) index.packages;
  refused = lib.filter (v: v.verdict != "offered") verdicts;

  verdictRow = v: "  ${v.name}: ${v.verdict}";

  # A Platform module this catalog declares but no default shell carries MUST
  # reach `absent`: that list is what the device reads back out of the manifest
  # and judges a Downloaded row against, so a name missing from it is a row
  # offered on a shell that cannot run it.
  unfloored = lib.filter
    (n: !(lib.elem n floor.present) && !(lib.elem n floor.absent))
    platformNames;

  problems = map mismatchRow mismatched
    ++ map (n: "  ${n}: declared a Platform module, and the derived floor lists it neither present nor absent") unfloored;

  # The message is a FILE rather than a heredoc in the builder: a nix indented
  # string strips the common leading whitespace, and one interpolated line at
  # column 0 would strip it to nothing and take the heredoc's own terminator
  # with it. `cat` a store path and the text is the text.
  pass = pkgs.writeText "catalog-platform-flags.txt" ''
    catalog platform flags agree with metadata.json for ${toString (builtins.length readable)} catalog entries.
    Platform modules (ADR 0009): ${nameList platformNames}

    The floor a default shell (--bundle ${lib.concatStringsSep " " defaultApps}) derives from this catalog:
      present: ${nameList floor.present}
      absent:  ${nameList floor.absent}

    ...and what such a shell would say about each row it cannot honour:
    ${lib.concatStringsSep "\n" (map verdictRow refused)}
  '';

  fail = pkgs.writeText "catalog-platform-flags-fail.txt" ''
    FAIL: the mobile catalog disagrees with what these modules declare.

    ${lib.concatStringsSep "\n" problems}

    A module that declares `"platform": true` (ADR 0009) but reads as false in
    the catalog is SILENTLY INERT: the Platform floor derived from this catalog
    will not list it, and a Downloaded module that depends on it is offered for
    install on a shell that never bundled it.

    The usual cause is the module's flake publishing `packages` and
    `legacyPackages` only. Forward the parsed document too:

        inherit (module) config configFor;

    ...and note the ORDER (logos-workspace#207): the `platform` key is known
    only to logos-fleet/logos-module-builder, and on an older builder that
    passthru THROWS rather than reading false -- which `or false` cannot catch,
    so it would break this flake's evaluation instead of failing this check.
    Move the module's `logos-module-builder` input first, then publish `config`.
  '';
in

if problems == [ ] then
  pkgs.runCommand "catalog-platform-flags" { } ''
    cat ${pass}
    touch $out
  ''
else
  pkgs.runCommand "catalog-platform-flags" { } ''
    cat ${fail}
    exit 1
  ''
