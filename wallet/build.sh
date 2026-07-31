#!/usr/bin/env bash
# Build the Medusa custom wallet + L1 sequencer, reproducibly, from a CLONE of the
# upstream logos-execution-zone at a PINNED commit + our patches - no machine-local checkout
# needed.
#
#   deployed source  ==  logos-execution-zone @ a58fbce2ff48c58b7bb5001b1a27e64b9596ee3a
#                        (upstream tag v0.2.0)         +  wallet/patches-v020/*.patch
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

# ---- pinned supply chain -------------------------------------------------------------------
# Both upstreams are pinned to a FULL 40-char commit id, never to a tag and never to a short
# rev. Tags are mutable refs on third-party GitHub repos, and this script builds wallet-lez:
# the binary that holds the encrypted keystore, derives keys and signs transactions. A moved tag
# here would ship a compromised engine to every user and still pass `lgx verify` and every hash
# check, because the hashes are computed over the compromised build. The tags are kept as
# comments only, for readability.
REPO="${LEZ_REPO:-https://github.com/logos-blockchain/logos-execution-zone.git}"
BASE_REV="${LEZ_BASE_REV:-a58fbce2ff48c58b7bb5001b1a27e64b9596ee3a}"   # == tag v0.2.0
# What the checkout MUST resolve to; asserted in step 2, so a mismatch fails loudly instead of
# building something else. Overriding LEZ_BASE_REV for development therefore also means stating
# the SHA you expect, deliberately.
BASE_SHA="${LEZ_EXPECTED_SHA:-$BASE_REV}"
# The patch series must match the base commit. v0.2.0 is the default; the superseded rc5 series
# is kept alongside it and can still be built explicitly:
#   LEZ_BASE_REV=27360cb7d6ccb2bfbcca7d171bab8a3938490264 \
#     LEZ_PATCH_DIR=patches-rc5 bash wallet/build.sh    # == tag v0.2.0-rc5, the old engine
# v0.2.0 is what the official public testnet (testnet.lez.logos.co) runs; rc5 is what the Paradox
# zone runs. Wallet and zone must move together - the risc0 program ImageIDs differ between them.
PATCH_DIR="${LEZ_PATCH_DIR:-patches-v020}"
SRC="${LEZ_SRC:-$HERE/.lez-build}"              # default: a repo-local (gitignored) clone

# diaphani-forward - the Tor TCP-forwarder built in step 5. Same rule: full commit id, asserted.
DIA_REPO="${DIAPHANI_REPO:-https://github.com/paradoxcomputer/diaphani.git}"
DIA_REV="${DIAPHANI_REV:-704192fd4e472f7fb3fde30c9c069aee565807af}"
DIA_SHA="${DIAPHANI_EXPECTED_SHA:-$DIA_REV}"
DIA_SRC="${DIAPHANI_SRC:-$HERE/.diaphani-build}"  # default: a repo-local (gitignored) clone

# A pin that is not a full commit id is not a pin: short revs are ambiguous and tags move.
assert_full_sha() {                             # $1=label  $2=value
  case "$2" in
    *[!0-9a-f]*|"") sha_ok=no ;;
    *) [ "${#2}" -eq 40 ] && sha_ok=yes || sha_ok=no ;;
  esac
  [ "$sha_ok" = yes ] || {
    echo "ERROR: $1 must be a full 40-char commit sha, got '$2'" >&2; exit 1; }
}
assert_head() {                                 # $1=label  $2=expected sha  (cwd = that repo)
  head_sha="$(git rev-parse HEAD)"
  [ "$head_sha" = "$2" ] || {
    echo "ERROR: $1 checked out $head_sha, expected $2" >&2
    echo "ERROR: refusing to build - the pinned revision moved or was overridden." >&2; exit 1; }
  echo ">> pinned $1 @ $head_sha (verified)"
}
assert_full_sha "LEZ base (LEZ_EXPECTED_SHA)"        "$BASE_SHA"
assert_full_sha "diaphani (DIAPHANI_EXPECTED_SHA)"   "$DIA_SHA"

