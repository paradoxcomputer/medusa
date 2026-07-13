#!/usr/bin/env bash
# Install the medusa-wallet module to LogosBasecamp for dev testing - one command.
#
# Installs BOTH parts of the module:
#   • medusa_core  (core C++ backend, built via Nix)  → <data>/modules/medusa_core/
#   • medusa_ui     (QML view, copied from source)      → <data>/plugins/medusa_ui/
#
# It installs into EVERY Basecamp data root it knows about, because different
# builds use different ones: the release/AppImage build uses "LogosBasecamp",
# the Nix dev build appends "Dev" → "LogosBasecampDev". (Override/extend with
# $BASECAMP_DATA.) That way whichever Basecamp you launch picks the module up.
#
# Usage:
#   ./scripts/install-dev.sh             # build core + install both modules
#   ./scripts/install-dev.sh --qml-only  # re-copy the QML view only (instant)
#   ./scripts/install-dev.sh --launch    # also restart any running Basecamp

set -euo pipefail
REPO="$(cd "$(dirname "$0")/.." && pwd)"
NIX="${NIX:-/nix/var/nix/profiles/default/bin/nix}"

ROOTS=()
[[ -n "${BASECAMP_DATA:-}" ]] && ROOTS+=("$BASECAMP_DATA")
ROOTS+=("$HOME/.local/share/Logos/LogosBasecamp" "$HOME/.local/share/Logos/LogosBasecampDev")

QML_ONLY=0; LAUNCH=0
for arg in "$@"; do
    [[ "$arg" == "--qml-only" ]] && QML_ONLY=1
    [[ "$arg" == "--launch"   ]] && LAUNCH=1
done

# Remove a dir even if it holds read-only files (Nix-store copies are read-only).
forceclean() { [[ -e "$1" ]] && { chmod -R u+w "$1" 2>/dev/null || true; rm -rf "$1"; }; return 0; }

# ── Build the core .so once (shared across all data roots) ───────────────────
SO=""
if [[ $QML_ONLY -eq 0 ]]; then
    echo "Building core module via Nix…"
    cd "$REPO"
    "$NIX" build .#packages.x86_64-linux.lgx
    SO="$(mktemp -d)/medusa_core_plugin.so"
    REPO="$REPO" OUT="$SO" python3 - << 'PYEOF'
import tarfile, os
lgx = os.path.join(os.environ["REPO"], "result", "logos-medusa_core-module-lib.lgx")
with tarfile.open(lgx) as t:
    so = next(m for m in t.getmembers() if m.name.endswith(".so"))
    open(os.environ["OUT"], "wb").write(t.extractfile(so).read())
print(f"  extracted {so.name}")
PYEOF
fi

write_core_manifest() {
    cat > "$1" << 'MANIFEST'
{
  "author": "Paradox Computer", "category": "blockchain", "dependencies": [],
  "description": "Medusa - privacy wallet core (accounts, faucet, shield/deshield, encrypted backup)",
  "icon": "",
  "main": { "linux-amd64": "medusa_core_plugin.so", "linux-amd64-dev": "medusa_core_plugin.so",
            "linux-x86_64-dev": "medusa_core_plugin.so", "darwin-arm64": "medusa_core_plugin.dylib" },
  "manifestVersion": "0.2.0", "name": "medusa_core", "type": "core", "version": "0.2.0"
}
MANIFEST
}
write_ui_manifest() {
    cat > "$1" << 'MANIFEST'
{
  "author": "Paradox Computer", "category": "blockchain", "dependencies": ["medusa_core"],
  "description": "Medusa wallet UI", "icon": "icons/medusa-icon.png", "main": {},
  "manifestVersion": "0.2.0", "name": "medusa_ui", "type": "ui_qml", "version": "0.2.0",
  "view": "qml/Main.qml"
}
MANIFEST
}

