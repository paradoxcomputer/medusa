# Changelog

Notable changes to Medusa, newest release first. New releases are appended above the
previous one.

`medusa_core` and `medusa_ui` share an ABI and are versioned and released together: install
them as a pair, never one alone.

Releases before 0.3.0 predate this file. See the git history for those.

## 0.4.0 - 2026-08-11

The engine moved again, the token faucet became real, and the zone gate that 0.3.0 introduced
turned out to be bypassable. **A 0.4 wallet talks to a v0.2.2 zone and cannot talk to a v0.2.0 or
rc5 one**, for the same reason 0.3 could not talk to rc5: program ImageIDs are a hash of the
program binaries and the wallet proves against the ids it was compiled with.

### the LEZ v0.2.2 cutover

- the engine is pinned to upstream commit `47eba256479f6f785acbd138834340703cd03401` (tag v0.2.4,
  which rebuilt no guest program, so it is the v0.2.2 program generation) plus
  `wallet/patches-v022/`. Both public zones reset their chains for this on 2026-08-07.
- **`kEngineEpoch` is now `"v022"`.** It was left at `"v020"` when the engine moved, which is the
  exact silent crossover the constant exists to prevent: `SequencerCore::start_from_config` reuses
  an existing rocksdb with no version check, so a local devnet started on a chain whose accounts
  are owned by program ids the binary no longer knows. **A local devnet therefore starts from a
  fresh genesis on upgrade**; the old directory is left on disk and is not read.
- the v0.2.2 CLI rejects the old flat `sequencer_addr` wallet config with
  `missing field 'sequencers'`; the wrapper writes the new list shape and reads either.
- the built-in **Logos public testnet** zone was verified against this build on 2026-08-11: it
  serves the same v0.2.2 program ids. It is a third-party zone and can move, which is what the
  per-zone compatibility probe and the "Zone build mismatch" modal exist to catch.

### the on-chain token faucet

- the `medusa_faucet` LEZ program is deployed and funded on both public zones, and the faucet now
  **pays into the claimant's own ATAs** instead of minting holdings it kept. Claims are rejected if
  they name the same token twice.
- **`medusa-faucet-client` is now bundled in the release.** It never was before, and it is needed
  by two paths, not one: the on-chain claim, and the wrapper's `init-holding` bootstrap.
- a claim reports progress from the moment the job is created rather than trusting a one-shot push,
  so a queued job no longer vanishes from the UI.

### tokens and privacy

- **token shielding works from an ATA-only balance again.** A shield proves spend authority from a
  signing key and an ATA is a PDA, so it can never be the sender; the wrapper now mints and
  initialises a keytree holding for the account and shields from that. Previously a user could
  claim a token and then never shield it. The "issuer-only" limitation in the 0.3 notes is gone.
- **every account has its own token vault**, instead of one holding shared by the whole wallet.
- the Shield screen offers the account's whole token balance rather than only its direct holdings,
  shows shielded balances, refreshes the list after a shield, and names the token.
- the token registry survives a torn write and a concurrent writer.
- tokens are shown again after a claim, and the UI distinguishes "balance is zero" from "balance is
  still loading" instead of rendering an unknown as 0.

### security

- **the zone gate is keyed on the store, not on the session.** 0.3.0 gated the zone verbs with
  `!m_password.isEmpty() && !authorize(password)`, and `clearSessionPassword()` is ungated by
  design: three calls with no secret (lock, `addZone(attacker url)`, `setActiveZone`) left the
  wallet talking to an attacker's sequencer, and the next ordinary Send from the wallet's own UI
  went there. The gate now asks whether the STORE ON DISK can prove a password, which no caller can
  flip. Consequence for users: **adding, editing, removing or switching a zone needs the wallet
  unlocked**; the zone list stays readable while locked and the controls say why they are absent.
  Onboarding is unaffected: a fresh install has no store, so the verbs are free there.
