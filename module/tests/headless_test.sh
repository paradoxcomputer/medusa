#!/usr/bin/env bash
# Headless tests for medusa-wallet-basecamp using logoscore daemon mode.
# Usage: bash tests/headless_test.sh

set -euo pipefail

# ── Config ────────────────────────────────────────────────────────────────────
LOGOSCORE=$(find /nix/store -maxdepth 4 -name "logoscore" -path "*/bin/*" 2>/dev/null | head -1)
if [[ -z "$LOGOSCORE" ]]; then
    echo "FATAL: logoscore not found in Nix store" >&2
    exit 1
fi

MODULE_SRC="$HOME/.local/share/Logos/LogosBasecamp/modules/medusa_core"
if [[ ! -d "$MODULE_SRC" ]]; then
    echo "FATAL: medusa_core not installed at $MODULE_SRC" >&2
    echo "Run scripts/install-dev.sh first." >&2
    exit 1
fi

# ── Temp workspace ────────────────────────────────────────────────────────────
TMPDIR=$(mktemp -d)
trap 'cleanup' EXIT

cleanup() {
    if [[ -n "${DAEMON_PID:-}" ]]; then
        kill "$DAEMON_PID" 2>/dev/null || true
        wait "$DAEMON_PID" 2>/dev/null || true
    fi
    rm -rf "$TMPDIR"
}

MDIR="$TMPDIR/modules"
mkdir -p "$MDIR/medusa_core"
cp -r "$MODULE_SRC/." "$MDIR/medusa_core/"

# ── Fake wallet CLI ───────────────────────────────────────────────────────────
FAKE_CLI="$TMPDIR/fake_wallet.sh"
cat > "$FAKE_CLI" << 'FAKECLI'
#!/usr/bin/env bash
# Fake wallet CLI - echoes canned responses based on subcommand args
CMD="${1:-} ${2:-}"
case "$CMD" in
  "account ls")
    echo '[{"id":"public/testaccount001","type":"public","balance":150},{"id":"public/testaccount002","type":"public","balance":0}]'
    ;;
  "account get")
    echo '{"id":"public/testaccount001","type":"public","balance":150}'
    ;;
  "account new")
    echo '{"id":"public/newaccount123","type":"public","balance":0}'
    ;;
  "auth-transfer init")
    echo '{"ok":true,"message":"account initialized for transfers"}'
    ;;
  "auth-transfer send")
    echo '{"ok":true,"txId":"tx_abc123def456","from":"public/testaccount001","to":"public/testaccount002","amount":"10"}'
    ;;
  "token send")
    echo '{"ok":true,"txId":"tok_tx_001"}'
    ;;
  "account sync-private")
    echo '{"ok":true}'
    ;;
  "account export-mnemonic")
    echo 'legal winner thank year wave sausage worth useful legal winner thank yellow'
    ;;
  "account export-key")
    echo '10a26a9aec7d34b82364eeae45c5294dbb0a764b000b94eeb9b58511dc487c4d'
    ;;
  "account import-key")
    echo 'Imported account with account_id Public/GkeQajoUJ6KUzUVXDoKFYki1CS6J2dNFwWjJs8akMazu'
    ;;
  "restore-keys --depth")
    echo 'Public tree generated'
    ;;
  "pinata claim")
    echo '{"ok":true,"claimed":150,"to":"public/testaccount001"}'
    ;;
  *)
    echo '{"error":"unknown command"}'
    exit 1
    ;;
esac
FAKECLI
chmod +x "$FAKE_CLI"

# ── Test counters ─────────────────────────────────────────────────────────────
PASS=0
FAIL=0
SKIP=0

pass() { echo "  ✓ $1"; PASS=$((PASS+1)); }
fail() { echo "  ✗ $1"; FAIL=$((FAIL+1)); }
skip() { echo "  - $1 (skipped)"; SKIP=$((SKIP+1)); }

json_field() {
    local json="$1" field="$2"
    python3 -c "import sys,json; d=json.loads('''$json'''); print(d.get('$field',''))" 2>/dev/null || echo ""
}

