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

# shellcheck source=mobile/liblogos-smoke/desktop-peers/_peer.sh
source "$(dirname "$0")/_peer.sh"

start_peer_daemon

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
