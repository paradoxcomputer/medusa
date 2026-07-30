# LEZ v0.2.0 migration notes

Status: patch series ported and compiling; NOT yet the shipping default.

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

## Still to do before cutover

- flip `LEZ_BASE_REV` / `PATCH_DIR` defaults in wallet/build.sh, and set kEngineEpoch="v020"
- update rc5 references in release-linux.yml, release-macos.yml, README.md
- the Paradox sequencer must upgrade at the same time: a v0.2.0 wallet cannot transact against
  an rc5 zone, because the wallet embeds the program ImageIDs it proves against