- **`removeZone` is gated too.** It was the ungated member of the same class: it mutates the zone
  list and moves the wallet off the zone it deletes.
- **`importKey` is gated**: planting a signing key the caller chooses is a capability.
- `curl` is launched with `-q`, so a planted `~/.curlrc` cannot steer the zone health probe that
  paints the connection indicator.
- the plugin's restore timeout was raised from 300s to 900s to match the wrapper's budget. A
  measured restore takes ~353s, so the plugin was killing the restore from above while the wrapper
  waited below it, leaving a half-synced store.
- the UI renders every string as plain text and refuses a remote dApp icon.
- the Connect SDK never returns a non-object and never brands the caller's action as its own.
- unlock probes the store **without touching the chain**, so a dead or mismatched zone no longer
  blocks unlocking (and a wrong password is no longer reported as a timeout under load).

### zones, connect and the UI

- **the Paradox · Tor zone works out of the box.** Its `.onion` was never baked into the build, so
  every released install listed a zone that could not connect and only a machine with a
  hand-written `~/.config/medusa-sequencer.onion` could use it. The address is now compiled in
  (`paradoxj4xy6orxue7y7qsk4rxutzme6patcpo65liw22jlmlpxlncyd.onion`), still overridable by
  `MEDUSA_SEQ_ONION` or that config file, and an override is taken verbatim: append `:<port>` for
  a hidden service that does not publish on 80.
- the Tor zone now publishes its address like the other two, so the UI can show it next to the zone
  name. That display is what a user checks against a wallet that has been repointed, and it was the
  one zone row with nothing to show.

- a transport tag is shown wherever a zone is named, and built-in names no longer carry a transport
  suffix. The add-zone form's Transport toggle is **gone**: the address decides (a `.onion` is a Tor
  zone), so the toggle changed nothing while still allowing "Tor" next to an `https://` URL.
- a Tor connect that fails says why, instead of showing "Connecting" forever, and can be stopped.
- every approval that comes from a dApp shows the operating network and the endpoint.
- the 10s account poll no longer hits the chain, which used to spam error toasts on a slow zone.
- fixed: an account row that could not be clicked, and balances that never arrived.

### packaging and tests

- the wallet CLI is named `medusa-wallet`, and `version()` tracks the manifest.
- every Q_INVOKABLE stays under the bridge's five-argument limit, with a connect abort added.
- the wrapper validates amounts before touching the chain.
- suites at this release: **C++ 155, wrapper 28, SDK 48, Rust 2**, all green.

## 0.3.0 - 2026-07-31

Two things happened in this release: the wallet moved to the LEZ v0.2.0 engine, and the
module was hardened against other software running under your own user account.

### the LEZ v0.2.0 cutover

**A 0.3 wallet talks to a v0.2.0 zone and cannot talk to an rc5 zone.** Program ImageIDs are
a hash of the program binary: they are identical across installations of a given engine
version and different between versions, and the wallet proves against the ids it was
compiled with. There is no compatibility mode and there cannot be one.

- the engine is LEZ v0.2.0, pinned to upstream commit
  `a58fbce2ff48c58b7bb5001b1a27e64b9596ee3a` (tag v0.2.0) plus the patch series in
  `wallet/patches-v020/`. The rc5 series is kept alongside it and can still be built
  explicitly, but it is no longer the default.
- pointing a 0.3 wallet at an rc5 zone now says so. A per-zone `wallet check-health` probe
  compares the wallet's program ids against the sequencer's `getProgramIds` and reports
  `unknown`, `ok` or `mismatch` through `getSequencerStatus`. On `mismatch` an operation
  raises a blocking "Zone build mismatch" modal naming the zone, the endpoint the wallet
  actually dials, and what to do about it. Previously the wallet just returned empty token
  lists and zero balances.
