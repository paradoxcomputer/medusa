# Medusa's patches against LEZ v0.2.2

One patch, not the three that `patches-v020/` carried. Upstream v0.2.2 rewrote enough of
`lez/wallet` that the three no longer sit on disjoint hunks: the keycard gating and the
encrypted-storage work both touch `lez/wallet/Cargo.toml` and `lez/wallet/src/cli/mod.rs`, and
splitting them again would mean maintaining a hunk-level split that breaks on the next rebase for
no benefit. What each part does is below.

Verified: `cargo build --release -p wallet` succeeds on v0.2.2 with this applied, and the built
binary offers `account list --json`, `account export-mnemonic`, `account export-key` and
top-level `restore-keys`.

## 1. Encrypted storage

Upstream has no encrypted wallet storage at all: `Storage::from_path` reads plaintext JSON. This
adds `lez/wallet/src/crypto.rs` (argon2id + AES-GCM), `Storage::from_path_maybe_encrypted` /
`save_to_path_maybe_encrypted`, and four `WalletCore` constructors:

- `new_init_storage_encrypted` - create sealed with a password, retaining the phrase
- `open_encrypted` - load, decrypting if the file is an envelope (plaintext still loads)
- `new_for_restore` - rebuild from a mnemonic WITHOUT reading the old storage, so recovery
  works when the existing wallet is unopenable, which is the point of recovery
- `new` - gained `password` + `mnemonic`, which every caller threads through

`main.rs` reads the password up front and intercepts `restore-keys` before any storage load.

WHAT CHANGED IN THE REBASE: `WalletCore::new()` is `async` in v0.2.2 and takes a
`statistics_path`, and the struct gained `statistics` / `multi_sequencer_client`. The
constructors were rethreaded onto that shape rather than textually re-applied.

## 2. `account list --json`

A machine-readable array of `{id, type, balance, initialized, label}`. The Medusa module parses
this; the human format is not a contract.

WHAT CHANGED IN THE REBASE: upstream factored the list body out of the match arm into
`AccountSubcommand::handle_list()`, so the JSON branch lives there now and `handle_list` gained a
`json: bool`.

## 3. `account export-mnemonic` / `account export-key`

Reveal the recovery phrase and a public account's signing key. Both refuse on a plaintext wallet,
which has no retained phrase.

## 4. Keycard is opt-in (`--features keycard`, OFF by default)

Upstream links the Status keycard through `keycard_wallet`, which pulls `pcsc-sys` and fails to
build without the smartcard system libraries. Medusa does not ship keycard support, so
`keycard_wallet` is an optional dependency and every use of it is `#[cfg(feature = "keycard")]`.
Without the feature the `keycard` subcommand still EXISTS and refuses with a sentence, so an old
invocation gets an explanation rather than a missing-subcommand error.

WHAT CHANGED IN THE REBASE: much smaller than in v0.2.0. Upstream deleted `lez/wallet/src/signing.rs`
and removed `pyo3` from the workspace entirely, so the hunks that gated the pyo3 `KeycardError`
variant and the `signing` module are simply gone. `pyo3` must NOT be declared by the wallet crate
now: it is not in `workspace.dependencies`, and declaring it fails the workspace manifest load.

## 5. Signing key on stdin

`account import public` takes the key on stdin line 2 rather than `--private-key <hex>`, because
`/proc/<pid>/cmdline` is world-readable. Applied cleanly from `patches-v020`.
