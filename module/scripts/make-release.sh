#!/usr/bin/env bash
# Build a SELF-CONTAINED, downloadable Medusa release for Logos Basecamp - the end user needs
# NO Nix, Rust, or Tor. Produces dist/medusa-wallet-<version>-<platform>.tar.gz containing:
#
#   modules/medusa_core/  medusa_core_plugin.so + manifest.json + bin/<all runtime binaries>
#   plugins/medusa_ui/    the QML wallet UI
#   install.sh            copies both into the Basecamp data dir(s) (no build tools)
#   INSTALL.txt           one-paragraph instructions
#
# The module finds its binaries in modules/medusa_core/bin/ at runtime (dladdr → resolveBin),
# so the unpacked bundle just works.
#
# BUILDER prerequisites (run once, on the build machine):
#   • Nix (with flakes)           - builds the core .so
#   • bash wallet/build.sh        - builds the wallet CLI + both sequencers (+ diaphani-forward)
# Override binary sources with $WALLET_BUILD / $FWD_BUILD / $TOR_SRC.
set -euo pipefail
REPO="$(cd "$(dirname "$0")/.." && pwd)"      # module/
ROOT="$(cd "$REPO/.." && pwd)"                # medusa/
NIX="${NIX:-/nix/var/nix/profiles/default/bin/nix}"
VERSION="${VERSION:-0.2.0}"
PLATFORM="${PLATFORM:-linux-amd64}"
WB="${WALLET_BUILD:-$ROOT/wallet/.lez-build/target/release}"
FWD_BUILD="${FWD_BUILD:-$ROOT/wallet/.diaphani-build/target/release}"

NAME="medusa-wallet-$VERSION"
WORK="$(mktemp -d)"; STAGE="$WORK/$NAME"
MDIR="$STAGE/modules/medusa_core"; PDIR="$STAGE/plugins/medusa_ui"; BINDIR="$MDIR/bin"
mkdir -p "$BINDIR" "$PDIR/qml" "$PDIR/icons" "$PDIR/fonts"
trap 'rm -rf "$WORK"' EXIT

need() { [ -x "$1" ] || { echo "ERROR: missing $2 - ($1)"; echo "       run: bash $ROOT/wallet/build.sh"; exit 1; }; }

