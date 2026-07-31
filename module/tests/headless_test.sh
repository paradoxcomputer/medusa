#!/usr/bin/env bash
# ══════════════════════════════════════════════════════════════════════════════════════════════
#  ██  THIS SCRIPT REFUSES TO RUN ITS LIVE SUITE. THAT IS DELIBERATE. READ THIS FIRST.  ██
#
#  It used to drive the medusa_core module through a logoscore daemon. It was written against an
#  ABI that no longer exists, and - this is the part that mattered - its ONLY isolation from the
#  developer's REAL wallet was `setCliPath`, a verb that has since been permanently disabled, and
#  `setSessionPassword`, a verb that has been deleted outright.
#
#  Run as it stood, on a box with logoscore installed, it would have:
#    * failed to install its fake CLI (setCliPath refuses now), so cliPath() would resolve the
#      real bundled `wallet` binary out of the copied module directory; and
#    * never exported LEE_WALLET_HOME_DIR, so walletHome() would fall back to
#      $HOME/.local/share/medusa-wallet-home - THE DEVELOPER'S ACTUAL WALLET;
#  and then called createAccount, initAccount, importKey, sendTransfer and startShield against
#  it, on whatever zone happened to be active. That is a destructive test, not a test.
#
#  It also asserted `pass "setSessionPassword ok"`, i.e. it stood as an instruction that
#  restoring the deleted session-password mint - the round-1 vulnerability - was the correct
#  outcome. That assertion is gone and is not coming back.
#
#  What runs instead: the STATIC ABI AUDIT at the bottom, which needs no daemon and no module
#  install, and which reports exactly which of the old suite's calls no longer match the shipped
#  header. That is the worklist for whoever revives this file. Reviving it means, at minimum:
#    1. keeping the isolation block below (it now runs BEFORE anything else, so even deleting
#       the refusal cannot point the module at a real wallet home or a real CLI);
#    2. driving sessions the way the product does - createEncryptedWallet / unlock against a
#       password-checking fake CLI - because no verb mints a session any more; and
#    3. passing the password argument to every gated verb, and updating the fake CLI's
#       subcommands, which are also stale (`account ls` / `account new` / `account import-key`
#       are not what the module calls).
#  Do not "fix" this by deleting the guard.
#
#  The suite that does run is the C++ one:
#      cmake -B build-tests -S . && cmake --build build-tests -j4
#      ./build-tests/test_wallet_plugin
# ══════════════════════════════════════════════════════════════════════════════════════════════
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MODULE_ROOT="$(cd "$HERE/.." && pwd)"
HEADER="$MODULE_ROOT/src/plugin/WalletPlugin.h"

# ── 1. ISOLATION, ESTABLISHED BEFORE ANY OTHER LINE OF THIS FILE ──────────────────────────────
# Deliberately first, and deliberately unconditional. The current module reads its wallet home
# from $LEE_WALLET_HOME_DIR ($NSSA_WALLET_HOME_DIR is honoured as the legacy name) and its CLI
# from $MEDUSA_WALLET_CLI - both from THIS PROCESS'S environment, which is the only override that
# still exists. Exporting them here means that even a future edit which removes the refusal below
# starts out pointed at a throwaway directory instead of the developer's wallet.
SANDBOX="$(mktemp -d "${TMPDIR:-/tmp}/medusa-headless.XXXXXXXX")"
trap 'rm -rf "$SANDBOX"' EXIT

export LEE_WALLET_HOME_DIR="$SANDBOX/wallet-home"
export NSSA_WALLET_HOME_DIR="$SANDBOX/wallet-home"
export MEDUSA_WALLET_CLI="$SANDBOX/fake-wallet.sh"
mkdir -p "$LEE_WALLET_HOME_DIR"

# A stand-in CLI that cannot reach a chain, cannot decrypt anything, and cannot be mistaken for
# the real one. It exists so that a resolver which ignored $MEDUSA_WALLET_CLI would show up as a
# failure rather than as a silent run of the shipped binary.
cat > "$MEDUSA_WALLET_CLI" << 'FAKECLI'
#!/bin/sh
# Refuses everything. A revived live suite would replace this with a password-checking stand-in.
echo '{"error":"headless_test.sh sandbox CLI: this build runs no wallet commands"}'
exit 1
FAKECLI
chmod +x "$MEDUSA_WALLET_CLI"