install_into() {
    local BASE="$1" MDIR="$1/modules/medusa_core" PDIR="$1/plugins/medusa_ui"
    # The Nix DEV build (LogosBasecamp**Dev** root) only loads "-dev" variants since
    # Basecamp 0.2.1 (supported: linux-amd64-dev/linux-x86_64-dev); the release/AppImage
    # build (plain "LogosBasecamp" root) loads the plain "linux-amd64" variant. Mark each
    # root for the runtime that reads it, or the module is silently "not loadable".
    local VARIANT="linux-amd64"
    [[ "$BASE" == *Dev ]] && VARIANT="linux-amd64-dev"
    if [[ $QML_ONLY -eq 0 ]]; then
        forceclean "$MDIR"; mkdir -p "$MDIR"
        install -m755 "$SO" "$MDIR/medusa_core_plugin.so"
        write_core_manifest "$MDIR/manifest.json"
        echo "$VARIANT" > "$MDIR/variant"
    fi
    forceclean "$PDIR"; mkdir -p "$PDIR/qml/fonts" "$PDIR/qml/icons" "$PDIR/icons"
    cp "$REPO/plugins/medusa_ui/qml/Main.qml"     "$PDIR/qml/Main.qml"
    # fonts AND ui icons (key/shield/logo) live UNDER qml/ so the lgx builder (which packs the
    # qml view dir) bundles them; Main.qml loads them as "fonts/…" / "icons/…" relative to qml/.
    cp "$REPO/plugins/medusa_ui/qml/fonts/"*.ttf  "$PDIR/qml/fonts/"      2>/dev/null || true
    cp "$REPO/plugins/medusa_ui/qml/icons/"*      "$PDIR/qml/icons/"      2>/dev/null || true
    cp "$REPO/plugins/medusa_ui/icons/"*.png      "$PDIR/icons/"          2>/dev/null || true
    cp "$REPO/plugins/medusa_ui/icons/"*.svg      "$PDIR/icons/"          2>/dev/null || true
    cp "$REPO/plugins/medusa_ui/metadata.json"    "$PDIR/metadata.json"    2>/dev/null || true
    write_ui_manifest "$PDIR/manifest.json"
    echo "$VARIANT" > "$PDIR/variant"
    echo "  installed → $BASE ($VARIANT)"
}

for BASE in "${ROOTS[@]}"; do install_into "$BASE"; done

