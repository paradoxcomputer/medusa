# medusa-l1

Phase-1 Bedrock L1 client for the Medusa wallet. A small CLI that talks to a
logos-blockchain ("Bedrock") node's REST API through the real pinned
logos-blockchain client crates: the same git rev the LEZ tree in
`wallet/.lez-build` pins, so the wire format always matches the deployed node.

Every subcommand prints a single JSON object to stdout: `{"ok":true,...}` on
success, `{"error":"..."}` on failure (exit code 1), the same convention as
`medusa-faucet-client`.

## Phase-1 custody model (node-side custody)

In phase 1 this binary holds NO key material and reads NO password. The zk keys
live in the node's own wallet service, so:

- `balance` (and the `--pk` option of `withdraw-status`) can only see keys the
  node itself has registered. For any other key the node answers 404 and this
  tool reports:
  `{"error":"key not registered on this node's wallet service - phase 1 serves node-registered keys only"}`
- `transfer` and `deposit` ask the NODE to build, sign and submit the
  transaction (`POST /wallet/transactions/transfer-funds`,
  `POST /channel/deposit`). They are write operations against node-custodied
  funds and are therefore gated: without `MEDUSA_L1_ALLOW_WRITES=1` in the
  environment they refuse with
  `{"error":"writes disabled: set MEDUSA_L1_ALLOW_WRITES=1"}`. The gate is
  checked before any request, including the read-only ones `deposit` makes to
  decide whether it may write at all.
- `info` and `withdraw-status` are plain read-only GETs.

## Node URL resolution

First non-empty wins (mirrors the zone endpoint pattern in
`module/src/plugin/WalletPlugin.cpp`):

1. `--url <base-url>` flag
2. env `MEDUSA_L1_URL`
3. file `~/.config/medusa-l1.url`
4. default: `https://logos-testnet.paradox.computer/`

Optional basic auth: `MEDUSA_L1_BASIC_AUTH="user:pass"`.

## Usage

```sh
# Consensus + time info merged into one object
medusa-l1 info
# {"ok":true,"height":...,"slot":...,"libSlot":...,"lib":"...","mode":...,
#  "slotDurationMs":...,"currentEpoch":...}

# Balance of a node-registered zk public key (64 hex chars, the same
# little-endian Fr hex the node uses in its /wallet/:public_key/balance path).
# "notes" is the COUNT of unspent notes, SDP-locked ones included, because that
# is what the node's own endpoint reports. Only `deposit` subtracts the locked
# ones; see "What deposit checks before it writes".
medusa-l1 balance --pk <zk-pk-hex> [--tip <block-id-hex>]
# {"ok":true,"pk":"...","balance":N,"notes":N,"address":"..."}

# Transfer between node-registered keys (gated)
MEDUSA_L1_ALLOW_WRITES=1 medusa-l1 transfer \
    --to <zk-pk-hex> --amount N --funding-pk <hex> [--change-pk <hex>]
# {"ok":true,"txHash":"..."}     (--change-pk defaults to --funding-pk)

# Bridge-deposit L1 funds to a zone account (gated). The DepositOp consumes a
# whole note, so a note of exactly --amount is required; when none exists the
# tool first mints one via a self transfer and reports "mintedExactNote":true.
# The deposit metadata is the borsh-serialized zone AccountId (32 raw bytes,
# parsed from base58 without the lee crates). --channel defaults to the
# 88...88 channel (64 hex '8's). --amount must be greater than 0.
MEDUSA_L1_ALLOW_WRITES=1 medusa-l1 deposit \
    --recipient <zone-account-base58> --amount N \
    [--channel <64-hex>] --funding-pk <hex> [--strict-locked-check]
# {"ok":true,"txHash":"...","channel":"...","noteId":"...","amount":N,
#  "mintedExactNote":false,"lockedNotesExcluded":0,
#  "sdpDeclarationsRead":true,"sdpLockedNotesSeen":5}
#
# "lockedNotesExcluded" is how many of the funding key's notes were dropped as
# SDP-locked, over the last balance the deposit read.
# "sdpDeclarationsRead" says whether GET /mantle/sdp/declarations answered and
# parsed; it reports that the lookup RAN, not that the key is clear.
# "sdpLockedNotesSeen" is how many distinct locked note ids that answer carried,
# network-wide, so an empty answer is distinguishable from a populated one (a
# node with no tip state answers 200 [], and so can a proxy).
# "sdpDeclarationsRead":false means the lookup could not run at all; the object
# then carries "lockedNotesWarning" with the reason and a null
# "sdpLockedNotesSeen". See "What deposit checks before it writes".
# --strict-locked-check refuses the deposit in that case instead of falling back.

# Read-only channel state
medusa-l1 withdraw-status --channel <64-hex> [--pk <zk-pk-hex>]
# {"ok":true,"channel":"...","channelBalance":N,"withdrawalNonce":N,
#  "tipSlot":N,"accreditedKeys":N}
# With --pk it also includes that key's wallet balance (pkBalance/pkNotes), or
# pkError when the key is not node-registered.
```

