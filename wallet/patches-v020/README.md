# LEZ v0.2.0 migration notes

Status: DEFAULT as of 0.3.0. wallet/build.sh targets v0.2.0 + patches-v020.

Build with the v0.2.0 engine:

    LEZ_BASE_REV=v0.2.0 LEZ_PATCH_DIR=patches-v020 bash wallet/build.sh

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
  `account import public --private-key` with the label applied via `account label`
- the token verbs (tokens / direct-holdings / token-shield / consolidate) are gated on a
  `wallet check-health` probe, which compares local program ImageIDs against the sequencer's.
  Previously a mismatched zone made them return empty arrays and zero balances silently.

## Sequencer upgrade: DONE 2026-07-31

seq-testnet.paradox.computer runs v0.2.0 as of 2026-07-31 00:12 UTC (binary from this
patch series' .lez-build, v0.2.0 + the two wallet-only patches; sequencer code is pure
upstream). Same channel 8888…88, same bedrock signing key, fresh rocksdb (the rc5 chain
ended at block 42036; the old db is kept on the box as data/rocksdb.old-rc5-8888, the
rc5 binary as sequencer_service_l1.bak-rc5). getProgramIds matches testnet.lez.logos.co
exactly and `wallet check-health` passes, so publishing 0.3.0 to the catalog is unblocked.

Historical context (why the upgrade had to be simultaneous with 0.3.0): Program ImageIDs
are a hash of the program binary, identical across installations of a given tag but
different between tags, and the wallet proves against the ids it was compiled with. A
0.3.0 wallet therefore could not transact against the rc5 zone at all - the probe above
refuses, by design.