check_no_error() {
    local json="$1" label="$2"
    if echo "$json" | python3 -c "import sys,json; d=json.loads(sys.stdin.read()); sys.exit(1 if 'error' in d else 0)" 2>/dev/null; then
        pass "$label"
    else
        local err
        err=$(echo "$json" | python3 -c "import sys,json; d=json.loads(sys.stdin.read()); print(d.get('error','?'))" 2>/dev/null || echo "?")
        fail "$label - error: $err"
    fi
}

check_field() {
    local json="$1" field="$2" expected="$3" label="$4"
    local actual
    actual=$(echo "$json" | python3 -c "import sys,json; d=json.loads(sys.stdin.read()); print(str(d.get('$field','')).lower())" 2>/dev/null || echo "")
    if [[ "$actual" == "$expected" ]]; then
        pass "$label"
    else
        fail "$label - expected '$expected', got '$actual'"
    fi
}

# ── Start daemon ──────────────────────────────────────────────────────────────
echo "Starting logoscore daemon…"
"$LOGOSCORE" -D --modules-dir "$MDIR" > "$TMPDIR/daemon.log" 2>&1 &
DAEMON_PID=$!
sleep 2

# Verify daemon is alive
if ! kill -0 "$DAEMON_PID" 2>/dev/null; then
    echo "FATAL: daemon failed to start"
    cat "$TMPDIR/daemon.log"
    exit 1
fi
echo "Daemon PID $DAEMON_PID"
echo ""

# ── Load module ───────────────────────────────────────────────────────────────
echo "=== Module load ==="
LOAD_OUT=$("$LOGOSCORE" load-module medusa_core 2>&1) || true
if echo "$LOAD_OUT" | grep -qi "ok\|loaded\|success"; then
    pass "load-module medusa_core"
else
    fail "load-module medusa_core - output: $LOAD_OUT"
    echo "Daemon log:"
    cat "$TMPDIR/daemon.log"
    exit 1
fi

LIST_OUT=$("$LOGOSCORE" list-modules --loaded 2>&1) || true
if echo "$LIST_OUT" | grep -q "medusa_core"; then
    pass "medusa_core appears in list-modules --loaded"
else
    fail "medusa_core not in list-modules --loaded - output: $LIST_OUT"
fi
echo ""

# ── getStatus - no CLI configured yet ────────────────────────────────────────
echo "=== getStatus (default - no CLI configured) ==="
RAW=$("$LOGOSCORE" call medusa_core getStatus 2>/dev/null) || RAW="{}"
RESULT=$(echo "$RAW" | python3 -c "import sys,json; d=json.loads(sys.stdin.read()); print(d.get('result','{}'))" 2>/dev/null || echo "{}")
# cliFound may be true if 'wallet' is in PATH, or false - both are valid
if echo "$RESULT" | python3 -c "import sys,json; d=json.loads(sys.stdin.read()); assert 'cliFound' in d and 'cliPath' in d" 2>/dev/null; then
    pass "getStatus returns cliFound + cliPath fields"
else
    fail "getStatus missing expected fields - result: $RESULT"
fi

# ── getConfig ─────────────────────────────────────────────────────────────────
echo ""
echo "=== getConfig ==="
RAW=$("$LOGOSCORE" call medusa_core getConfig 2>/dev/null) || RAW="{}"
RESULT=$(echo "$RAW" | python3 -c "import sys,json; d=json.loads(sys.stdin.read()); print(d.get('result','{}'))" 2>/dev/null || echo "{}")
if echo "$RESULT" | python3 -c "import sys,json; d=json.loads(sys.stdin.read()); assert 'cliPath' in d and 'cliPathEff' in d" 2>/dev/null; then
    pass "getConfig returns cliPath + cliPathEff fields"
else
    fail "getConfig missing expected fields - result: $RESULT"
fi

# ── setCliPath - point to fake CLI ────────────────────────────────────────────
echo ""
echo "=== setCliPath ==="

# Empty path → error
RAW=$("$LOGOSCORE" call medusa_core setCliPath "" 2>/dev/null) || RAW="{}"
RESULT=$(echo "$RAW" | python3 -c "import sys,json; d=json.loads(sys.stdin.read()); print(d.get('result','{}'))" 2>/dev/null || echo "{}")
if echo "$RESULT" | python3 -c "import sys,json; d=json.loads(sys.stdin.read()); assert 'error' in d" 2>/dev/null; then
    pass "setCliPath('') returns error"