- the token verbs in the wallet wrapper (`tokens`, `direct-holdings`, `token-shield`,
  `consolidate`) refuse on a mismatched zone with "this zone runs a different LEZ build than
  this wallet (program ids differ)". Previously they answered with an empty array, which is
  a silent wrong answer.
- an unresolvable token name is no longer cached. A miss is usually transient or a zone
  mismatch, and caching the placeholder pinned a wrong ticker forever.
- new built-in zone: **Logos - public testnet** (`https://testnet.lez.logos.co/`), the
  official logos-co zone, which runs the same v0.2.0 engine this build compiles against.
  Override the endpoint with `MEDUSA_LOGOS_TESTNET_URL` or
  `~/.config/medusa-logos-testnet.url`. Accounts and keys are shared across zones; balances
  are per zone.
- the local sequencer's home is now versioned per engine epoch:
  `sequencer-<zone>-v020`. `SequencerCore::start_from_config` reuses an existing rocksdb with
  no version check, so without this a v0.2.0 binary would have started on an rc5 chain whose
  accounts are owned by program ids it no longer knows. A local zone therefore starts from a
  fresh genesis; the old directory is left on disk untouched and is not read.
- fixed: **Export key** called `account export-key -a <id>`. The patch declares `account_id`
  as a long-only flag, so `-a` was always rejected and the verb never worked. It now passes
  `--account-id`.
- fixed: **Import a private key** called `account import-key`, which upstream has never had.
  It is now `account import public`, with the label applied afterwards through
  `account label`.

### zone failures are reported instead of failing silently

- a blocking modal for any operation a zone cannot serve, in two flavours: "Zone connection
  offline" and "Zone build mismatch". It names the zone, the endpoint, and reason-specific
  advice, and Retry re-checks health and closes on success. A send while the zone was down
  used to produce no feedback at all.
- `getSequencerStatus` now returns `endpoint`, `healthy` and `compat` for every zone kind,
  and for a local zone a machine-readable `reason`: `binary-missing`, `launch-failed`,
  `exited`, `unhealthy` or `mismatch`. A just-spawned sequencer gets a 15 second grace
  window before a non-answer counts as a failure, so "still starting" is not reported as
  broken.
- the local sequencer's merged output goes to `<sequencer home>/sequencer.log`, truncated per
  launch, and the UI points at it. An unexpected exit is recorded with its exit code instead
  of leaving the zone in a silent "Connecting...".
- operation errors are classified: transport-class failures raise the offline modal,
  "program ids differ" raises the mismatch modal, and the error toast persists so it can be
  copied.

### security

This release hardens the wallet against other software running under your own user account.
The session password is now held in one place, set only after the wallet CLI has actually
opened your encrypted store, and every operation that spends or reveals a key must present
it; no command can mint a session, and erasing or restoring a wallet never destroys the copy
it sets aside. Every program the wallet launches is resolved to an absolute path in a
directory you cannot write, and the environment those programs receive no longer names any
location you can write: a file planted in your Python user-site directory, or in the dynamic
loader's search paths, can no longer run inside the wallet or read your password.

In detail:

- 14 verbs are gated on the session password: `sendTransfer`, `startSendTransfer`,
  `startSendToken`, `startShield`, `startDeshield`, `startPrivateTransfer`,
  `startPrivateTransferForeign`, `consolidateToken`, `approveAction`, `approveZone`,
  `exportMnemonic`, `exportKey`, `resetWallet` and `restoreWallet`. The gate compares the
  presented password against the session in constant time and consults nothing else, in
  particular no file an attacker at your uid can rewrite.
- `setSessionPassword` is removed. It let a caller install a session password of its own
  choosing, which is the whole gate undone. A session is now established in exactly one
  place, by opening the store on disk with the password that was typed.
- `setCliPath` always refuses, and a CLI path previously saved in settings is no longer read.
  The stored value is still reported so a poisoned install shows what was planted, and
  Settings marks it as ignored. `MEDUSA_WALLET_CLI` (absolute path, set before launching) is
  the one remaining override. The same applies to the stored sequencer path.