# Fail closed: if the sandbox did not take, stop here rather than continue into anything that
# could reach the real store.
REAL_HOME_DEFAULT="$HOME/.local/share/medusa-wallet-home"
if [[ "$LEE_WALLET_HOME_DIR" == "$REAL_HOME_DEFAULT" || "$LEE_WALLET_HOME_DIR" != "$SANDBOX"/* ]]; then
    echo "FATAL: wallet-home isolation did not take (LEE_WALLET_HOME_DIR=$LEE_WALLET_HOME_DIR)" >&2
    exit 1
fi
if [[ ! -x "$MEDUSA_WALLET_CLI" || "$MEDUSA_WALLET_CLI" != "$SANDBOX"/* ]]; then
    echo "FATAL: CLI isolation did not take (MEDUSA_WALLET_CLI=$MEDUSA_WALLET_CLI)" >&2
    exit 1
fi
if [[ -e "$LEE_WALLET_HOME_DIR/storage.json" ]]; then
    echo "FATAL: the sandbox wallet home is not empty - refusing to operate on it" >&2
    exit 1
fi

# ── 2. THE REFUSAL ────────────────────────────────────────────────────────────────────────────
cat >&2 << 'BANNER'

  ##############################################################################
  #  headless_test.sh: THE LIVE DAEMON SUITE IS DISABLED AND WILL NOT RUN.     #
  #                                                                            #
  #  Reason: it was written against a removed ABI, and its only isolation from #
  #  your real wallet was setCliPath(), a verb that now always refuses. Running #
  #  it would have created accounts, imported a key, sent a transfer and        #
  #  shielded funds against ~/.local/share/medusa-wallet-home.                  #
  #                                                                            #
  #  The suite that runs is the C++ one:                                       #
  #      cmake -B build-tests -S . && cmake --build build-tests -j4            #
  #      ./build-tests/test_wallet_plugin                                      #
  #                                                                            #
  #  What follows is a static audit of this file's old calls against the        #
  #  shipped header. It starts nothing and touches no wallet.                   #
  ##############################################################################

BANNER

# ── 3. STATIC ABI AUDIT (this is the part that actually runs) ─────────────────────────────────
# The table below is the exact set of calls the deleted body made: verb, argument count, and
# whether the verb is one the module now gates on the session password. Nothing here loads or
# executes a module; it reads the header and compares.
if [[ ! -r "$HEADER" ]]; then
    echo "FATAL: cannot read $HEADER" >&2
    exit 1
fi

PY=$(command -v python3 || true)
if [[ -z "$PY" ]]; then
    echo "python3 is not installed, so the audit cannot run. The live suite stays disabled." >&2
    exit 2
fi

"$PY" - "$HEADER" << 'AUDIT'
import re, sys

header = open(sys.argv[1]).read()

# Every Q_INVOKABLE, as name -> (min arity, max arity). Defaulted parameters make the minimum
# smaller than the maximum, which is exactly why an arity check ALONE cannot catch a gated verb
# called without its password: startShield(4) is still arity-legal and still always refuses.
sigs = {}
for m in re.finditer(r'Q_INVOKABLE\s+(.*?);', header, re.S):
    decl = ' '.join(m.group(1).split())
    head = re.match(r'.*?(\w+)\s*\((.*)\)\s*(?:const)?$', decl)
    if not head:
        continue
    name, params = head.group(1), head.group(2).strip()
    if not params:
        lo = hi = 0
    else:
        depth, cur, parts = 0, '', []
        for ch in params:
            if ch == ',' and depth == 0:
                parts.append(cur); cur = ''
                continue
            if ch in '(<': depth += 1
            if ch in ')>': depth -= 1
            cur += ch
        parts.append(cur)
        hi = len(parts)
        lo = sum(1 for p in parts if '=' not in p)
    sigs[name] = (lo, hi)

# The old body's calls: (verb, argc, is-gated-on-the-session-password).
CALLS = [
    ("getStatus",                   0, False), ("getConfig",                    0, False),
    ("setCliPath",                  1, False), ("listAccounts",                 0, False),
    ("getBalance",                  1, False), ("createAccount",                0, False),
    ("initAccount",                 1, False), ("claimFaucet",                  1, False),
    ("sendTransfer",                3, True),  ("createPrivateAccount",         1, False),
    ("syncPrivate",                 0, False), ("getAccountKeys",               1, False),
    ("startShield",                 4, True),  ("startDeshield",                4, True),
    ("startPrivateTransferForeign", 6, True),  ("getJob",                       1, False),
    ("setSessionPassword",          1, False), ("getSecurityState",             0, False),
    ("exportMnemonic",              0, True),  ("exportKey",                    1, True),
    ("importKey",                   2, False),
]

gone, arity, unauth, ok = [], [], [], []
for name, argc, gated in CALLS:
    if name not in sigs:
        gone.append("%s/%d  - the verb no longer exists" % (name, argc))
        continue
    lo, hi = sigs[name]
    if not (lo <= argc <= hi):
        arity.append("%s/%d  - the header takes %d..%d" % (name, argc, lo, hi))
        continue
    if gated and argc < hi:
        unauth.append("%s/%d  - gated verb, no password argument: always refuses now"
                      % (name, argc))
        continue
    ok.append("%s/%d" % (name, argc))

def section(title, rows):
    print("  %s (%d)" % (title, len(rows)))
    for r in rows or ["none"]:
        print("      %s" % r)
    print()

print("  STATIC ABI AUDIT of the calls headless_test.sh used to make")
print("  header: %s (%d Q_INVOKABLE verbs)\n" % (sys.argv[1], len(sigs)))
section("DELETED VERBS", gone)
section("ARITY NO LONGER VALID", arity)
section("STILL CALLABLE BUT NOW ALWAYS REFUSED", unauth)
section("STILL VALID AS WRITTEN", ok)

broken = len(gone) + len(arity) + len(unauth)
print("  %d of %d calls are stale. The live suite stays disabled until they are rewritten."
      % (broken, len(CALLS)))
AUDIT

echo >&2
echo "headless_test.sh: refused (live suite disabled). Sandbox was $SANDBOX; nothing outside it was touched." >&2
exit 2