else
    fail "setCliPath('') should return error - result: $RESULT"
fi

# Valid path → ok
RAW=$("$LOGOSCORE" call medusa_core setCliPath "$FAKE_CLI" 2>/dev/null) || RAW="{}"
RESULT=$(echo "$RAW" | python3 -c "import sys,json; d=json.loads(sys.stdin.read()); print(d.get('result','{}'))" 2>/dev/null || echo "{}")
if echo "$RESULT" | python3 -c "import sys,json; d=json.loads(sys.stdin.read()); assert d.get('ok') is True" 2>/dev/null; then
    pass "setCliPath(fake_cli) returns ok:true"
else
    fail "setCliPath(fake_cli) failed - result: $RESULT"
fi

# Verify getStatus now shows cliFound:true
RAW=$("$LOGOSCORE" call medusa_core getStatus 2>/dev/null) || RAW="{}"
RESULT=$(echo "$RAW" | python3 -c "import sys,json; d=json.loads(sys.stdin.read()); print(d.get('result','{}'))" 2>/dev/null || echo "{}")
if echo "$RESULT" | python3 -c "import sys,json; d=json.loads(sys.stdin.read()); assert d.get('cliFound') is True" 2>/dev/null; then
    pass "getStatus.cliFound=true after setCliPath"
else
    fail "getStatus.cliFound still false after setCliPath - result: $RESULT"
fi

# ── listAccounts ──────────────────────────────────────────────────────────────
echo ""
echo "=== listAccounts ==="
RAW=$("$LOGOSCORE" call medusa_core listAccounts 2>/dev/null) || RAW="{}"
RESULT=$(echo "$RAW" | python3 -c "import sys,json; d=json.loads(sys.stdin.read()); print(d.get('result','[]'))" 2>/dev/null || echo "[]")
COUNT=$(echo "$RESULT" | python3 -c "import sys,json; d=json.loads(sys.stdin.read()); print(len(d) if isinstance(d,list) else 0)" 2>/dev/null || echo "0")
if [[ "$COUNT" == "2" ]]; then
    pass "listAccounts returns 2 accounts from fake CLI"
else
    fail "listAccounts: expected 2 accounts, got '$COUNT' - result: $RESULT"
fi

FIRST_ID=$(echo "$RESULT" | python3 -c "import sys,json; d=json.loads(sys.stdin.read()); print(d[0]['id'] if isinstance(d,list) and d else '')" 2>/dev/null || echo "")
if [[ "$FIRST_ID" == "public/testaccount001" ]]; then
    pass "listAccounts first account id correct"
else
    fail "listAccounts first id: expected 'public/testaccount001', got '$FIRST_ID'"
fi

BALANCE=$(echo "$RESULT" | python3 -c "import sys,json; d=json.loads(sys.stdin.read()); print(d[0].get('balance','') if isinstance(d,list) and d else '')" 2>/dev/null || echo "")
if [[ "$BALANCE" == "150" ]]; then
    pass "listAccounts first account balance=150"
else
    fail "listAccounts balance: expected '150', got '$BALANCE'"
fi

# ── getBalance ────────────────────────────────────────────────────────────────
echo ""
echo "=== getBalance ==="

# Missing accountId → error
RAW=$("$LOGOSCORE" call medusa_core getBalance "" 2>/dev/null) || RAW="{}"
RESULT=$(echo "$RAW" | python3 -c "import sys,json; d=json.loads(sys.stdin.read()); print(d.get('result','{}'))" 2>/dev/null || echo "{}")
if echo "$RESULT" | python3 -c "import sys,json; d=json.loads(sys.stdin.read()); assert 'error' in d" 2>/dev/null; then
    pass "getBalance('') returns error"
else
    fail "getBalance('') should return error - result: $RESULT"
fi

