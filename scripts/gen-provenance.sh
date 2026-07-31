#!/usr/bin/env bash
# Emit a PROVENANCE record for a Medusa release.
#
# Identifying which engine a published .lgx actually contained used to require fingerprinting a
# risc0 circuit source path inside the binary, because nothing in the release recorded the
# upstream revision, the patch series or any hash. This writes that record next to the artifacts:
#
#   * the pinned logos-execution-zone commit and the diaphani commit (read back out of
#     wallet/build.sh, so the two can never disagree)
#   * the sha256 of every patch applied on top of that commit
#   * the sha256 of every engine binary and every .lgx shipped
#   * the toolchain versions and the SOURCE_DATE_EPOCH / RUSTFLAGS used
#
# Usage:
#   bash scripts/gen-provenance.sh <output-file> [artifact-or-directory ...]
#
# Optional env:
#   LEZ  - dir holding wallet / sequencer_service / sequencer_service_l1 (release-*.yml set it)
#   DIA  - dir holding diaphani-forward
#   TOR_BIN - the tor binary bundled as bin/medusa-tor, if any
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${1:-$ROOT/PROVENANCE.txt}"
[ "$#" -ge 1 ] && shift || true

hash_of() {                                     # $1=path -> "<sha256>  <basename>"
  [ -f "$1" ] || return 0
  printf '  %s  %s\n' "$(sha256sum "$1" | cut -d' ' -f1)" "$(basename "$1")"
}

tool_version() {                                # $1=command, rest=version args
  cmd="$1"; shift
  if command -v "$cmd" >/dev/null 2>&1; then
    printf '  %-16s %s\n' "$cmd" "$("$cmd" "$@" 2>&1 | head -1)"
  else
    printf '  %-16s (not installed)\n' "$cmd"
  fi
}

# The pins come from build.sh itself rather than being restated here: a provenance file that can
# drift from the thing it describes is worse than none.
pins="$(MEDUSA_PRINT_PINS=1 bash "$ROOT/wallet/build.sh")"
lez_repo=$(printf '%s\n' "$pins" | grep '^LEZ_REPO='        | cut -d= -f2-)
lez_sha=$(printf  '%s\n' "$pins" | grep '^LEZ_SHA='         | cut -d= -f2-)
patch_dir=$(printf '%s\n' "$pins" | grep '^LEZ_PATCH_DIR='  | cut -d= -f2-)
dia_repo=$(printf '%s\n' "$pins" | grep '^DIAPHANI_REPO='   | cut -d= -f2-)
dia_sha=$(printf  '%s\n' "$pins" | grep '^DIAPHANI_SHA='    | cut -d= -f2-)
sde=$(printf      '%s\n' "$pins" | grep '^SOURCE_DATE_EPOCH=' | cut -d= -f2-)
rflags=$(printf   '%s\n' "$pins" | grep '^RUSTFLAGS='       | cut -d= -f2-)

medusa_sha=$(git -C "$ROOT" rev-parse HEAD 2>/dev/null || echo "(not a git checkout)")
medusa_desc=$(git -C "$ROOT" describe --tags --always --dirty 2>/dev/null || echo "(unknown)")
medusa_dirty=$(git -C "$ROOT" status --porcelain 2>/dev/null | wc -l)

{
  echo "Medusa release provenance"
  echo "========================="
  echo
  echo "medusa"
  echo "  repo commit       $medusa_sha"
  echo "  describe          $medusa_desc"
  echo "  uncommitted files $medusa_dirty  (must be 0 for a release build)"
  echo
  echo "engine source (built by wallet/build.sh)"
  echo "  upstream repo     $lez_repo"
  echo "  upstream commit   $lez_sha"
  echo "  patch series      wallet/$patch_dir"
  for p in "$ROOT/wallet/$patch_dir"/*.patch; do hash_of "$p"; done
  echo
  echo "  diaphani repo     $dia_repo"
  echo "  diaphani commit   $dia_sha"
  echo
  echo "build environment"
  printf '  %-17s %s\n' "SOURCE_DATE_EPOCH" "$sde"
  printf '  %-17s %s\n' "RUSTFLAGS" "$rflags"
  tool_version rustc --version
  tool_version cargo --version
  tool_version nix --version
  if command -v cargo >/dev/null 2>&1; then
    printf '  %-16s %s\n' "cargo-risczero" "$(cargo risczero --version 2>&1 | head -1)"
  fi
  if [ -n "${TOR_BIN:-}" ] && [ -x "${TOR_BIN:-}" ]; then
    printf '  %-16s %s\n' "tor" "$("$TOR_BIN" --version 2>&1 | head -1)"
    hash_of "$TOR_BIN"
  fi
  echo
  echo "engine binaries"
  for b in wallet sequencer_service sequencer_service_l1; do hash_of "${LEZ:-$ROOT/wallet/.lez-build/target/release}/$b"; done
  hash_of "${DIA:-$ROOT/wallet/.diaphani-build/target/release}/diaphani-forward"
  echo
  echo "artifacts"
  for a in "$@"; do
    if [ -d "$a" ]; then
      find "$a" -type f | sort | while read -r f; do hash_of "$f"; done
    else
      hash_of "$a"
    fi
  done
  echo
  echo "To reproduce: check out medusa at $medusa_sha and run"
  echo "  SOURCE_DATE_EPOCH=$sde bash wallet/build.sh"
  echo "build.sh asserts the two upstream commits above before it builds anything, so a moved tag"
  echo "or an unreachable pin aborts instead of substituting other source. The engine binaries"
  echo "should then match the sha256 values above; the .lgx packages will NOT, because the nix"
  echo "module build and lgx packaging are not yet byte-reproducible (see README.md)."
} > "$OUT"

echo ">> wrote $OUT"
cat "$OUT"