- every program the module launches goes through one function that refuses anything which is
  not an absolute path, resolved from the module's own `bin/` or from a root-owned system
  directory. No setting influences it.
- the environment handed to those programs drops every uid-writable directory from `PATH`,
  removes every inherited `PYTHON*` variable and sets `PYTHONNOUSERSITE=1`,
  `PYTHONSAFEPATH=1`, `PYTHONBREAKPOINT=0` and `PYTHONDONTWRITEBYTECODE=1`, and filters
  uid-writable entries out of `LD_LIBRARY_PATH`, `LD_PRELOAD`, `LD_AUDIT`, `GCONV_PATH` and
  the Qt/QML plugin paths.
- the imported signing key travels on the CLI's stdin, not argv. `/proc/<pid>/cmdline` is
  world-readable and the wrapper re-exec'd its argv, so `--private-key <hex>` put the key on
  two command lines at once for the length of the import. Patch `0003` makes `--private-key`
  optional and reads the key from stdin line 2, after the password line, the same convention
  `restore-keys` already used for the recovery phrase. The flag still parses for existing
  callers, and the wrapper lifts its value onto stdin so it never reaches the engine's argv.
- secrets are redacted from the module log. Secret-bearing flags are redacted by flag name
  rather than by call site, and the output of `export-key`, `export-mnemonic`,
  `restore-keys` and `account import` is never echoed: a success line reads
  `ok: <redacted, N bytes>`, and a failure is redacted too, because the argument parser
  quotes the value it rejected.
- unlock is rate limited: three attempts with no delay for honest typos, then 1s, 2s, 4s and
  so on, capped at a minute. The backoff lives in the one function every verb uses to ask
  "does this password open the store", so no verb is an unthrottled oracle. A wrong guess no
  longer disturbs a session that is already working.
- the wallet locks itself after 15 minutes idle, with a warning toast before it fires.
  Passing the gate is what counts as activity, so the 10 second background account poll does
  not hold the lock open. A "Lock now" button was added to the top bar and to Security and
  Backup. `MEDUSA_IDLE_LOCK_MS` overrides the timeout for testing.
- reset and restore stay usable while locked, which is the state a user who forgot their
  password is in, and neither ever deletes: the store is renamed to
  `storage.json.bak-<UTC timestamp>`, with a counter appended on a same-second collision so
  a second reset cannot destroy the first copy. Security and Backup now lists every store the
  module has moved or copied aside, with a put-back flow, instead of leaving the user to
  discover it.
- a wallet whose storage predates encryption is now described honestly. `getSecurityState`
  reports `protected: false` with a plain warning, the gate refuses with reason
  `unencrypted` and carries the route out, and the controls that could only refuse are
  disabled with the reason on their own row. Reading accounts and balances, the faucet,
  receiving, importing a key, restoring from a phrase, erasing, and setting a password all
  still work, and setting a password keeps every account.
- the UI now routes every refusal reason to a real outcome rather than a generic failure:
  `locked` sends you to the lock screen, `unauthorized` clears the stale session and asks you
  to unlock again, `rate-limited` shows how long to wait, `unencrypted` explains that nothing
  can prove who is asking and points at the fix.

#### what this does not protect against

Stated plainly, because a security note that only lists wins is not useful:

- **a native Basecamp core module runs inside the wallet's own process.** Basecamp loads
  those as shared libraries into its address space, so a hostile one can read anything the
  wallet holds, including the session password, and nothing in this release applies to it.
  Only install core modules you trust.
- **a development install is not protected.** With no bundled `bin/` directory the module
  falls back to `~/.local/bin`, which you can write, so anything that can drop a file there
  can be executed as the wallet's CLI.
- **it is not a defence against malware that replaces the wallet's own bundled binaries.**
- **another program running as you can still repoint the wallet at a different sequencer**,
  by writing one small file in your home directory. Your keys stay yours and every
  transaction is still signed locally, but the balances you see and the network you broadcast
  to could be someone else's. Check the server address shown next to the network name before
  moving anything significant.