# Valid accountId → balance
RAW=$("$LOGOSCORE" call medusa_core getBalance "public/testaccount001" 2>/dev/null) || RAW="{}"
RESULT=$(echo "$RAW" | python3 -c "import sys,json; d=json.loads(sys.stdin.read()); print(d.get('result','{}'))" 2>/dev/null || echo "{}")
BAL=$(echo "$RESULT" | python3 -c "import sys,json; d=json.loads(sys.stdin.read()); print(d.get('balance',''))" 2>/dev/null || echo "")
if [[ "$BAL" == "150" ]]; then
    pass "getBalance returns balance=150"
else
    fail "getBalance: expected balance=150, got '$BAL' - result: $RESULT"
fi

# ── createAccount ─────────────────────────────────────────────────────────────
echo ""
echo "=== createAccount ==="
RAW=$("$LOGOSCORE" call medusa_core createAccount 2>/dev/null) || RAW="{}"
RESULT=$(echo "$RAW" | python3 -c "import sys,json; d=json.loads(sys.stdin.read()); print(d.get('result','{}'))" 2>/dev/null || echo "{}")
NEW_ID=$(echo "$RESULT" | python3 -c "import sys,json; d=json.loads(sys.stdin.read()); print(d.get('id',''))" 2>/dev/null || echo "")
if [[ "$NEW_ID" == "public/newaccount123" ]]; then
    pass "createAccount returns new account id"
else
    fail "createAccount: expected id='public/newaccount123', got '$NEW_ID' - result: $RESULT"
fi

# ── initAccount ───────────────────────────────────────────────────────────────
echo ""
echo "=== initAccount ==="

# Missing accountId → error
RAW=$("$LOGOSCORE" call medusa_core initAccount "" 2>/dev/null) || RAW="{}"
RESULT=$(echo "$RAW" | python3 -c "import sys,json; d=json.loads(sys.stdin.read()); print(d.get('result','{}'))" 2>/dev/null || echo "{}")
if echo "$RESULT" | python3 -c "import sys,json; d=json.loads(sys.stdin.read()); assert 'error' in d" 2>/dev/null; then
    pass "initAccount('') returns error"
else
    fail "initAccount('') should return error - result: $RESULT"
fi

# Valid accountId → ok
RAW=$("$LOGOSCORE" call medusa_core initAccount "public/testaccount001" 2>/dev/null) || RAW="{}"
RESULT=$(echo "$RAW" | python3 -c "import sys,json; d=json.loads(sys.stdin.read()); print(d.get('result','{}'))" 2>/dev/null || echo "{}")
if echo "$RESULT" | python3 -c "import sys,json; d=json.loads(sys.stdin.read()); assert d.get('ok') is True" 2>/dev/null; then
    pass "initAccount returns ok:true"
else
    fail "initAccount: expected ok:true - result: $RESULT"
fi

# ── claimFaucet ───────────────────────────────────────────────────────────────
echo ""
echo "=== claimFaucet ==="

# Missing accountId → error
RAW=$("$LOGOSCORE" call medusa_core claimFaucet "" 2>/dev/null) || RAW="{}"
RESULT=$(echo "$RAW" | python3 -c "import sys,json; d=json.loads(sys.stdin.read()); print(d.get('result','{}'))" 2>/dev/null || echo "{}")
if echo "$RESULT" | python3 -c "import sys,json; d=json.loads(sys.stdin.read()); assert 'error' in d" 2>/dev/null; then
    pass "claimFaucet('') returns error"
else
    fail "claimFaucet('') should return error - result: $RESULT"
fi

# Valid accountId → ok + claimed=150
RAW=$("$LOGOSCORE" call medusa_core claimFaucet "public/testaccount001" 2>/dev/null) || RAW="{}"
RESULT=$(echo "$RAW" | python3 -c "import sys,json; d=json.loads(sys.stdin.read()); print(d.get('result','{}'))" 2>/dev/null || echo "{}")
CLAIMED=$(echo "$RESULT" | python3 -c "import sys,json; d=json.loads(sys.stdin.read()); print(d.get('claimed',''))" 2>/dev/null || echo "")
if [[ "$CLAIMED" == "150" ]]; then
    pass "claimFaucet returns claimed=150"
else
    fail "claimFaucet: expected claimed=150, got '$CLAIMED' - result: $RESULT"
fi

