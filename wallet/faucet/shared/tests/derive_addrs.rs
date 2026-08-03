//! The faucet's program id and its treasury addresses are ONE fact, split across two files.
//!
//! A treasury is a PDA of the faucet program, so changing the guest changes its ImageID, which
//! is the program id, which moves every treasury. `kFaucetZones` in
//! `module/src/plugin/WalletPlugin.cpp` hardcodes both, because the module must be able to NAME
//! a treasury (to tell an operator which account to fund) without re-deriving a PDA in C++ and
//! risking drift from this crate. That hardcoding is the right call, but it means the two can
//! be edited apart, and the failure is quiet: the wallet would ask a live chain about accounts
//! that belong to a program nobody deployed, and report an unfunded faucet.
//!
//! So this test re-derives the shipped treasuries from the shipped program id. Update the guest,
//! and it fails until kFaucetZones is updated to match.
//!
//! Run:  cargo test --manifest-path wallet/faucet/shared/Cargo.toml
use lee_core::{account::AccountId, program::ProgramId};
use medusa_faucet_shared::treasury_account_id;

/// The 32-byte ImageID, cast to `[u32; 8]`. This is exactly what the client does with
/// `Program::new(bytes).id()` and prints via `hex::encode(bytemuck::cast::<ProgramId,[u8;32]>)`.
fn program_id_from_hex(hex: &str) -> ProgramId {
    assert_eq!(hex.len(), 64, "an ImageID is 32 bytes of hex");
    let mut bytes = [0u8; 32];
    for (i, b) in bytes.iter_mut().enumerate() {
        *b = u8::from_str_radix(&hex[i * 2..i * 2 + 2], 16).expect("hex digit");
    }
    let mut id = [0u32; 8];
    for (i, w) in id.iter_mut().enumerate() {
        *w = u32::from_le_bytes(bytes[i * 4..i * 4 + 4].try_into().unwrap());
    }
    id
}

/// Must equal kFaucetProgramId in module/src/plugin/WalletPlugin.cpp.
const PROGRAM_ID: &str = "3c56ad0e1d2be3b57582d91187892daa8be2b63d300c2c9d9df318a494dcb885";

/// Must equal kFaucetZones in module/src/plugin/WalletPlugin.cpp: (zone, ticker, definition,
/// treasury).
const SHIPPED: &[(&str, &str, &str, &str)] = &[
    ("paradox-clearnet", "GOLD", "5YEhWdY2edtRFkCruXjtnFH5F62VkCiCxXmNAvHuVkEY",
                                 "Fed1dmPD9aNNyMQrPkSbLznyqVBCJ7Q25bbzQV1rxGnL"),
    ("paradox-clearnet", "SILV", "HUDERmRqyX6swMnuk9FT5vmqNbcdLNbVxDRtLEdzsMXk",
                                 "CWd7PbmCfebZ9ziK1bvmjj8RiDi5XCXW78H4qYdWCTMP"),
    ("paradox-clearnet", "BRNZ", "3zS3bGdToZcqPU9jBZC8c1aK9MQvpekse9EJ52nD1wiM",
                                 "7HvJ5wqSL3NXf1CmGpoDDLj8nk42S39wULuM2TAtzmXx"),
    ("logos-testnet",    "GOLD", "7ZZGE941fzSGCAfxxdkPWQszSspBhZEcjHUkLqWrrnz6",
                                 "D8ScxGNvPLtCeLbPphWSKeei36Lj5tbmFxGGgxr66jsV"),
    ("logos-testnet",    "SILV", "CfuvpaUhbxEzWd6ZtLDiKWVg5DZLiYj14Q8HgtDUwuS6",
                                 "DZQnfiJBz9YZkzkmMDFhHjgk2zZEKLM5oSrfX8SmXXdB"),
    ("logos-testnet",    "BRNZ", "EEMUsdWL1WxrQBi1SmNFUKVcMUjgVcky12NRv2BjBuxp",
                                 "CZxne337Uh7ezNVZcNASN5yN9G7uEkCm1MLpHHdVRPD"),
];

#[test]
fn shipped_table_matches_the_program_id() {
    let pid = program_id_from_hex(PROGRAM_ID);
    let mut wrong = Vec::new();
    for (zone, ticker, def, treasury) in SHIPPED {
        let d: AccountId = def.parse().expect("definition id is base58");
        let derived = treasury_account_id(&pid, &d).to_string();
        if derived != *treasury {
            wrong.push(format!("  {zone} {ticker}: table says {treasury}, derives to {derived}"));
        }
    }
    assert!(
        wrong.is_empty(),
        "kFaucetZones no longer matches kFaucetProgramId {PROGRAM_ID}.\n{}\n\
         The guest changed without the module's table being updated (or the reverse). Rebuild \
         with `cargo risczero build`, take the printed ImageID, and update BOTH.",
        wrong.join("\n")
    );
}

/// The derivation itself, pinned against the previously DEPLOYED program and the treasuries it
/// really had on chain. If this ever fails, the helper above is wrong and the addresses it
/// produces cannot be trusted, whatever the table says.
#[test]
fn the_derivation_reproduces_the_previously_deployed_addresses() {
    let old = program_id_from_hex("523320bdfff97cdbec1f01fdb5de9c37b4555abb7585cd123d77e9d09756e571");
    for (def, want) in [
        ("5YEhWdY2edtRFkCruXjtnFH5F62VkCiCxXmNAvHuVkEY", "A9NwZksDYPzZzpdnbHmJkcEwgHvbGmmYNsYV9rHGoxAF"),
        ("HUDERmRqyX6swMnuk9FT5vmqNbcdLNbVxDRtLEdzsMXk", "5iG2BTUhWCmgviBw54ZMtr3qjSMyLfPz7pMNAwvk6kiQ"),
        ("3zS3bGdToZcqPU9jBZC8c1aK9MQvpekse9EJ52nD1wiM", "89MWMvGchyEVq4FZFQPsXS747LQjLe4L9ev4hXMBY8PK"),
    ] {
        let d: AccountId = def.parse().expect("definition id is base58");
        assert_eq!(treasury_account_id(&old, &d).to_string(), want, "for definition {def}");
    }
}