- **a wallet whose storage predates encryption is readable by anything running as you**, and
  no password gate can change that. The app now says so and disables the operations it cannot
  honestly perform until you set a password on it, which keeps all your accounts.

### Connect SDK and the Tip Jar

- no dApp-facing verb changed. `connectRequest`, `sessionInfo`, `requestAction`,
  `actionStatus`, `requestZone`, `revokeSession`, `getBalance`, `getTokens` and `getJob` all
  keep their arity, and none of them became gated. The verbs that grew a password argument
  are wallet-UI verbs, which no dApp calls. Existing dApps need no change.
- approving an action or a zone now needs an unlocked wallet. The approval sheets are shown
  only while the wallet is unlocked, and the lock screen says which app is waiting. The
  request stays pending, so a dApp that keeps polling sees it resolve after the user unlocks.
- on a wallet whose store is not encrypted, an approval can never succeed: the wallet
  disables Approve and shows the route out, and a dApp sees `pending` until the 15 minute TTL
  expires and then `approval timed out`.
- documentation corrections in `sdk/medusa-connect.js` and `sdk/medusa-connect.d.ts`, which
  described a surface the wallet does not have:
  - `getBalance` returns `{ ok, output }`, the wallet CLI's free-text `account get` output
    wrapped. There is no top-level `balance` field. Parse it out of `output` yourself.
  - `getBalance`, `getTokens` and `getJob` accept a `sessionId` and discard it. The wallet
    verbs take no session at all and answer any caller, connected or not, so a successful
    read is not evidence that your session is live. Use `isConnected(sessionId)` for that.
    `getAccounts` is the one genuinely session-scoped read.
  - the approval TTL is documented as 15 minutes, applied when you poll.
  - remaining rc5 references corrected to LEZ v0.2.0.
- `examples/tip-jar/install.sh` now refuses to install when the vendored
  `qml/medusa-connect.js` differs from the canonical `sdk/medusa-connect.js`, printing the
  diff. The two drifted once and the two install routes then delivered different bytes.
- `@paradoxcomputer/medusa-connect` and `tip_jar` to 0.2.1 (documentation only; no behaviour
  change).

### build, CI and provenance

- `wallet/build.sh` pins both upstreams to a full 40 character commit id and asserts the
  checked-out HEAD before a single patch is applied: logos-execution-zone
  `a58fbce2ff48c58b7bb5001b1a27e64b9596ee3a` and diaphani
  `704192fd4e472f7fb3fde30c9c069aee565807af`. Tags are mutable refs on third-party
  repositories, and this script builds the binary that holds the keystore and signs
  transactions, so a moved tag would ship a compromised engine and still pass every hash
  check computed over it. `git fetch --tags` is no longer `--force`, which would have
  overwritten a correct local tag with a moved upstream one.
- builds are reproducible enough to compare: `SOURCE_DATE_EPOCH` defaults to the repository
  HEAD's commit time, and `--remap-path-prefix` keeps the builder's home out of the
  artifacts. The previously shipped engine carried 569 absolute paths from one developer's
  home directory. `MEDUSA_NO_REMAP=1` disables the remaps for debugging.
- new `rust-toolchain.toml` pins rustc 1.94.0 for everything built out of this repository.
  The engine clone carries its own toolchain file asking for the same version, and the
  nearest file wins, so this cannot move the engine's compiler behind upstream's back.
- CI and the release workflows are pinned: every `uses:` to a commit sha with the tag in a
  trailing comment (`nix-installer-action` was tracking a branch on a third-party org and ran
  first), the release runner to `ubuntu-24.04` rather than `ubuntu-latest`, the Rust
  toolchain to 1.94.0 and risc0 to 3.0.5 with the cache key naming both versions, and the
  bundled `tor` to an exact apt version which is asserted after install. That last step used
  to end in `|| true`, which could publish a `medusa_core` with no Tor in it at all. Staging
  `diaphani-forward` no longer swallows a failure either.