# Without prefix → plugin should add public/ prefix before passing to CLI
RAW=$("$LOGOSCORE" call medusa_core claimFaucet "testaccount001" 2>/dev/null) || RAW="{}"
RESULT=$(echo "$RAW" | python3 -c "import sys,json; d=json.loads(sys.stdin.read()); print(d.get('result','{}'))" 2>/dev/null || echo "{}")
if echo "$RESULT" | python3 -c "import sys,json; d=json.loads(sys.stdin.read()); assert d.get('ok') is True" 2>/dev/null; then
    pass "claimFaucet adds public/ prefix automatically"
else
    fail "claimFaucet without prefix: expected ok:true - result: $RESULT"
fi

# ── sendTransfer - input validation ──────────────────────────────────────────
echo ""
echo "=== sendTransfer (validation) ==="

# NOTE: logoscore daemon parses CLI args as JSON before forwarding.
# Numeric-only strings (e.g. "10") are coerced to int → METHOD_FAILED for QString params.
# Use '"10"' (JSON-quoted) so logoscore delivers a string, or use a non-numeric value.

# Missing from → error
RAW=$("$LOGOSCORE" call medusa_core sendTransfer "" "public/testaccount002" '"10"' 2>/dev/null) || RAW="{}"
RESULT=$(echo "$RAW" | python3 -c "import sys,json; d=json.loads(sys.stdin.read()); print(d.get('result','{}'))" 2>/dev/null || echo "{}")
if echo "$RESULT" | python3 -c "import sys,json; d=json.loads(sys.stdin.read()); assert 'error' in d" 2>/dev/null; then
    pass "sendTransfer(from='') returns error"
else
    fail "sendTransfer(from='') should return error - result: $RESULT"
fi

# Missing to → error
RAW=$("$LOGOSCORE" call medusa_core sendTransfer "public/testaccount001" "" '"10"' 2>/dev/null) || RAW="{}"
RESULT=$(echo "$RAW" | python3 -c "import sys,json; d=json.loads(sys.stdin.read()); print(d.get('result','{}'))" 2>/dev/null || echo "{}")
if echo "$RESULT" | python3 -c "import sys,json; d=json.loads(sys.stdin.read()); assert 'error' in d" 2>/dev/null; then
    pass "sendTransfer(to='') returns error"
else
    fail "sendTransfer(to='') should return error - result: $RESULT"
fi

# Missing amount → error
RAW=$("$LOGOSCORE" call medusa_core sendTransfer "public/testaccount001" "public/testaccount002" "" 2>/dev/null) || RAW="{}"
RESULT=$(echo "$RAW" | python3 -c "import sys,json; d=json.loads(sys.stdin.read()); print(d.get('result','{}'))" 2>/dev/null || echo "{}")
if echo "$RESULT" | python3 -c "import sys,json; d=json.loads(sys.stdin.read()); assert 'error' in d" 2>/dev/null; then
    pass "sendTransfer(amount='') returns error"
else
    fail "sendTransfer(amount='') should return error - result: $RESULT"
fi

# ── sendTransfer - success path ───────────────────────────────────────────────
echo ""
echo "=== sendTransfer (success) ==="
# Use '"10"' so logoscore passes it as a JSON string, not an integer
RAW=$("$LOGOSCORE" call medusa_core sendTransfer \
    "public/testaccount001" "public/testaccount002" '"10"' 2>/dev/null) || RAW="{}"
RESULT=$(echo "$RAW" | python3 -c "import sys,json; d=json.loads(sys.stdin.read()); print(d.get('result','{}'))" 2>/dev/null || echo "{}")
TX_ID=$(echo "$RESULT" | python3 -c "import sys,json; d=json.loads(sys.stdin.read()); print(d.get('txId',''))" 2>/dev/null || echo "")
if [[ "$TX_ID" == "tx_abc123def456" ]]; then
    pass "sendTransfer returns txId from fake CLI"
elif echo "$RESULT" | python3 -c "import sys,json; d=json.loads(sys.stdin.read()); assert d.get('ok') is True" 2>/dev/null; then
    pass "sendTransfer returns ok:true"
else
    fail "sendTransfer: expected txId - result: $RESULT"
fi