Tunables: `MEDUSA_L1_POLL_SECS` (default 180, clamped to 24h) bounds how long
`deposit` waits for the freshly minted exact-value note before giving up. The
clamp is not cosmetic: the deadline is an `Instant` addition, which panics on
overflow, on the far side of the irreversible mint.
`MEDUSA_L1_HTTP_TIMEOUT_SECS` (default 35, `0` disables) bounds every single
HTTP request, so no subcommand can hang forever against an unresponsive node.
The default sits deliberately just ABOVE the node's own 30s request deadline so
the server's real error surfaces instead of an opaque client-side timeout; on
the write verbs a client that gave up first could hide the tx hash of a mint the
node went on to accept. Both tunables fall back to their default on an
unparseable value.

## What `deposit` checks before it writes

The `DepositOp` consumes a note of exactly `--amount` WHOLE, so the deposit
transaction has to be funded from some OTHER note on the same key. The node's
`fund_tx` skips two kinds of note: every note the transaction already consumes,
and every note the SDP ledger has LOCKED. With an empty candidate pool it
returns `InsufficientFunds` no matter how small the fee is. That, and not a fee
figure, is what the pre-flight is built around. No fee is reserved.

### Spendable, not reported

`GET /wallet/:public_key/balance` sums and lists locked notes alongside
spendable ones, so its `balance` is only an UPPER BOUND on what the node can
actually spend. Before it writes anything, `deposit` therefore reads
`GET /mantle/sdp/declarations`, takes the `locked_note_id` of every declaration,
and subtracts that set from the key's notes. Every check below runs on the
result, the SPENDABLE notes, and the exact-value note handed to the `DepositOp`
is picked from that set too, so a locked note is never offered as the deposit's
input either.

That subtraction is **not** a superset of what the ledger has locked, and
nothing here is justified as if it were. It runs in two directions and only one
of them is safe:

- **listed but no longer locked.** A withdrawn declaration stays listed until it
  is garbage collected while the ledger has already released its note, so
  subtracting it can refuse a deposit that would have worked. Nothing is written,
  so this direction costs at most a wasted run.
- **locked but no longer listed.** Upstream unlocks a note only from a
  declaration whose `withdrawn` epoch has been reached, but it garbage-collects a
  declaration on withdrawn **or** inactive. A provider that goes quiet without
  withdrawing has its declaration deleted while its note stays locked forever.
  That note is invisible to this endpoint, so the subtraction misses it. See
  "Residual risk", first bullet.

So a note the subtraction **removes** is proved unspendable or freshly released,
which keeps every refusal below at worst over-cautious; a note it **keeps** is
not proved spendable. The checks below are all refusals, so they stay sound.
Surviving them is not a proof that the deposit can be funded.

