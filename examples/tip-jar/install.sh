#!/usr/bin/env bash
# Install the Tip Jar sample (a "Connect with Medusa" SDK demo) into Basecamp, next to the
# medusa_core module. The wallet must already be installed (module/scripts/install-dev.sh) -
# its approval sheets are what gate the connect + tip. Bundles the canonical SDK into the plugin.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"     # examples/tip-jar
REPO="$(cd "$HERE/../.." && pwd)"          # repo root
SDK="$REPO/sdk/medusa-connect.js"

[[ -f "$SDK" ]] || { echo "!! missing SDK at $SDK"; exit 1; }

ROOTS=()
[[ -n "${BASECAMP_DATA:-}" ]] && ROOTS+=("$BASECAMP_DATA")
ROOTS+=("$HOME/.local/share/Logos/LogosBasecamp" "$HOME/.local/share/Logos/LogosBasecampDev")

for BASE in "${ROOTS[@]}"; do
    PDIR="$BASE/plugins/tip_jar"
    rm -rf "$PDIR"; mkdir -p "$PDIR/qml"
    cp "$HERE/qml/Main.qml"         "$PDIR/qml/Main.qml"
    cp "$HERE/qml/medusa-logo.png"  "$PDIR/qml/medusa-logo.png"  # Medusa mark for the Connect button
    cp "$SDK"                       "$PDIR/qml/medusa-connect.js"  # bundle the canonical SDK alongside the view
    cp "$HERE/manifest.json" "$PDIR/manifest.json"
    cp "$HERE/metadata.json" "$PDIR/metadata.json"
    # The Nix DEV build (LogosBasecamp**Dev** root) only loads "-dev" variants since
    # Basecamp 0.2.1; the release/AppImage root loads the plain "linux-amd64" variant.
    VARIANT="linux-amd64"; [[ "$BASE" == *Dev ]] && VARIANT="linux-amd64-dev"
    printf '%s' "$VARIANT" > "$PDIR/variant"
    echo "  installed Tip Jar → $BASE ($VARIANT)"
    if [[ ! -d "$BASE/plugins/medusa_ui" ]]; then
        echo "    (note: medusa_ui not found here - install the wallet first: module/scripts/install-dev.sh)"
    fi
    # PREFLIGHT: the installed wallet .so MUST expose the connect API the SDK calls, or every
    # connect returns "invalid response" (method not found). This catches a stale wallet build.
    SO="$BASE/modules/medusa_core/medusa_core_plugin.so"
    if [[ -f "$SO" ]]; then
        # grep -c (not -q) so `strings` reads to completion - `grep -q` closes the pipe early and,
        # under `set -o pipefail`, the SIGPIPE'd `strings` would make this a false negative.
        if [[ "$(strings "$SO" 2>/dev/null | grep -xc connectRequest || true)" -ge 1 ]]; then
            echo "    preflight ✓ wallet exposes connectRequest"
        else
            echo "    !! PREFLIGHT FAIL: $SO has no connectRequest - it predates the connect API."
            echo "       Rebuild the wallet: module/scripts/install-dev.sh   (then restart Basecamp)"
        fi
    fi
done
echo "Done. Launch Basecamp, open Tip Jar → Connect with Medusa, approve in the wallet."
