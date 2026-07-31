# medusa_faucet: the on-chain token faucet

An LEZ program (risc0 guest) that replaces the client-side treasury faucet: it owns the
whitelist tokens' supplies in per-definition treasury PDAs and dispenses a pseudorandom
amount of each to a claimant, with a 6-hour per-claimant cooldown enforced on-chain via
the `CLOCK_01` system clock account.

Layout:

- `shared/` - `medusa_faucet_shared`: the instruction enum + PDA/amount derivations,
  used by BOTH the guest and the client so wire encodings can never drift.
- `guest/` - `medusa_faucet`: the risc0 guest program (the thing that gets deployed).
- `client/` - `medusa-faucet-client`: host binary to deploy / init treasuries / claim.

All path-deps resolve against the PATCHED v0.2.0 tree in `wallet/.lez-build`
(medusa-build = tag v0.2.0 + `wallet/patches-v020/*`), the same tree the deployed
wallet and sequencer are built from.

## Program design

Instructions (risc0-serde encoded, defined once in `shared/`):

- `InitTreasury` - pre-states `[definition, treasury_pda]`. Idempotently creates the
  faucet's token holding at its own PDA (`SHA-256("MEDUSA/treasury/" || def_id)`) by
  chain-calling the token program's `InitializeAccount` with
  `pda_seeds = [treasury_seed]` (the `ata::Create` pattern). The token program id is
  derived from the definition's `program_owner`, never hardcoded.
- `Claim` - pre-states `[recipient_1..n, treasury_1..n, marker, CLOCK_01]`. Requires
  every recipient holding to be claimant-signed; enforces the cooldown from the marker
  PDA (`SHA-256("MEDUSA/claim/" || claimant_id)`, data = `last_claim_ms: u64` borsh,
  created on first claim via `Claim::Pda`); then emits one chained
  `token::Transfer` per treasury with the treasury pre-state authorized via
  `pda_seeds = [treasury_seed]` (the `pinata_token` pattern).

Amounts: `MIN + (SHA-256(clock_ms || claimant_id || def_id) mod (MAX-MIN+1))` with
`MIN=10`, `MAX=500` (the client-side faucet's bounds); cooldown 21 600 000 ms (6 h).

Everything is public-flow (no privacy paths), and the guest respects the
well-behavedness rules in `lee/state_machine/core/src/program.rs::validate_execution`
(only the owner decreases balances / mutates data; balances conserved at the faucet
level - token amounts move inside the chained token-program calls).

## Building

Client (host side; also wired into `wallet/build.sh`):

```sh
cargo build --release --manifest-path wallet/faucet/client/Cargo.toml
# -> wallet/faucet/client/target/release/medusa-faucet-client
```

Guest, reproducible (docker; this is what a deployed zone must be given, since the
ImageID is derived from the binary):

```sh
cd wallet && cargo risczero build --manifest-path faucet/guest/Cargo.toml
# -> wallet/target/riscv32im-risc0-zkvm-elf/docker/medusa_faucet.bin  (+ prints the ImageID)
```

Guest build plumbing (why it looks the way it does):

- `wallet/Cargo.toml` makes `wallet/` the guest's workspace root ONLY so the docker
  build context contains the `.lez-build` path-deps (cargo-risczero copies the
  workspace root into the container); `wallet/.dockerignore` keeps that copy lean.
- The docker builder image's `risc0` toolchain is rustc 1.88, so `wallet/Cargo.lock`
  pins a handful of transitive crates to the same 1.88-compatible versions
  `.lez-build/Cargo.lock` uses (e.g. `enum-ordinalize` 4.3.2, `ruint` 1.17.2). If you
  regenerate the lockfile and the in-docker build complains "rustc 1.88.0-dev is not
  supported", re-pin the named crates with
  `cargo update <name>@<ver> --precise <lez-build-lock-ver>`.
- risc0-zkvm is pinned `=3.0.5` (the upstream pin) in guest + shared so exactly one
  zkVM copy links into the guest.

Verified build of this tree (2026-07-30):

- Build path used: `cargo risczero build` in docker (reproducible path; the
  risc0/risc0-guest-builder image), after the context/lockfile fixes above.
- `.bin`: `wallet/target/riscv32im-risc0-zkvm-elf/docker/medusa_faucet.bin`
- Size: 437 884 bytes (block budget: well under the ~900 KB that fits a 1 MiB block;
  in-tree programs run 394-533 KB)
- ImageID (`compute_image_id`):
  `523320bdfff97cdbec1f01fdb5de9c37b4555abb7585cd123d77e9d09756e571`
  Cross-checked offline with `medusa-faucet-client info --bin <bin>` (same derivation
  as `deploy`, via `Program::new`).

## Usage

All subcommands read the wallet password on stdin (exactly like the wallet CLI, so the
wrapper's `runWalletCommandInput` convention works), honor `LEE_WALLET_HOME_DIR`, and
print a single JSON object to stdout: `{"ok":true,...}` or `{"error":"..."}`.

```sh
# 0) offline: print the ImageID + size of a guest .bin (no wallet, no network)
medusa-faucet-client info --bin medusa_faucet.bin

# 1) deploy the program (prints programId = ImageID + txHash; polls for inclusion)
medusa-faucet-client deploy --bin medusa_faucet.bin

# 2) create the faucet treasury for each whitelist token (idempotent)
medusa-faucet-client init-treasury --bin medusa_faucet.bin --definition <GOLD_DEF_ID>

# 2b) fund the treasury: a plain token send from the treasury wallet's supply account
#     to the treasury holding printed by init-treasury (it is initialized, so a
#     cross-wallet credit lands fine)
wallet token send --from Public/<SUPPLY_ID> --to Public/<TREASURY_ID> --amount 300000

# 3) claim (one comma-separated recipient holding per definition, same order; all
#    recipients must be owned by the claiming wallet - they are signed; the FIRST one
#    is the claimant the cooldown marker binds to)
medusa-faucet-client claim --bin medusa_faucet.bin \
  --account <RECIPIENT_GOLD>,<RECIPIENT_SILV> \
  --definitions <GOLD_DEF_ID>,<SILV_DEF_ID>
```

`MEDUSA_FAUCET_POLL_BLOCKS` (default 4) bounds how many blocks each subcommand waits
for inclusion before reporting the sequencer's silent-rejection failure mode as an
error.

NOTE: deployment to any real zone (and treasury funding) is a separate, explicit
operator step - nothing in `wallet/build.sh` or this crate touches a sequencer on its
own.