- **`--amount 0`**: refused before any request. The ledger rejects zero-valued
  outputs (`Outputs::validate` returns `OutputsError::ZeroValueNote`,
  `core/src/mantle/ledger.rs:113-114`, reached from `TransferOp::validate`,
  `core/src/mantle/ops/transfer.rs:84`), so the mint would produce nothing and
  the tool would poll the whole `MEDUSA_L1_POLL_SECS` and then blame a timeout.
  It is the ledger and not the tx builder that stops it: `add_ledger_output` has
  no value check, and `with_dummy_change_note` deliberately builds a zero note.
- **exact-value note present, and it is the key's only SPENDABLE note**:
  refused. The deposit could not have been funded either way, and nothing has
  been written.
- **no exact-value note, spendable balance below `--amount`**: refused. The mint
  could not have been funded either.
- **no exact-value note, spendable balance exactly `--amount`**: refused. This is
  the one input on which the mint succeeds and still strands the key: the node
  funds the mint from every spendable note there is, the funding delta lands on
  exactly `0`, and the zero-delta path returns without a change note. The deposit
  would then fail after an irreversible write. This is also the case that a
  locked note used to hide: a key holding `{3000, 2000}` spendable plus a locked
  `{100000}` reports a balance of `105000`, which passed the old check at
  `--amount 5000` and minted anyway.
- **no exact-value note, spendable balance above `--amount`**: allowed. The mint
  either leaves a note untouched or emits a change note, and that is what funds
  the deposit.

The threshold is `--amount`, not `--amount` plus a fee, because on the pinned
node revision the fee is provably zero: both genesis gas prices are `0`, every
gas-market update multiplies the previous price (so zero is an absorbing state)
and the total cost is gas times price. The deposit's own `max_tx_fee` of 1000 is
a ceiling the node enforces before it signs or mempools anything, so an
overpriced deposit fails cleanly without moving funds.

### When the locked-note lookup fails

The declaration lookup is a plain read-only GET and can fail: an older node
without the route, a proxy that filters it, a 500, or a body this client cannot
parse. That does NOT abort the deposit by default. It falls back to treating
every note as spendable, which is exactly how this tool behaved before the check
existed, and says so: `"sdpDeclarationsRead":false` plus `"lockedNotesWarning"`
in the success object, and the same notice appended to the `{"error":...}`
string on failure.

The reasoning for still permitting the mint: the lookup REFINES a guard that is
already sound on its own terms, so a failed lookup is never worse than the
previous release, only no better; the residual it fails to catch costs no funds
(see below); and hard-failing would trade an unlikely, recoverable failure for a
certain, frequent one, refusing deposits that plainly work for the overwhelming
majority of keys, which own no locked note at all. Pass
`--strict-locked-check` to invert that trade and refuse rather than write,
which is the right setting for automation and for large amounts.

Be clear about what that flag buys, though. It only covers the case where the
lookup FAILED. A lookup that succeeds still leaves the first residual below, so
the distance between strict and permissive is narrower than the argument above
makes it sound, in both directions.

### Residual risk

The locked-note hole is **narrowed, not closed**, and it is not closed even on
the path where the lookup succeeds. What the guard is: correct for every note
the declarations endpoint knows about. What it is not: complete, because that
endpoint is known upstream to forget notes that stay locked permanently. What is
left, worst first:

- **A permanently locked note the endpoint no longer lists**, on the success
  path, with nothing in the output warning you (`"sdpDeclarationsRead":true`).
  Upstream releases a locked note in exactly one place,
  `unlock_notes_from_withdrawn_declarations`
  (`ledger/src/mantle/sdp/mod.rs:210`, the only non-test caller of
  `LockedNotes::unlock`), and only when the declaration's `withdrawn` epoch has
  been reached. But `is_expired` (`:252`) is true on withdrawn **or** inactive,
  and the inactive arm needs no withdrawal at all, after which `gc_declarations`
  (`:230`) deletes the declaration. `SDPWithdrawOp::validate`
  (`core/src/mantle/ops/sdp/withdraw.rs:42`) then fails `DeclarationNotFound`,
  so the note can never be unlocked afterwards. A provider that simply stops
  sending `SDPActive` therefore leaves its note locked forever and absent from
  `/mantle/sdp/declarations`, and the shipped devnet settings
  (`inactivity_period: 1`, `retention_period: 1`) put that three epochs after it
  goes quiet. Such a note is counted by the balance endpoint, is not subtracted
  here, and `fund_tx` still refuses to spend it, so a deposit can mint
  irreversibly and then fail to fund exactly as before this check existed.
  `--strict-locked-check` does **not** help: the lookup succeeded. Recovery is
  the same as below (top the key up by any amount and re-run; the exact-value
  note is already there) and the mint hash is carried, so no funds are lost.
  Closing this would need the ledger's own locked-note set, which no endpoint on
  the pinned revision exposes.