# ── Private account management ────────────────────────────────────────────────
echo ""
echo "=== createPrivateAccount ==="
RAW=$("$LOGOSCORE" call medusa_core createPrivateAccount "" 2>/dev/null) || RAW="{}"
RESULT=$(echo "$RAW" | python3 -c "import sys,json; d=json.loads(sys.stdin.read()); print(d.get('result','{}'))" 2>/dev/null || echo "{}")
if echo "$RESULT" | python3 -c "import sys,json; d=json.loads(sys.stdin.read()); assert d.get('ok') is True" 2>/dev/null; then
    pass "createPrivateAccount returns ok:true"
else
    fail "createPrivateAccount: expected ok:true - result: $RESULT"
fi

echo ""
echo "=== syncPrivate ==="
RAW=$("$LOGOSCORE" call medusa_core syncPrivate 2>/dev/null) || RAW="{}"
RESULT=$(echo "$RAW" | python3 -c "import sys,json; d=json.loads(sys.stdin.read()); print(d.get('result','{}'))" 2>/dev/null || echo "{}")
if echo "$RESULT" | python3 -c "import sys,json; d=json.loads(sys.stdin.read()); assert d.get('ok') is True" 2>/dev/null; then
    pass "syncPrivate returns ok:true"
else
    fail "syncPrivate: expected ok:true - result: $RESULT"
fi

echo ""
echo "=== getAccountKeys ==="
RAW=$("$LOGOSCORE" call medusa_core getAccountKeys "" 2>/dev/null) || RAW="{}"
RESULT=$(echo "$RAW" | python3 -c "import sys,json; d=json.loads(sys.stdin.read()); print(d.get('result','{}'))" 2>/dev/null || echo "{}")
if echo "$RESULT" | python3 -c "import sys,json; d=json.loads(sys.stdin.read()); assert 'error' in d" 2>/dev/null; then
    pass "getAccountKeys('') returns error"
else
    fail "getAccountKeys('') should return error - result: $RESULT"
fi

RAW=$("$LOGOSCORE" call medusa_core getAccountKeys "Private/testaccount002" 2>/dev/null) || RAW="{}"
RESULT=$(echo "$RAW" | python3 -c "import sys,json; d=json.loads(sys.stdin.read()); print(d.get('result','{}'))" 2>/dev/null || echo "{}")
if echo "$RESULT" | python3 -c "import sys,json; d=json.loads(sys.stdin.read()); assert d.get('ok') is True" 2>/dev/null; then
    pass "getAccountKeys(valid) returns ok:true"
else
    fail "getAccountKeys(valid): expected ok:true - result: $RESULT"
fi

# ── Privacy transfers (async: start → poll getJob) ────────────────────────────
# Polls getJob until the job is terminal; echoes "<state> <txId>".
wait_job() {
    local jobid="$1" state="" txid="" jr jres i
    for i in $(seq 1 40); do
        jr=$("$LOGOSCORE" call medusa_core getJob "$jobid" 2>/dev/null) || jr="{}"
        # Unwrap the daemon RPC envelope only (result is the getJob JSON string,
        # whose state/txId are at the top level) - same pattern as every other call.
        jres=$(echo "$jr" | python3 -c "import sys,json; d=json.loads(sys.stdin.read()); print(d.get('result','{}'))" 2>/dev/null || echo "{}")
        state=$(echo "$jres" | python3 -c "import sys,json; d=json.loads(sys.stdin.read()); print(d.get('state',''))" 2>/dev/null || echo "")
        if [[ "$state" == "done" || "$state" == "error" ]]; then
            txid=$(echo "$jres" | python3 -c "import sys,json; d=json.loads(sys.stdin.read()); print(d.get('txId',''))" 2>/dev/null || echo "")
            break
        fi
        sleep 0.3
    done
    echo "$state $txid"
}

echo ""
echo "=== startShield (validation) ==="
# Missing amount → error
RAW=$("$LOGOSCORE" call medusa_core startShield "native" "Public/testaccount001" "Private/testaccount002" "" 2>/dev/null) || RAW="{}"
RESULT=$(echo "$RAW" | python3 -c "import sys,json; d=json.loads(sys.stdin.read()); print(d.get('result','{}'))" 2>/dev/null || echo "{}")
if echo "$RESULT" | python3 -c "import sys,json; d=json.loads(sys.stdin.read()); assert 'error' in d" 2>/dev/null; then
    pass "startShield(amount='') returns error"
