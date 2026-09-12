# The two desktop peers

A phone cannot discover a laptop, and neither of the networking criteria can
be answered from the phone alone: `send_message` succeeds locally whether or
not anything is listening, and a gossipsub publish is a local call too. Both
halves have to be visible from the DESKTOP end, and these two scripts are that
end.

Each is a `logoscore` daemon over this workspace's own module builds, with its
own `--config-dir` — two daemons sharing one would drive each other. The daemon
itself, and the two ways to read an answer out of it, are `_peer.sh`, which
both source.

```bash
# One modules directory both peers load from. `--install` is the output that
# lays a module out the way logoscore scans for: modules/<name>/<name>_plugin.*
# beside its manifest. The plain `.#<repo>` output is a flat lib/ and the
# daemon will not find it.
mkdir -p /tmp/peers/modules
for r in logos-chat-module logos-delivery-module logos-libp2p-module logos-capability-module; do
  out=$(nix build --no-link --print-out-paths ".#$r--install")
  cp -RL "$out"/modules/* /tmp/peers/modules/
done
chmod -R u+w /tmp/peers/modules

export LOGOSCORE=$(nix build --no-link --print-out-paths '.#logos-logoscore-cli')/bin/logoscore

./libp2p-peer.sh /tmp/peers/modules /tmp/peers/libp2p   # prints --peer
./chat-peer.sh   /tmp/peers/modules /tmp/peers/chat     # prints --chat-peer
```

Each prints the argument the phone needs and then stays in the foreground:

```bash
ws run logos-basecamp --target ios-arm64 --app shell \
  --bundle capability_module,libp2p_module,delivery_module,chat_module \
  -- --peer /ip4/192.168.1.158/tcp/9500/p2p/12D3KooW... \
     --chat-peer 015ccf0b561496fa...
```

## What each proves, and from which end

| | the phone says | this end says |
|---|---|---|
| gossipsub, phone → desktop | `published 'phone-<nonce>'` | `from the phone: phone-<nonce>` |
| gossipsub, desktop → phone | `gossipsub message from the desktop peer: 'desktop-hello-N'` | it published that N |
| chat, phone → desktop | `sent 'phone-<nonce>' to the group` | `from the phone in <convo>: phone-<nonce>` |
| chat, desktop → phone | `message from the desktop peer: 'desktop-<nonce>'` | `answered in <convo>: desktop-<nonce>` |

The chat peer answers a conversation **once**, and only after the phone has
written into it. Writing first would make this end the one that opened the
exchange, and the criterion is that the phone sends and receives; answering
twice would make the phone's "a message that is not mine" ambiguous about
which one it saw.
