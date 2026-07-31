# LEZ v0.2.0 migration notes

Status: DEFAULT as of 0.3.0. wallet/build.sh targets v0.2.0 + patches-v020.

Build with the v0.2.0 engine:

    LEZ_BASE_REV=v0.2.0 LEZ_PATCH_DIR=patches-v020 bash wallet/build.sh

## The series

    0001  encrypted storage (Argon2id + AES-256-GCM), `account list --json`,
          `account export-mnemonic` / `export-key`, keycard subcommand disabled
    0002  pyo3 / keycard_wallet behind a non-default `keycard` feature, so the
          binary carries no libpython dependency
    0003  `account import public` takes the private key on stdin, not argv

### 0003: the private key must not be on a command line

`account import public --private-key <hex>` put a raw 32-byte signing key on the process
command line. `/proc/<pid>/cmdline` is mode 0444 and a stock box mounts /proc with no
hidepid, so every local uid could read the key for as long as the import ran; and because
`module/scripts/wallet-wrapper` re-exec'd its argv verbatim, the key was on TWO command
lines at once, the wrapper's and the engine's. Measured with a stub engine and a /proc
scan, before and after:

    before   KEY VISIBLE ON 2 COMMAND LINE(S)   wrapper pid + engine pid, both mode 444
    after    KEY VISIBLE ON 0 COMMAND LINE(S)

The fix follows the convention the CLI already had rather than inventing one. `main()`
consumes stdin line 1 as the password before dispatching any subcommand
(`lez/wallet/src/main.rs`), the same way `restore-keys` consumes a recovery-phrase line
and then a password line. So `private_key` becomes `Option<lee::PrivateKey>` and, when the
flag is absent, `read_private_key_from_stdin()` reads line 2. That helper sits next to
`read_password_from_stdin` and `read_mnemonic_from_stdin` in `lez/wallet/src/cli/mod.rs`,
holds the value in a `zeroize::Zeroizing<String>`, and fails with a bare "Invalid private
key" that never echoes the value, since callers log stderr.

`--private-key` still parses. Removing it would break existing callers for no security
gain, because a leak on argv has to be prevented by the caller: a process cannot rewrite
its own `/proc/<pid>/cmdline` from Python. It is documented in `--help` as the unsafe form,
the module no longer passes it, and the wrapper strips it and lifts the value onto stdin so
it never reaches the engine's argv either.

Wrapper and engine ship in the same module package, so the pairing matters:

    new wrapper + new engine   the key travels on stdin only (this release)
    new wrapper + old engine   clap: "required arguments were not provided: --private-key",
                               loud, never a silently wrong import
    old wrapper + new engine   unchanged, the flag still parses

Equivalence check: the same key imported through `--private-key` on the unpatched engine
and through stdin on the patched one both yield
`Public/4SNEckW9ekZmcrX7vbwZcdmdnmzjNj1m1znyqq3nKqjL`.

## Verified equivalent between v0.2.0-rc5 and v0.2.0 (no Medusa change needed)

- Sequencer config struct: `lez/sequencer/core/src/config.rs` is byte-identical at both tags
  (same git blob), so the `kSeqConfigTemplate` embedded in WalletPlugin.cpp still deserialises.
- Genesis format: `GenesisAction::{SupplyAccount,SupplyBridgeAccount}` unchanged.
- `standalone` cargo feature on sequencer_service still exists, so build.sh's two builds are fine.
- Sequencer CLI (`<config> --port N`), env vars, and required files unchanged.
- JSON-RPC is purely additive: `getChannelId` was added, nothing renamed or removed.
- risc0 3.0.5 and logos-blockchain-circuits v0.5.3 are identical at both tags, so the CI
  cargo-risczero cache needs no change.

## The real breakage: chain state is not portable

v0.2.0 rebuilt every risc0 program, so all program ImageIDs changed. Consequences:

- the faucet/bridge system account ids are derived from program ids, so they move;
- the genesis commitment tree differs;
- `SequencerCore::start_from_config` reuses an existing rocksdb with NO version check, and
  `writeSeqConfig()` deliberately preserves the home across runs.

So an in-place upgrade would silently run a v0.2.0 binary against an rc5 chain. `seqHome()` is
now epoch-suffixed to prevent that. `kEngineEpoch` is "" today (paths unchanged for rc5 users);
set it to "v020" in the SAME commit that flips build.sh's defaults.

## Done in the cutover

- build.sh defaults flipped to v0.2.0 / patches-v020 (rc5 still buildable via the env vars above)
- kEngineEpoch = "v020", so local zones start from a fresh genesis instead of an rc5 rocksdb
- rc5 references updated in release-linux.yml, release-macos.yml, README.md and the four
  user-facing wrapper messages (the quirks they describe still exist in v0.2.0 - only the
  version string was stale)
- `account export-key` now passes --account-id (the patch declares no short form, so -a was
  always rejected) and `import-key`, which never existed upstream, is now
  `account import public` (key on stdin since 0003) with the label applied via `account label`
- the token verbs (tokens / direct-holdings / token-shield / consolidate) are gated on a
  `wallet check-health` probe, which compares local program ImageIDs against the sequencer's.
  Previously a mismatched zone made them return empty arrays and zero balances silently.

## Sequencer upgrade: DONE 2026-07-31

seq-testnet.paradox.computer runs v0.2.0 as of 2026-07-31 00:12 UTC (binary from this
patch series' .lez-build, v0.2.0 + the wallet-only patches; sequencer code is pure
upstream). Same channel 8888…88, same bedrock signing key, fresh rocksdb (the rc5 chain
ended at block 42036; the old db is kept on the box as data/rocksdb.old-rc5-8888, the
rc5 binary as sequencer_service_l1.bak-rc5). getProgramIds matches testnet.lez.logos.co
exactly and `wallet check-health` passes, so publishing 0.3.0 to the catalog is unblocked.

Historical context (why the upgrade had to be simultaneous with 0.3.0): Program ImageIDs
are a hash of the program binary, identical across installations of a given tag but
different between tags, and the wallet proves against the ids it was compiled with. A
0.3.0 wallet therefore could not transact against the rc5 zone at all - the probe above
refuses, by design.