# ---- reproducibility -----------------------------------------------------------------------
# So a third party can rebuild these binaries and compare them byte for byte:
#   SOURCE_DATE_EPOCH   - deterministic timestamp, defaults to this repo's HEAD commit time.
#   --remap-path-prefix - keeps the builder's $HOME out of the artifacts (the shipped engine
#                         carried 569 absolute paths from one developer's home directory).
# The risc0 guest programs are prebuilt .bin artifacts committed in the LEZ tree and included by
# build_utils::include_artifacts, so remapping the HOST build cannot move any zkVM ImageID.
# MEDUSA_NO_REMAP=1 disables the remaps when you need real paths in a debugger.
export SOURCE_DATE_EPOCH="${SOURCE_DATE_EPOCH:-$(git -C "$HERE/.." log -1 --format=%ct 2>/dev/null || echo 0)}"
CARGO_DIR="${CARGO_HOME:-${HOME:-/nonexistent}/.cargo}"
if [ "${MEDUSA_NO_REMAP:-0}" != "1" ]; then
  RUSTFLAGS="${RUSTFLAGS:-}"
  RUSTFLAGS="${RUSTFLAGS:+$RUSTFLAGS }--remap-path-prefix=$SRC=/medusa/lez"
  RUSTFLAGS="$RUSTFLAGS --remap-path-prefix=$HERE=/medusa/wallet"
  RUSTFLAGS="$RUSTFLAGS --remap-path-prefix=$CARGO_DIR=/medusa/cargo"
  export RUSTFLAGS
fi

# `MEDUSA_PRINT_PINS=1 bash wallet/build.sh` prints the resolved pins and exits without building,
# so scripts/gen-provenance.sh can record them without duplicating them.
if [ "${MEDUSA_PRINT_PINS:-0}" = "1" ]; then
  printf 'LEZ_REPO=%s\nLEZ_SHA=%s\nLEZ_PATCH_DIR=%s\nDIAPHANI_REPO=%s\nDIAPHANI_SHA=%s\nSOURCE_DATE_EPOCH=%s\nRUSTFLAGS=%s\n' \
    "$REPO" "$BASE_SHA" "$PATCH_DIR" "$DIA_REPO" "$DIA_SHA" "$SOURCE_DATE_EPOCH" "${RUSTFLAGS:-}"
  exit 0
fi

# 1) obtain the source by CLONE (reproducible) unless an existing checkout was provided
if [ ! -e "$SRC/.git" ]; then
  echo ">> cloning $REPO -> $SRC"
  git clone "$REPO" "$SRC"
fi
cd "$SRC"
# NOT --force: that flag overwrites a correct local tag with a moved upstream one, which is the
# exact attack this pin exists to stop. A rejected tag update is fine, we check out a SHA below.
git fetch --tags origin 2>/dev/null || true

# 2) reset to the pinned base + (re)apply the medusa patch series
git am --abort 2>/dev/null || true
git checkout -f -B medusa-build "$BASE_REV"
# Hard gate: `git am` succeeding is NOT a check (patches apply by context), so verify the base
# commit itself before a single patch is applied.
assert_head "logos-execution-zone base" "$BASE_SHA"
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
#    Override: DIAPHANI_SRC=~/dedicated-checkout (reused as-is), or DIAPHANI_REV plus a matching
#    DIAPHANI_EXPECTED_SHA. The pins themselves live in the pin block at the top of this file.
if [ ! -d "$DIA_SRC/.git" ]; then
  echo ">> cloning $DIA_REPO -> $DIA_SRC"
  git clone "$DIA_REPO" "$DIA_SRC"
fi
(
  cd "$DIA_SRC"
  git fetch --tags origin 2>/dev/null || true
  # Fail CLOSED. This used to swallow the checkout error and build $DIA_SRC HEAD instead, so
  # anyone able to force-push diaphani and make the pin unreachable got their HEAD bundled into
  # bin/diaphani-forward inside the shipped medusa_core, on both platforms.
  git checkout -f --detach "$DIA_REV"
  assert_head "diaphani" "$DIA_SHA"
  if [ "${MEDUSA_NO_REMAP:-0}" != "1" ]; then
    RUSTFLAGS="${RUSTFLAGS:-}"
    export RUSTFLAGS="${RUSTFLAGS:+$RUSTFLAGS }--remap-path-prefix=$DIA_SRC=/medusa/diaphani"
  fi
  echo ">> building diaphani-forward (release)…"
  cargo build --release -p diaphani-forward
)
echo ">> diaphani-forward:             $DIA_SRC/target/release/diaphani-forward"
