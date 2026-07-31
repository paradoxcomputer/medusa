#!/usr/bin/env bash
# Build the Medusa custom wallet + L1 sequencer, reproducibly, from a CLONE of the
# upstream logos-execution-zone "rc5" tag + our patches - no machine-local checkout needed.
#
#   deployed source  ==  logos-execution-zone @ v0.2.0  +  wallet/patches-v020/*.patch
#
# Patch (rc5 series) reconstructs the deployed wallet customisations:
#   0001 encrypted storage (Argon2id + AES-256-GCM) + account list --json + mnemonic/key export
#   0002 pyo3/keycard_wallet behind an opt-in "keycard" feature, so the wallet no longer links
#        the build host's libpython (medusa#1). The feature is OFF by default, so the plain
#        `cargo build` below already builds it out - no extra flags needed here.
# (rc4's 0003/0006 are obsolete on rc5 - logos-blockchain is already pinned and token ops
#  already print hashes; 0004/0005 demand-driven sequencer are deferred.)
#
# Override the source via env if you already have a checkout:
#   LEZ_SRC=~/some/logos-execution-zone bash wallet/build.sh
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"

REPO="${LEZ_REPO:-https://github.com/logos-blockchain/logos-execution-zone.git}"
BASE_REV="${LEZ_BASE_REV:-v0.2.0}"              # tag a58fbce - what testnet.lez.logos.co runs
# The patch series must match the base tag. v0.2.0 is the default; the superseded rc5 series is
# kept alongside it and can still be built explicitly:
#   LEZ_BASE_REV=v0.2.0-rc5 LEZ_PATCH_DIR=patches-rc5 bash wallet/build.sh   # the old engine
# v0.2.0 is what the official public testnet (testnet.lez.logos.co) runs; rc5 is what the Paradox
# zone runs. Wallet and zone must move together - the risc0 program ImageIDs differ between them.
PATCH_DIR="${LEZ_PATCH_DIR:-patches-v020}"
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
echo ">> applying $(ls -1 "$HERE/$PATCH_DIR"/*.patch | wc -l) medusa patches ($PATCH_DIR) onto $BASE_REV"
git am "$HERE/$PATCH_DIR"/*.patch

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

# 4) medusa-faucet-client - operator client for the medusa_faucet LEZ program
#    (deploy / init-treasury / claim). Builds against the SAME patched tree via
#    path-deps, so it inherits the pyo3-free wallet crate (medusa#1).
echo ">> building medusa-faucet-client (release)…"
cargo build --release --manifest-path "$HERE/faucet/client/Cargo.toml"
echo ">> medusa-faucet-client:          $HERE/faucet/client/target/release/medusa-faucet-client"
#    The faucet GUEST (the on-chain risc0 program) is NOT built here: the reproducible
#    build runs in docker via cargo-risczero and is an explicit operator step:
#        cargo risczero build --manifest-path wallet/faucet/guest/Cargo.toml
#    The resulting .bin path + its ImageID are recorded in wallet/faucet/README.md,
#    and deploying it to any zone is likewise a separate, explicit operator step.

# 4b) medusa-l1 - phase-1 Bedrock L1 client (node-side custody; read-only by default,
#     writes gated behind MEDUSA_L1_ALLOW_WRITES=1). Builds against the pinned
#     logos-blockchain client crates (the same git rev the LEZ tree pins), NOT the
#     patched LEZ tree - it talks HTTP to a Bedrock node, not to a zone sequencer.
echo ">> building medusa-l1 (release)…"
cargo build --release --manifest-path "$HERE/l1/Cargo.toml"
echo ">> medusa-l1:                    $HERE/l1/target/release/medusa-l1"

# 5) diaphani-forward - the Tor TCP-forwarder for the "Paradox Computer · Tor" zone. It lives in
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