- **The lookup did not run** (`"sdpDeclarationsRead":false`) and the key really
  does hold a locked note that inflates its reported balance. The mint is
  allowed, lands, and the deposit POST then fails with the node's
  `InsufficientFunds`. The post-mint re-check does not catch this one: with an
  empty locked set it degenerates to counting notes, and the locked note is one
  of the notes it counts. The error does carry the mint's tx hash and the
  degraded-lookup notice, so topping the key up by any amount and re-running
  completes the deposit: the exact-value note is already there. No funds are
  lost. `--strict-locked-check` refuses instead of writing.
- **The lock was taken after the snapshot.** The declaration set is read once,
  before the mint. A note that becomes SDP-locked between that read and the
  deposit POST is invisible to both the pre-flight and the post-mint re-check.
  Same outcome, same recovery, and the mint hash is again carried.
- **The over-approximation refuses too much.** A declaration that has been
  withdrawn but not yet garbage collected still lists its `locked_note_id` while
  the ledger has unlocked it. Such a note is treated as unspendable, so a deposit
  can be refused, or a second exact-value note minted, when neither was
  necessary. Nothing is lost and it resolves itself when the declaration is
  collected. This is not refined using the declaration's own withdrawal epoch on
  purpose: the deployed public node calls that field `withdraw_at` while the
  pinned revision this client is built against calls it `withdrawn`, so its
  semantics cannot be verified against the source, and guessing wrong there
  would err in the unsafe direction.
- **Non-zero gas prices.** The tightness of the `spendable > --amount` threshold
  rests on the fee being zero, which is a property of the pinned node revision
  rather than a promise of the protocol. On a chain with non-zero gas prices the
  same zero-delta case moves to `spendable == --amount + mint fee`, which this
  client cannot compute, and a mint could leave a change note too small to pay
  the deposit's own fee. Both fail after the mint, again without losing money,
  and again carrying the mint hash. Inventing a fee constant to cover that would
  refuse deposits that plainly succeed today, so the number is not invented.

Every failure after a successful mint reports that mint's tx hash, so the
irreversible step always leaves the user a handle: a poll that keeps erroring, a
poll that times out, the post-mint re-check, the metadata encode, the deposit
URL join, the deposit POST itself, and the rendering of its response.

That sentence is meant literally, so the two ways it used to be false are gone.
The three `?` on the encode, the `try_into` and the URL join now decorate their
error with the hash instead of propagating it bare, unreachable as they are. And
`MEDUSA_L1_POLL_SECS` is clamped to 24h: the poll deadline is
`Instant::now() + Duration::from_secs(...)`, whose `Add` **panics** on overflow,
and that line runs after the mint, so an absurd value in the environment used to
replace the JSON error object with a bare Rust panic.

## Build

```sh
cargo build --release --manifest-path wallet/l1/Cargo.toml
# or as part of the full wallet build:
bash wallet/build.sh
```

The crate is standalone (own `[workspace]`), binary at
`wallet/l1/target/release/medusa-l1`.

## PHASE 2 (stub)

Self-custody is under investigation: client-side zk signing plus local UTXO
discovery, so keys never live on the node and `balance`/`transfer`/`deposit`
work for any wallet-held key instead of only node-registered ones. Not designed
yet; phase 1 deliberately ships the node-custody model above.
