#!/usr/bin/env bash
# Bootstrap a Medusa token treasury: a dedicated wallet that mints the whitelist
# tokens and holds their supplies. The wrapper's faucet then sends each claimant a
# random amount of each (the user's wallet can't mint tokens it doesn't own).
#
# Run once against a running rc4 sequencer. Writes <treasury>/faucet_tokens.json,
# which the wrapper reads for the whitelist + the per-claim distribution.
set -euo pipefail

WALLET="${WALLET:-$HOME/.local/bin/wallet}"        # the JSON wrapper
export LEE_WALLET_HOME_DIR="${MEDUSA_TREASURY_HOME:-$HOME/.local/share/medusa-treasury}"
export NSSA_WALLET_HOME_DIR="$LEE_WALLET_HOME_DIR"   # rc4 binary compat
export LOGOS_SEQUENCER="${LOGOS_SEQUENCER:-http://127.0.0.1:3071/}"
LAND="${LAND_WAIT:-16}"                            # seconds to wait for a tx to land

# Tokens to mint:  NAME=TOTAL_SUPPLY
declare -A SUPPLY=( [GOLD]=1000000 [SILV]=5000000 [BRNZ]=20000000 )
ORDER=(GOLD SILV BRNZ)

mkdir -p "$LEE_WALLET_HOME_DIR"
# Each network (sequencer port) gets its own token set; point the treasury at this one.
PORT=$(echo "$LOGOS_SEQUENCER" | grep -oE ':[0-9]+' | tr -d ':'); PORT="${PORT:-3071}"
OUTFILE="$LEE_WALLET_HOME_DIR/faucet_tokens-$PORT.json"
rm -f "$LEE_WALLET_HOME_DIR/wallet_config.json"   # re-seed so the treasury targets $LOGOS_SEQUENCER
newid() { printf '\n' | "$WALLET" account new public 2>/dev/null \
  | python3 -c "import sys,json,re;m=re.search(r'account_id (Public/\S+)',json.load(sys.stdin).get('output',''));print(m.group(1) if m else '')"; }

echo ">> Treasury home: $LEE_WALLET_HOME_DIR   sequencer: $LOGOS_SEQUENCER (port $PORT)"
RESULT="["
for NAME in "${ORDER[@]}"; do
  DEF=$(newid); SUP=$(newid)            # both must be pristine Account::default()
  echo ">> minting $NAME  def=${DEF#Public/}  sup=${SUP#Public/}"
  printf '\n' | timeout 180 "$WALLET" token new \
    --definition-account-id "$DEF" --supply-account-id "$SUP" \
    --name "$NAME" --total-supply "${SUPPLY[$NAME]}" >/dev/null 2>&1
  sleep "$LAND"
  HOLD=$(printf '\n' | "$WALLET" account get --account-id "$SUP" 2>/dev/null \
    | python3 -c "import sys,json;print('OK' if 'Fungible' in json.load(sys.stdin).get('output','') else 'FAIL')")
  echo "   supply holds tokens: $HOLD"
  [ "$HOLD" = "OK" ] || { echo "   !! $NAME failed - aborting"; exit 1; }
  RESULT+="{\"name\":\"$NAME\",\"def\":\"${DEF#Public/}\",\"sup\":\"${SUP#Public/}\",\"min\":10,\"max\":500},"
done
RESULT="${RESULT%,}]"
echo "$RESULT" | python3 -m json.tool > "$OUTFILE"
echo ">> wrote $OUTFILE"
cat "$OUTFILE"