- the darwin merge step is bound to the release commit instead of taking the newest
  successful macOS run on any ref.
- new `scripts/gen-provenance.sh` writes a provenance record next to the artifacts: the
  pinned upstream commits, the sha256 of every patch applied on top of them, of every engine
  binary and of every `.lgx` shipped, plus the toolchain versions, `SOURCE_DATE_EPOCH` and
  `RUSTFLAGS`. The release workflows generate and upload it. The pins are read back out of
  `wallet/build.sh` so the record cannot drift from the thing it describes. Honest caveat
  recorded in the file itself: the engine binaries should rebuild to the listed hashes, the
  `.lgx` packages will not, because the nix module build and lgx packaging are not yet
  byte-reproducible.

### repository additions that are not shipped to users

Both are built by `wallet/build.sh` and live in the source tree. Neither is bundled into the
`medusa_core` package, and neither is reachable from the wallet UI. They are operator and
developer tooling.

- `wallet/faucet/` : `medusa_faucet`, an on-chain LEZ program (a risc0 guest) with its shared
  instruction types and the `medusa-faucet-client` operator CLI. It is an on-chain
  replacement for the client-side treasury faucet, holding each whitelist token's supply in a
  per-definition treasury PDA and enforcing a 6 hour per-claimant cooldown on-chain. The
  guest program is not built by `build.sh` and is not deployed by this release.
- `wallet/l1/` : `medusa-l1`, a phase-1 Bedrock L1 client. Node-side custody: it holds no key
  material and reads no password, reads are plain GETs against a node's REST API, and writes
  refuse unless `MEDUSA_L1_ALLOW_WRITES=1` is set in the environment.

### upgrading from 0.2.x

1. **Your zone must run LEZ v0.2.0.** A 0.3 wallet cannot transact against an rc5 zone at
   all. If you point it at one you get a "Zone build mismatch" modal naming the zone, which
   is the design, not a bug. Switch to a v0.2.0 zone, or stay on 0.2.x until your zone
   operator upgrades. Both built-in remote zones ran v0.2.0 when this release was cut: the
   Logos public testnet, and the Paradox clearnet zone as of 2026-07-31.
2. **Install `medusa_core` and `medusa_ui` together.** Every gated verb gained a trailing
   password argument, so a 0.3.0 UI does not fit a 0.2.x core and a 0.2.x UI does not fit a
   0.3.0 core. Updating one alone leaves calls that do not match anything.
3. **Rebuild the engine**: `bash wallet/build.sh`. The patch series is three patches now, and
   `account import public` needs `0003`. Against an older engine, importing a private key
   fails closed with the argument parser's "the following required arguments were not
   provided: --private-key" rather than falling back to argv. Everything else works on an
   older engine.
4. **A local devnet starts over.** The sequencer home is now `sequencer-<zone>-v020`, so the
   local chain begins at a fresh genesis and you will need to claim the faucet again. Your
   old `sequencer-<zone>` directory is untouched but nothing reads it. Accounts and keys live
   in `storage.json` and are unaffected.
5. **The Paradox clearnet zone was rebuilt on a fresh database** when it moved to v0.2.0 on
   2026-07-31 (recorded in `wallet/patches-v020/README.md`), so anything held on that zone
   before then is not on the new chain. Accounts and keys are local and unaffected.
6. **A CLI path saved in settings is ignored.** Settings shows the stored value marked as
   ignored. Set `MEDUSA_WALLET_CLI` to an absolute path before launching Basecamp if you need
   a different build, or reinstall the module so its `bin/` directory is bundled.
7. **The wallet locks itself after 15 minutes idle.** Reading balances is not activity, so a
   long read-only session will lock. A warning toast appears about 90 seconds beforehand.