else
    fail "startShield(amount='') should return error - result: $RESULT"
fi

# Private source → prefix conflict → error
RAW=$("$LOGOSCORE" call medusa_core startShield "native" "Private/testaccount002" "Private/testaccount003" '"10"' 2>/dev/null) || RAW="{}"
RESULT=$(echo "$RAW" | python3 -c "import sys,json; d=json.loads(sys.stdin.read()); print(d.get('result','{}'))" 2>/dev/null || echo "{}")
if echo "$RESULT" | python3 -c "import sys,json; d=json.loads(sys.stdin.read()); assert 'error' in d" 2>/dev/null; then
    pass "startShield(private source) returns error"
else
    fail "startShield(private source) should return error - result: $RESULT"
fi

echo ""
echo "=== startShield (async success) ==="
RAW=$("$LOGOSCORE" call medusa_core startShield "native" "Public/testaccount001" "Private/testaccount002" '"10"' 2>/dev/null) || RAW="{}"
RESULT=$(echo "$RAW" | python3 -c "import sys,json; d=json.loads(sys.stdin.read()); print(d.get('result','{}'))" 2>/dev/null || echo "{}")
JOBID=$(echo "$RESULT" | python3 -c "import sys,json; d=json.loads(sys.stdin.read()); print(d.get('jobId',''))" 2>/dev/null || echo "")
if [[ -n "$JOBID" ]]; then
    pass "startShield returns jobId ($JOBID)"
else
    fail "startShield returned no jobId - result: $RESULT"
fi

if [[ -n "$JOBID" ]]; then
    read -r STATE TXID < <(wait_job "$JOBID")
    if [[ "$STATE" == "done" ]]; then
        pass "shield job reached state=done"
    else
        fail "shield job did not complete (state=$STATE)"
    fi
    if [[ "$TXID" == "tx_abc123def456" ]]; then
        pass "shield job surfaced txId from fake CLI"
    else
        fail "shield job txId mismatch (got '$TXID')"
    fi
else
    skip "shield job poll (no jobId)"
    skip "shield job txId (no jobId)"
fi

echo ""
echo "=== startPrivateTransferForeign (async success) ==="
RAW=$("$LOGOSCORE" call medusa_core startPrivateTransferForeign \
    "native" "Private/testaccount002" "aabbccdd" "eeff0011" "ident1" '"7"' 2>/dev/null) || RAW="{}"
RESULT=$(echo "$RAW" | python3 -c "import sys,json; d=json.loads(sys.stdin.read()); print(d.get('result','{}'))" 2>/dev/null || echo "{}")
JOBID=$(echo "$RESULT" | python3 -c "import sys,json; d=json.loads(sys.stdin.read()); print(d.get('jobId',''))" 2>/dev/null || echo "")
if [[ -n "$JOBID" ]]; then
    pass "startPrivateTransferForeign returns jobId"
    read -r STATE TXID < <(wait_job "$JOBID")
    if [[ "$STATE" == "done" ]]; then
        pass "foreign private transfer reached state=done"
    else
        fail "foreign private transfer did not complete (state=$STATE)"
    fi
else
    fail "startPrivateTransferForeign returned no jobId - result: $RESULT"
    skip "foreign private transfer poll"
fi

echo ""
echo "=== startDeshield (async success) ==="
RAW=$("$LOGOSCORE" call medusa_core startDeshield "native" "Private/testaccount002" "Public/testaccount001" '"5"' 2>/dev/null) || RAW="{}"
RESULT=$(echo "$RAW" | python3 -c "import sys,json; d=json.loads(sys.stdin.read()); print(d.get('result','{}'))" 2>/dev/null || echo "{}")
JOBID=$(echo "$RESULT" | python3 -c "import sys,json; d=json.loads(sys.stdin.read()); print(d.get('jobId',''))" 2>/dev/null || echo "")
if [[ -n "$JOBID" ]]; then
    pass "startDeshield returns jobId"
    read -r STATE TXID < <(wait_job "$JOBID")
    if [[ "$STATE" == "done" ]]; then
        pass "deshield job reached state=done"
    else
        fail "deshield job did not complete (state=$STATE)"
    fi
