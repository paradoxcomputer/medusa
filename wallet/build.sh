#!/usr/bin/env bash
# Build the Medusa custom wallet + L1 sequencer, reproducibly, from a CLONE of the
# upstream logos-execution-zone "rc5" tag + our patches - no machine-local checkout needed.
#
#   deployed source  ==  logos-execution-zone @ v0.2.0-rc5  +  wallet/patches-rc5/*.patch
#
# Patch (rc5 series) reconstructs the deployed wallet customisations:
#   0001 encrypted storage (Argon2id + AES-256-GCM) + account list --json + mnemonic/key export
# (rc4's 0003/0006 are obsolete on rc5 - logos-blockchain is already pinned and token ops
#  already print hashes; 0004/0005 demand-driven sequencer are deferred.)
#
# Override the source via env if you already have a checkout:
#   LEZ_SRC=~/some/logos-execution-zone bash wallet/build.sh
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"

REPO="${LEZ_REPO:-https://github.com/logos-blockchain/logos-execution-zone.git}"
BASE_REV="${LEZ_BASE_REV:-v0.2.0-rc5}"          # tag 27360cb - the rc5 base the patches apply onto
SRC="${LEZ_SRC:-$HERE/.lez-build}"              # default: a repo-local (gitignored) clone

# 1) obtain the source by CLONE (reproducible) unless an existing checkout was provided
if [ ! -e "$SRC/.git" ]; then
  echo ">> cloning $REPO -> $SRC"
  git clone "$REPO" "$SRC"
fi
cd "$SRC"
git fetch --tags --force origin 2>/dev/null || true

# 2) reset to the rc4 base + (re)apply the medusa patch series
git am --abort 2>/dev/null || true
git checkout -f -B medusa-build "$BASE_REV"
echo ">> applying $(ls -1 "$HERE"/patches-rc5/*.patch | wc -l) medusa patches onto $BASE_REV"
git am "$HERE"/patches-rc5/*.patch

# 3) build the binaries from the patched tree. The sequencer has TWO builds from the same
#    crate (one cargo output path), so build sequentially and copy each out:
#      - L1 build (diaphani zone): settles to the Bedrock L1 via node_url      -> sequencer_service_l1
#      - standalone build (devnet zone): L1-free local sandbox (ignores L1)    -> sequencer_service
echo ">> building wallet + L1 sequencer (release)…"
cargo build --release -p wallet -p sequencer_service
cp -f target/release/sequencer_service target/release/sequencer_service_l1
echo ">> building standalone (L1-free) sequencer (release, --features standalone)…"
cargo build --release -p sequencer_service --features standalone
echo ">> wallet:                       $SRC/target/release/wallet"
echo ">> sequencer (standalone/devnet): $SRC/target/release/sequencer_service"
echo ">> sequencer (L1/diaphani):       $SRC/target/release/sequencer_service_l1"

# 4) diaphani-forward - the Tor TCP-forwarder for the "Paradox Computer · Tor" zone. It lives in
#    the separate Diaphani project (Apache-2.0/MIT), so build it from a pinned clone like the
#    wallet. Only needed for the Paradox·Tor zone; the default devnet zone needs no forward.
#    Override: DIAPHANI_SRC=~/dedicated-checkout (reused as-is) or DIAPHANI_REV=<tag/rev>.
DIA_REPO="${DIAPHANI_REPO:-https://github.com/paradoxcomputer/diaphani.git}"
DIA_REV="${DIAPHANI_REV:-704192f}"               # pin to a Diaphani tag once it's released
DIA_SRC="${DIAPHANI_SRC:-$HERE/.diaphani-build}"  # default: a repo-local (gitignored) clone
if [ ! -d "$DIA_SRC/.git" ]; then
  echo ">> cloning $DIA_REPO -> $DIA_SRC"
  git clone "$DIA_REPO" "$DIA_SRC"
fi
(
  cd "$DIA_SRC"
  git fetch --all --tags 2>/dev/null || true
  git checkout -f "$DIA_REV" 2>/dev/null || echo ">> (pin $DIA_REV unavailable - building $DIA_SRC HEAD)"
  echo ">> building diaphani-forward (release)…"
  cargo build --release -p diaphani-forward
)
echo ">> diaphani-forward:             $DIA_SRC/target/release/diaphani-forward"
