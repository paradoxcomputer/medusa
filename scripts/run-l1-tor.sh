#!/usr/bin/env bash
# Route the LEZ sequencer's L1 (Bedrock) connection over Tor to the shared,
# diaphani-fronted Logos node. Zero code change: it maps a local port to the
# node's .onion via Tor; point the sequencer's bedrock_config.node_url at it.
#
#   bash run-l1-tor.sh            # forwards 127.0.0.1:8081 -> <onion>:80 over Tor
#   then set sequencer node_url = http://127.0.0.1:8081
#
# The onion is read from $DIAPHANI_ONION or ~/.config/medusa-diaphani.onion
# (kept OUT of git - it's a real v3 .onion).
set -euo pipefail
ONION="${DIAPHANI_ONION:-$(cat ~/.config/medusa-diaphani.onion 2>/dev/null)}"
LISTEN="${1:-127.0.0.1:8081}"
SOCKS="${DIAPHANI_SOCKS:-127.0.0.1:9050}"
FWD="${DIAPHANI_FORWARD:-$HOME/Documents/diaphani-publish/target/release/diaphani-forward}"
[ -n "$ONION" ] || { echo "set DIAPHANI_ONION or ~/.config/medusa-diaphani.onion"; exit 1; }
[ -x "$FWD" ]   || { echo "build it: (cd ~/Documents/diaphani-publish && cargo build --release -p diaphani-forward)"; exit 1; }
echo ">> $LISTEN -> $ONION:80 via Tor SOCKS $SOCKS"
exec "$FWD" --onion "$ONION" --listen "$LISTEN" --socks "$SOCKS"