else
    fail "startDeshield returned no jobId - result: $RESULT"
    skip "deshield job poll"
fi

# ── Security / import-export ──────────────────────────────────────────────────
echo ""
echo "=== session password ==="
RAW=$("$LOGOSCORE" call medusa_core setSessionPassword '"hunter2"' 2>/dev/null) || RAW="{}"
RESULT=$(echo "$RAW" | python3 -c "import sys,json; d=json.loads(sys.stdin.read()); print(d.get('result','{}'))" 2>/dev/null || echo "{}")
if echo "$RESULT" | python3 -c "import sys,json; d=json.loads(sys.stdin.read()); assert d.get('ok') is True" 2>/dev/null; then
    pass "setSessionPassword ok"
else
    fail "setSessionPassword - result: $RESULT"
fi
RAW=$("$LOGOSCORE" call medusa_core getSecurityState 2>/dev/null) || RAW="{}"
RESULT=$(echo "$RAW" | python3 -c "import sys,json; d=json.loads(sys.stdin.read()); print(d.get('result','{}'))" 2>/dev/null || echo "{}")
if echo "$RESULT" | python3 -c "import sys,json; d=json.loads(sys.stdin.read()); assert d.get('hasPassword') is True" 2>/dev/null; then
    pass "getSecurityState.hasPassword=true"
else
    fail "getSecurityState - result: $RESULT"
fi

echo ""
echo "=== exportMnemonic / exportKey / importKey ==="
RAW=$("$LOGOSCORE" call medusa_core exportMnemonic 2>/dev/null) || RAW="{}"
RESULT=$(echo "$RAW" | python3 -c "import sys,json; d=json.loads(sys.stdin.read()); print(d.get('result','{}'))" 2>/dev/null || echo "{}")
WORDS=$(echo "$RESULT" | python3 -c "import sys,json; d=json.loads(sys.stdin.read()); print(len(d.get('mnemonic','').split()))" 2>/dev/null || echo 0)
if [[ "$WORDS" -ge 12 ]]; then pass "exportMnemonic returns a phrase ($WORDS words)"; else fail "exportMnemonic - result: $RESULT"; fi

RAW=$("$LOGOSCORE" call medusa_core exportKey "Public/abc" 2>/dev/null) || RAW="{}"
RESULT=$(echo "$RAW" | python3 -c "import sys,json; d=json.loads(sys.stdin.read()); print(d.get('result','{}'))" 2>/dev/null || echo "{}")
KLEN=$(echo "$RESULT" | python3 -c "import sys,json; d=json.loads(sys.stdin.read()); print(len(d.get('privateKey','')))" 2>/dev/null || echo 0)
if [[ "$KLEN" == "64" ]]; then pass "exportKey returns a 64-hex key"; else fail "exportKey - len $KLEN, result: $RESULT"; fi

RAW=$("$LOGOSCORE" call medusa_core importKey "deadbeef" "mine" 2>/dev/null) || RAW="{}"
RESULT=$(echo "$RAW" | python3 -c "import sys,json; d=json.loads(sys.stdin.read()); print(d.get('result','{}'))" 2>/dev/null || echo "{}")
if echo "$RESULT" | python3 -c "import sys,json; d=json.loads(sys.stdin.read()); assert d.get('ok') is True and d.get('id','').startswith('Public/')" 2>/dev/null; then
    pass "importKey returns the imported account id"
else
    fail "importKey - result: $RESULT"
fi

# ── Summary ───────────────────────────────────────────────────────────────────
echo ""
echo "══════════════════════════════════"
echo "  PASS: $PASS"
echo "  FAIL: $FAIL"
echo "  SKIP: $SKIP"
echo "══════════════════════════════════"

if [[ $FAIL -gt 0 ]]; then
    echo ""
    echo "Daemon log tail:"
    tail -20 "$TMPDIR/daemon.log"
    exit 1
fi
echo "All tests passed."
