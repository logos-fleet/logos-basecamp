#!/usr/bin/env bash
# The DESKTOP half of the chat criterion: a logoscore installation that the
# phone opens a group with, and that answers inside it.
#
# The phone's own log is only half the evidence. `send_message` succeeds
# locally whether or not anything is listening, and `get_messages` reports
# what this installation stored -- so "the phone sent a message" has to be
# read off the FAR end, and "the phone received one" has to be a message this
# end demonstrably wrote. That is this script: it prints what it sees from the
# phone, and it writes one nonce of its own into the group.
#
#   chat-peer.sh <modules-dir> [config-dir]
#
# It stays in the foreground and keeps answering until interrupted, because a
# device run is a person or a script driving a phone next to it.
set -euo pipefail

MODULES=${1:?usage: chat-peer.sh <modules-dir> [config-dir]}
CONFIG=${2:-${TMPDIR:-/tmp}/logos-chat-peer}
LOGOSCORE=${LOGOSCORE:-logoscore}
PRESET=${CHAT_PRESET:-logos.test}

core() { "$LOGOSCORE" --config-dir "$CONFIG" "$@"; }
# The answer half of a `logoscore call`, which wraps every answer in a
# `{module, method, result, status}` envelope.
value() { core call "$@" --json | jq -c '.result'; }
# ...and the answer half of a `result`-returning method, which nests one
# further inside `{success, value, error}`. Separate from `value` on purpose:
# a `result` whose success is false still comes back as a perfectly good JSON
# object, so a caller that used `value` for both would read `null` as an id
# and carry on -- which is how `add_group_member` was handed a whole envelope
# as a convo_id and answered "conversation not found".
result() {
  local method="$2" out
  out=$(value "$@")
  if [ "$(echo "$out" | jq -r '.success')" != true ]; then
    echo "chat_module.$method failed: $(echo "$out" | jq -r '.error')" >&2
    return 1
  fi
  echo "$out" | jq -r '.value'
}

rm -rf "$CONFIG"; mkdir -p "$CONFIG"
core -D -m "$MODULES" > "$CONFIG/daemon.log" 2>&1 &
trap 'kill %1 2>/dev/null || true' EXIT

echo "==> waiting for the daemon"
for _ in $(seq 1 60); do core status >/dev/null 2>&1 && break; sleep 1; done
core status >/dev/null || { echo "the daemon never came up; see $CONFIG/daemon.log" >&2; exit 1; }

# chat_module pulls delivery_module in by its own declared dependency.
core load-module chat_module >/dev/null
result chat_module init "json:{\"delivery_preset\":\"$PRESET\",\"log_level\":\"info\"}" >/dev/null

# DELIVERY FIRST, for the same reason the phone waits: init returns as soon as
# the state is installed and the node joins the network on callbacks after
# that. A group invite that arrives while this end is still `initialising` is
# not processed, and the phone waits out its commit bound for nothing.
echo "==> waiting for the delivery node"
for _ in $(seq 1 120); do
  state=$(value chat_module status | jq -r '.delivery_state')
  [ "$state" = online ] && break
  [ "$state" = error ] && { echo "delivery failed: $(value chat_module status)" >&2; exit 1; }
  sleep 1
done
[ "$state" = online ] || { echo "the delivery node never came online (last: $state)" >&2; exit 1; }

address=$(value chat_module get_address | jq -r '.')
echo
echo "chat peer ready. Hand this to the phone:"
echo "  --chat-peer $address"
echo

# One answer per conversation, and only after the phone has written into it:
# writing first would be this end opening the exchange, and the criterion is
# that the PHONE sends and receives. Answering twice would also make the
# phone's "a message that is not mine" ambiguous about which one it saw.
answered=""
echo "==> watching for a group invite (^C to stop)"
while :; do
  for convo in $(value chat_module list_conversations | jq -r '.[].convo_id'); do
    messages=$(value chat_module get_messages "$convo")
    inbound=$(echo "$messages" | jq -r '[.[] | select(.from_self == false)] | length')
    [ "$inbound" -gt 0 ] || continue
    case " $answered " in *" $convo "*) continue ;; esac
    echo "$messages" | jq -r --arg c "$convo" \
      '.[] | select(.from_self == false) | "  from the phone in \($c): \(.content)"'
    nonce="desktop-$RANDOM$RANDOM"
    result chat_module send_message "$convo" "$nonce" >/dev/null
    echo "  answered in $convo: $nonce"
    answered="$answered $convo"
  done
  sleep 2
done
