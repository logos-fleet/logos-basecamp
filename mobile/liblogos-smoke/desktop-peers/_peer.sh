#!/usr/bin/env bash
# What both desktop peers are made of: a logoscore daemon of their own, and
# the two ways to read an answer out of one.
#
# Sourced, never run. The caller sets MODULES, CONFIG and LOGOSCORE, then calls
# start_peer_daemon.

# One `logoscore` against this peer's own instance. Each peer gets its own
# --config-dir; two daemons sharing one would drive each other.
core() { "$LOGOSCORE" --config-dir "$CONFIG" "$@"; }

# The answer half of a `logoscore call`, which wraps every answer in a
# `{module, method, result, status}` envelope.
value() { core call "$@" --json | jq -c '.result'; }

# ...and the answer half of a `result`-returning method, which nests one
# further inside `{success, value, error}`. Separate from `value` on purpose:
# a `result` whose success is false still comes back as a perfectly good JSON
# object, so a caller that used `value` for both would read `null` as an id and
# carry on -- which is how `add_group_member` was handed a whole envelope as a
# convo_id and answered "conversation not found", and how `--peer` came out as
# `/ip4/.../tcp/9500/p2p/{ "error": null, ... }`.
result() {
  local module="$1" method="$2" out
  out=$(value "$@")
  if [ "$(echo "$out" | jq -r '.success')" != true ]; then
    echo "$module.$method failed: $(echo "$out" | jq -r '.error')" >&2
    return 1
  fi
  echo "$out" | jq -c '.value'
}

# A daemon on a FRESH config directory -- a peer that inherited the last run's
# conversations could answer one of them -- killed when the caller exits.
start_peer_daemon() {
  rm -rf "$CONFIG"; mkdir -p "$CONFIG"
  core -D -m "$MODULES" > "$CONFIG/daemon.log" 2>&1 &
  trap 'kill %1 2>/dev/null || true' EXIT

  echo "==> waiting for the daemon"
  for _ in $(seq 1 60); do core status >/dev/null 2>&1 && break; sleep 1; done
  core status >/dev/null || {
    echo "the daemon never came up; see $CONFIG/daemon.log" >&2
    exit 1
  }
}