# Stage the bundled binaries the plugin auto-launches (local + diaphani modes).
# Installed to ~/.local/bin (where seqPath() looks), next to wallet/wallet-lez.
#   sequencer_service     - standalone build (devnet/testnet, L1-free)
#   sequencer_service_l1  - NON-standalone build (diaphani: talks to the real Bedrock L1)
#   diaphani-forward      - Tor tunnel to the node's .onion (diaphani mode)
if [[ $QML_ONLY -eq 0 ]]; then
    # Wallet CLI: the plugin invokes ~/.local/bin/wallet (the JSON-normalizing wrapper), which
    # in turn calls ~/.local/bin/wallet-lez (the built wallet). Stage BOTH or every wallet op
    # fails "wallet CLI not found".
    install -m755 "$REPO/scripts/wallet-wrapper" "$HOME/.local/bin/wallet" \
        && echo "  staged wallet wrapper → ~/.local/bin/wallet"
    WALLET_SRC="${WALLET_SRC:-$REPO/../wallet/.lez-build/target/release/wallet}"
    if [[ -x "$WALLET_SRC" ]]; then
        install -m755 "$WALLET_SRC" "$HOME/.local/bin/wallet-lez"
        echo "  staged wallet binary → ~/.local/bin/wallet-lez"
    else
        echo "  !! wallet binary not found at \$WALLET_SRC ($WALLET_SRC)"
        echo "     build it reproducibly: bash $REPO/../wallet/build.sh"
    fi
    SEQ_SRC="${SEQ_SRC:-$REPO/../wallet/.lez-build/target/release/sequencer_service}"
    if [[ -x "$SEQ_SRC" ]]; then
        install -m755 "$SEQ_SRC" "$HOME/.local/bin/sequencer_service"
        echo "  staged sequencer → ~/.local/bin/sequencer_service"
    else
        echo "  !! standalone sequencer not found at \$SEQ_SRC ($SEQ_SRC)"
        echo "     build it reproducibly: bash $REPO/../wallet/build.sh   (builds wallet + BOTH sequencers)"
    fi
    # L1 (non-standalone) sequencer for the diaphani zone - build.sh produces it alongside the
    # standalone (sequencer_service_l1). The standalone (sequencer_service) backs the devnet zone.
    SEQ_L1_SRC="${SEQ_L1_SRC:-$REPO/../wallet/.lez-build/target/release/sequencer_service_l1}"
    [[ -x "$SEQ_L1_SRC" ]] && install -m755 "$SEQ_L1_SRC" "$HOME/.local/bin/sequencer_service_l1" \
        && echo "  staged L1 sequencer → ~/.local/bin/sequencer_service_l1" \
        || echo "  (i) diaphani L1 sequencer not staged - run: bash $REPO/../wallet/build.sh"
    # diaphani-forward (Tor tunnel for the "Paradox Computer · Tor" zone) - from the Diaphani
    # project; wallet/build.sh clones+builds it into wallet/.diaphani-build. Only the Paradox·Tor
    # zone needs it, so its absence is non-fatal (the default devnet zone needs no forward).
    FWD_SRC="${FWD_SRC:-$REPO/../wallet/.diaphani-build/target/release/diaphani-forward}"
    if [[ -x "$FWD_SRC" ]]; then
        install -m755 "$FWD_SRC" "$HOME/.local/bin/diaphani-forward"
        echo "  staged diaphani-forward → ~/.local/bin/diaphani-forward"
    else
        echo "  (i) diaphani-forward not staged (only the Paradox·Tor zone needs it) - run: bash $REPO/../wallet/build.sh"
    fi
    # Bundle Tor so users need NO external Tor - the module launches this on a private SOCKS
    # port (9250). Prefer an explicit $TOR_SRC, else copy the system tor binary.
    TOR_SRC="${TOR_SRC:-$(command -v tor || true)}"
    if [[ -n "$TOR_SRC" && -x "$TOR_SRC" ]]; then
        install -m755 "$(readlink -f "$TOR_SRC")" "$HOME/.local/bin/medusa-tor" \
            && echo "  staged Tor → ~/.local/bin/medusa-tor ($("$TOR_SRC" --version 2>/dev/null | head -1))"
    else
        echo "  !! tor not found to bundle - install tor or set \$TOR_SRC (Paradox·Tor zone needs it)"
    fi
    # Tor control-port monitor (real onion-connection stages for the connect progress bar).
    install -m755 "$REPO/scripts/medusa-tor-monitor" "$HOME/.local/bin/medusa-tor-monitor" 2>/dev/null \
        && echo "  staged tor monitor → ~/.local/bin/medusa-tor-monitor" || true
fi
# Clear every Basecamp QML cache so the new view is picked up.
for c in "$HOME/.cache/Logos/"*/qmlcache; do [[ -d "$c" ]] && rm -rf "$c"; done
echo "Done - installed into: ${ROOTS[*]/#$HOME/~}"

if [[ $LAUNCH -eq 1 ]]; then
    echo "Restarting Basecamp…"
    pkill -f "LogosBasecamp" 2>/dev/null || true
    pkill -f "logos_host"    2>/dev/null || true
    sleep 1
    # Find the Basecamp binary generically: $LOGOS_BASECAMP_BIN, else whatever is on PATH.
    BIN="${LOGOS_BASECAMP_BIN:-$(command -v LogosBasecamp 2>/dev/null || command -v logos-basecamp 2>/dev/null || true)}"
    if [[ -n "$BIN" && -x "$BIN" ]]; then
        [[ -n "${WAYLAND_DISPLAY:-}" && -z "${QT_QPA_PLATFORM:-}" ]] && export QT_QPA_PLATFORM=wayland
        nohup "$BIN" >/tmp/basecamp.log 2>&1 &
        echo "  launched $BIN (log: /tmp/basecamp.log)"
    else
        echo "  Basecamp binary not found - set LOGOS_BASECAMP_BIN=/path/to/LogosBasecamp (or add it to PATH), then re-run with --launch. Otherwise just restart Basecamp yourself."
    fi
else
    echo "Restart Basecamp to load the module (or re-run with --launch)."
fi
