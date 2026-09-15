# EACH SHELL'S PLATFORM FLOOR, DERIVED FROM THE CATALOG IT OFFERS.
#
# ADR 0009's second rule, and the mirror of ADR 0007's "a Bundled module may
# depend only on Bundled modules": a Downloaded module may depend on a PLATFORM
# module -- one that owns access a webview cannot give it, `"platform": true`
# in metadata.json -- only where that module is in the Bundled set of the shell
# it lands on.
#
# A Bundled set is fixed at build time (ADR 0007), so on a shell that did not
# bundle it there is no remedy on the device: the install succeeds and the
# module dies at its first call. Guideline 4.7 makes the host liable for what it
# lists, so the answer has to exist before the row is offered.
#
# DERIVED, NEVER MAINTAINED AS A LIST. Both halves are already written down --
# which modules are Platform modules is `platform: true` in each module's
# metadata.json, carried into the catalog index by nix-bundle-lgx's mkCatalog;
# which modules this shell ships is the closure nix/bundled-set.nix resolves
# from `--bundle`. A third home for the same truth is a list that drifts, and it
# drifts into exactly the failure the honest-availability rule exists to
# prevent.
#
#   floorOf        index + closure -> { present; absent; }
#   missingFor     the first absent Platform module a name reaches
#   reasonFor      the sentence a refused row carries
#
# WHY THE WALK IS TRANSITIVE. `chat_module` is deliberately NOT a Platform
# module: it is built ON one -- its network comes from `delivery_module` -- and
# `chat_ui` reaches that two edges down. A floor that read only a row's own flag
# would offer chat_ui on a shell that cannot deliver a single message. So what
# is asked of a name is "does anything you reach need a Platform module this
# build does not have", and the answer names THAT module, not the intermediate
# the user has never heard of.
#
# THE RUNTIME HALF IS mobile/appmanager/PlatformFloor.{h,cpp}: the same walk,
# over the same floor, read back out of bundled-set.json on the device. It has
# to be both -- the floor can only be DERIVED where the Bundled closure is
# known (here), and the verdict can only be APPLIED where the catalog rows are
# known, which on a Store shell is at run time against a repository this build
# never saw.
{ lib }:

let
  # Strings, or `{ name = ...; }` entries: the two spellings a catalog index
  # allows, the same two nix/bundled-set.nix reads.
  depNamesOf = entry:
    map (d: if builtins.isString d then d else d.name) (entry.dependencies or [ ]);
in
rec {
  # name -> the names it declares, for every package in a catalog index.
  dependenciesOf = index:
    lib.listToAttrs (map (p: { name = p.name; value = depNamesOf p; }) index.packages);

  # The Platform modules a catalog knows about. `platform` is absent from an
  # index published before the flag existed, and absent reads as false -- which
  # is the safe direction: an unflagged module is judged by the variant rule
  # alone, exactly as it was.
  platformNamesOf = index:
    map (p: p.name) (lib.filter (p: p.platform or false) index.packages);

  # THE FLOOR. `closure` is what nix/bundled-set.nix resolved for this shell.
  #
  # `absent` is the load-bearing half: a Platform module the catalog holds and
  # this build does not carry. `present` is here so the manifest states what the
  # shell HAS rather than only what it lacks -- a floor that listed only
  # refusals would be unreadable on a device, where there is no catalog to
  # subtract it from.
  floorOf = { index, closure }:
    let
      bundled = map (e: e.name) closure;
      declared = platformNamesOf index;
    in
    {
      present = lib.filter (n: lib.elem n bundled) declared;
      absent = lib.filter (n: !(lib.elem n bundled)) declared;
    };

  # The first Platform module `name` reaches that this build does not ship, or
  # null. `dependencies` is a name -> [ name ] attrset (dependenciesOf, or a
  # Downloaded catalog's own graph).
  #
  # A name the floor does not carry is NOT refused: the floor answers one
  # question, and whether some other module ships a variant a shell can install
  # is the variant rule's answer. Two implementations of one verdict is how the
  # two come to disagree.
  missingFor = { floor, dependencies, name }:
    let
      step = state: n:
        if state.found != null then state
        else if lib.elem n floor.absent then state // { found = n; }
        else if lib.elem n state.seen then state
        else lib.foldl' step (state // { seen = state.seen ++ [ n ]; })
          (dependencies.${n} or [ ]);
    in
    (lib.foldl' step { seen = [ name ]; found = null; } (dependencies.${name} or [ ])).found;

  # The sentence a refused row carries, and the ONE place it is spelled. The
  # runtime half repeats it (PlatformFloor::reasonFor) because a device cannot
  # evaluate nix; the tests on both sides assert the same string so the two
  # cannot drift apart unnoticed.
  reasonFor = missing: "requires ${missing}, not in this build";

}
