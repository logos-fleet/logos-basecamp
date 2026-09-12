#!/usr/bin/env bash
# The DESKTOP half of the gossipsub criterion: a logoscore installation the
# phone dials, publishing on the topic once a second and reading what the
# phone publishes back.
#
#   libp2p-peer.sh <modules-dir> [config-dir]
#
# It prints the `--peer` string the phone needs and then stays in the
# foreground: `phone-<nonce>` appearing HERE is the half of the exchange the
# phone cannot prove on its own.
set -euo pipefail

MODULES=${1:?usage: libp2p-peer.sh <modules-dir> [config-dir]}
CONFIG=${2:-${TMPDIR:-/tmp}/logos-libp2p-peer}
LOGOSCORE=${LOGOSCORE:-logoscore}
TOPIC=${LOGOS_SMOKE_TOPIC:-logos-smoke}
PORT=${LIBP2P_PORT:-9500}

core() { "$LOGOSCORE" --config-dir "$CONFIG" "$@"; }
# The answer half of a `logoscore call`'s `{module, method, result, status}`
# envelope...
value() { core call "$@" --json | jq -c '.result'; }
# ...and of a `result`-returning method, which nests one further inside
# `{success, value, error}`. Every libp2p_module method below is one of those,
# so a caller that used `value` would hand the whole envelope on -- which is
# how `--peer` came out as `/ip4/.../tcp/9500/p2p/{ "error": null, ... }`.
result() {
  local method="$2" out
  out=$(value "$@")
  if [ "$(echo "$out" | jq -r '.success')" != true ]; then
    echo "libp2p_module.$method failed: $(echo "$out" | jq -r '.error')" >&2
    return 1
  fi
  echo "$out" | jq -c '.value'
}

rm -rf "$CONFIG"; mkdir -p "$CONFIG"
core -D -m "$MODULES" > "$CONFIG/daemon.log" 2>&1 &
trap 'kill %1 2>/dev/null || true' EXIT

echo "==> waiting for the daemon"
for _ in $(seq 1 60); do core status >/dev/null 2>&1 && break; sleep 1; done
core status >/dev/null || { echo "the daemon never came up; see $CONFIG/daemon.log" >&2; exit 1; }

core load-module libp2p_module >/dev/null
# `str:`, not `json:`: createNode's parameter is a tstr the module parses
# itself, unlike chat's init, which takes a record.
# gossipsubTriggerSelf off, for the same reason the phone turns it off: the
# default delivers this node's own publishes back to it, and this loop would
# then report `desktop-hello-N` as "from the phone".
result libp2p_module createNode \
  "str:{\"addrs\":[\"/ip4/0.0.0.0/tcp/$PORT\"],\"transport\":\"tcp\",\"mountGossipsub\":true,\"gossipsubTriggerSelf\":false}" >/dev/null
result libp2p_module start >/dev/null
result libp2p_module gossipsubSubscribe "$TOPIC" >/dev/null

peer_id=$(result libp2p_module getNodeInfo PeerId | jq -r '.')
# The LAN address, because a phone cannot reach 127.0.0.1. Whichever
# non-loopback v4 address the node is actually listening on.
addr=$(result libp2p_module getNodeInfo Multiaddrs \
  | jq -r '.. | strings | select(startswith("/ip4/") and (contains("/ip4/127.") | not))' \
  | head -1)

echo
echo "libp2p peer ready. Hand this to the phone:"
echo "  --peer $addr/p2p/$peer_id"
echo "  --topic $TOPIC"
echo

# Publish on a timer, read on the same loop. The phone subscribes before it
# dials, but gossipsub's mesh is built when the connection comes up, so the
# first few publishes after a dial may find no mesh -- hence a loop rather
# than one message.
echo "==> publishing and reading on '$TOPIC' (^C to stop)"
n=0
while :; do
  n=$((n + 1))
  result libp2p_module gossipsubPublish "$TOPIC" "desktop-hello-$n" >/dev/null || true
  # An empty queue is an ordinary outcome and comes back as a FAILED result
  # ("timeout waiting for message"), so the failure is swallowed here rather
  # than printed once a second.
  got=$(result libp2p_module gossipsubNextMessage "$TOPIC" 1000 2>/dev/null | jq -r '. // ""' || true)
  case "$got" in
    ""|null|desktop-hello-*) ;;
    *) echo "  from the phone: $got" ;;
  esac
  sleep 1
done