# ── 1. core plugin .so (extracted from the Nix-built .lgx) ──
echo "[1/5] building core module (.lgx) via Nix…"
( cd "$REPO" && "$NIX" build .#packages.x86_64-linux.lgx )
REPO="$REPO" OUT="$MDIR/medusa_core_plugin.so" python3 - <<'PY'
import tarfile, os
lgx = os.path.join(os.environ["REPO"], "result", "logos-medusa_core-module-lib.lgx")
with tarfile.open(lgx) as t:
    so = next(m for m in t.getmembers() if m.name.endswith(".so"))
    open(os.environ["OUT"], "wb").write(t.extractfile(so).read())
print("      extracted", so.name)
PY

# ── 2. runtime binaries → modules/medusa_core/bin/ (the dir resolveBin()/dladdr looks in) ──
echo "[2/5] bundling runtime binaries…"
install -m755 "$REPO/scripts/wallet-wrapper"     "$BINDIR/wallet"            # JSON wrapper (calls wallet-lez next to it)
need "$WB/wallet" "wallet binary";               install -m755 "$WB/wallet"               "$BINDIR/wallet-lez"
need "$WB/sequencer_service" "sequencer";        install -m755 "$WB/sequencer_service"    "$BINDIR/sequencer_service"
need "$WB/sequencer_service_l1" "L1 sequencer";  install -m755 "$WB/sequencer_service_l1" "$BINDIR/sequencer_service_l1"
install -m755 "$REPO/scripts/medusa-tor-monitor" "$BINDIR/medusa-tor-monitor"
TOR="${TOR_SRC:-$(command -v tor || true)}"
if [ -n "$TOR" ] && [ -x "$TOR" ]; then install -m755 "$(readlink -f "$TOR")" "$BINDIR/medusa-tor"
else echo "      (i) no tor to bundle (set TOR_SRC) - the Paradox·Tor zone won't work"; fi
if [ -x "$FWD_BUILD/diaphani-forward" ]; then install -m755 "$FWD_BUILD/diaphani-forward" "$BINDIR/diaphani-forward"
else echo "      (i) diaphani-forward not bundled (only the Paradox·Tor zone needs it)"; fi

# ── 3. core manifest (no integrity hashes - a drop-in install doesn't verify them) ──
echo "[3/5] writing manifests + UI…"
cat > "$MDIR/manifest.json" <<JSON
{
  "author": "Paradox Computer", "category": "blockchain", "dependencies": [],
  "description": "Medusa - privacy wallet core (accounts, faucet, shield/deshield, encrypted backup)",
  "icon": "",
  "main": { "linux-amd64": "medusa_core_plugin.so", "linux-amd64-dev": "medusa_core_plugin.so",
            "linux-x86_64-dev": "medusa_core_plugin.so" },
  "manifestVersion": "0.2.0", "name": "medusa_core", "type": "core", "version": "$VERSION"
}
JSON
echo "$PLATFORM" > "$MDIR/variant"

# ── 4. UI module ──
cp "$REPO/plugins/medusa_ui/qml/Main.qml"   "$PDIR/qml/Main.qml"
cp "$REPO/plugins/medusa_ui/fonts/"*.ttf    "$PDIR/fonts/"        2>/dev/null || true
cp "$REPO/plugins/medusa_ui/icons/"*.png    "$PDIR/icons/"        2>/dev/null || true
cp "$REPO/plugins/medusa_ui/icons/"*.svg    "$PDIR/icons/"        2>/dev/null || true
cp "$REPO/plugins/medusa_ui/metadata.json"  "$PDIR/metadata.json" 2>/dev/null || true
cat > "$PDIR/manifest.json" <<JSON
{
  "author": "Paradox Computer", "category": "blockchain", "dependencies": ["medusa_core"],
  "description": "Medusa wallet UI", "icon": "icons/medusa-icon.png", "main": {},
  "manifestVersion": "0.2.0", "name": "medusa_ui", "type": "ui_qml", "version": "$VERSION",
  "view": "qml/Main.qml"
}
JSON
echo "$PLATFORM" > "$PDIR/variant"

# ── 5. installer + readme + tarball ──
echo "[4/5] writing installer…"
cat > "$STAGE/install.sh" <<'INST'
#!/usr/bin/env bash
# Install Medusa into Logos Basecamp - copies the two pre-built modules into your Basecamp
# data dir(s). No build tools required.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOTS=()
[ -n "${BASECAMP_DATA:-}" ] && ROOTS+=("$BASECAMP_DATA")
ROOTS+=("$HOME/.local/share/Logos/LogosBasecamp" "$HOME/.local/share/Logos/LogosBasecampDev")
installed=0
for BASE in "${ROOTS[@]}"; do
  [ -d "$BASE" ] || continue
  mkdir -p "$BASE/modules" "$BASE/plugins"
  rm -rf "$BASE/modules/medusa_core" "$BASE/plugins/medusa_ui"
  cp -r "$HERE/modules/medusa_core" "$BASE/modules/medusa_core"
  cp -r "$HERE/plugins/medusa_ui"   "$BASE/plugins/medusa_ui"
  echo "  installed → $BASE"
  installed=1
done
[ "$installed" = 1 ] || { echo "No Basecamp data dir found. Set BASECAMP_DATA=/path/to/LogosBasecamp and re-run."; exit 1; }
for c in "$HOME/.cache/Logos/"*/qmlcache; do [ -d "$c" ] && rm -rf "$c"; done
echo "Done. Restart Logos Basecamp - open \"Medusa\" → Create wallet."
INST
chmod +x "$STAGE/install.sh"

cat > "$STAGE/INSTALL.txt" <<TXT
Medusa $VERSION - Logos Basecamp privacy wallet ($PLATFORM)

Install:
  1. Make sure Logos Basecamp is installed.
  2. Run:  ./install.sh
  3. Restart Basecamp, then open "Medusa" and create your wallet.

This bundle is self-contained: the wallet CLI, sequencer, and Tor all ship inside
modules/medusa_core/bin/, so no build tools are needed.

Uninstall: delete modules/medusa_core and plugins/medusa_ui from your Basecamp data dir.
TXT

echo "[5/5] packaging…"
mkdir -p "$ROOT/dist"
OUT="$ROOT/dist/$NAME-$PLATFORM.tar.gz"
tar czf "$OUT" -C "$WORK" "$NAME"
echo "→ $OUT  ($(du -h "$OUT" | cut -f1))"
echo "   contents:"; tar tzf "$OUT" | sed 's/^/     /' | head -40
