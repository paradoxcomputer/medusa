//! Shared definitions for the `medusa_faucet` LEZ program.
//!
//! This crate is the single source of truth for everything the risc0 guest
//! (`wallet/faucet/guest`) and the host client (`wallet/faucet/client`) must agree on:
//!
//! - the [`Instruction`] enum (risc0-serde encoded on the wire),
//! - the PDA seed derivations (treasury per token definition, cooldown marker per
//!   claimant),
//! - the cooldown / amount constants and the deterministic per-claim amount formula,
//! - the marker account data encoding (`last_claim_ms: u64`, borsh).
//!
//! The enum + constants compile without std; the derivation helpers (which need the
//! in-tree `lee_core` types and the risc0 SHA-256 impl) sit behind the default
//! `helpers` feature.

#![cfg_attr(not(feature = "helpers"), no_std)]

use serde::{Deserialize, Serialize};

/// Cooldown between claims for a given claimant: 6 hours, in milliseconds.
pub const COOLDOWN_MS: u64 = 21_600_000;

/// Smallest amount a claim can dispense per token (matches the client-side faucet).
pub const MIN_AMOUNT: u128 = 10;

/// Largest amount a claim can dispense per token (matches the client-side faucet).
pub const MAX_AMOUNT: u128 = 500;

/// Prefix of the treasury PDA seed preimage: `SHA-256("MEDUSA/treasury/" || def_id)`.
pub const TREASURY_SEED_PREFIX: &[u8] = b"MEDUSA/treasury/";

/// Prefix of the claim-marker PDA seed preimage: `SHA-256("MEDUSA/claim/" || claimant_id)`.
pub const CLAIM_SEED_PREFIX: &[u8] = b"MEDUSA/claim/";

/// The `medusa_faucet` program instruction.
///
/// Encoded with risc0-serde (`risc0_zkvm::serde`), i.e. the variant index as a `u32`.
/// Both sides use THIS enum, so the encoding cannot drift.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
pub enum Instruction {
    /// Create the faucet's treasury token holding for one token definition.
    ///
    /// Pre-states (2): `[definition, treasury_pda]`. Idempotent (mirrors `ata::Create`):
    /// if the treasury is already initialized the call is a no-op. Otherwise the faucet
    /// chain-calls the token program's `InitializeAccount` with
    /// `pda_seeds = [treasury_seed(def_id)]`, and the token program claims the PDA.
    InitTreasury,

    /// Dispense a pseudorandom amount of each treasury token to the claimant.
    ///
    /// Pre-states (`2n + 2`), in order:
    /// `[recipient_1 .. recipient_n, treasury_1 .. treasury_n, marker, CLOCK_01]`
    ///
    /// - each `recipient_i` is a claimant-signed token holding (uninitialized is fine;
    ///   the token program initializes it on transfer),
    /// - each `treasury_i` is the faucet treasury PDA whose holding data names the
    ///   definition it pairs with,
    /// - `marker` is the faucet-owned cooldown record for the claimant
    ///   (`recipient_1.account_id`), created on first claim via `Claim::Pda`,
    /// - `CLOCK_01` is the system clock account, read-only.
    Claim,
}

/// Marker account data encoding: borsh of `last_claim_ms: u64` (8 little-endian bytes).
#[must_use]
pub fn encode_marker_data(last_claim_ms: u64) -> [u8; 8] {
    last_claim_ms.to_le_bytes()
}

/// Decodes marker account data. `None` unless the data is exactly a borsh `u64`.
#[must_use]
pub fn decode_marker_data(data: &[u8]) -> Option<u64> {
    let bytes: [u8; 8] = data.try_into().ok()?;
    Some(u64::from_le_bytes(bytes))
}

#[cfg(feature = "helpers")]
mod helpers {
    use lee_core::{
        account::AccountId,
        program::{PdaSeed, ProgramId},
    };
    use risc0_zkvm::sha::{Impl, Sha256 as _};

    use crate::{CLAIM_SEED_PREFIX, MAX_AMOUNT, MIN_AMOUNT, TREASURY_SEED_PREFIX};

    fn sha256(bytes: &[u8]) -> [u8; 32] {
        Impl::hash_bytes(bytes)
            .as_bytes()
            .try_into()
            .expect("SHA-256 output must be exactly 32 bytes long")
    }

    fn prefixed_seed(prefix: &[u8], account_id: &AccountId) -> PdaSeed {
        let mut bytes = Vec::with_capacity(prefix.len() + 32);
        bytes.extend_from_slice(prefix);
        bytes.extend_from_slice(&account_id.to_bytes());
        PdaSeed::new(sha256(&bytes))
    }

    /// Seed of the faucet's treasury PDA for `definition_id`:
    /// `SHA-256("MEDUSA/treasury/" || def_id)`.
    #[must_use]
    pub fn treasury_seed(definition_id: &AccountId) -> PdaSeed {
        prefixed_seed(TREASURY_SEED_PREFIX, definition_id)
    }

    /// Seed of the faucet's cooldown-marker PDA for `claimant_id`:
    /// `SHA-256("MEDUSA/claim/" || claimant_id)`.
    #[must_use]
    pub fn marker_seed(claimant_id: &AccountId) -> PdaSeed {
        prefixed_seed(CLAIM_SEED_PREFIX, claimant_id)
    }

    /// The treasury holding's account id in the faucet's own PDA namespace.
    #[must_use]
    pub fn treasury_account_id(
        faucet_program_id: &ProgramId,
        definition_id: &AccountId,
    ) -> AccountId {
        AccountId::for_public_pda(faucet_program_id, &treasury_seed(definition_id))
    }

    /// The cooldown marker's account id in the faucet's own PDA namespace.
    #[must_use]
    pub fn marker_account_id(faucet_program_id: &ProgramId, claimant_id: &AccountId) -> AccountId {
        AccountId::for_public_pda(faucet_program_id, &marker_seed(claimant_id))
    }

    /// Deterministic per-claim amount:
    /// `MIN + (first 8 LE bytes of SHA-256(clock_ms_le || claimant_id || def_id) mod (MAX - MIN + 1))`.
    #[must_use]
    pub fn claim_amount(clock_ms: u64, claimant_id: &AccountId, definition_id: &AccountId) -> u128 {
        let mut bytes = [0_u8; 72];
        bytes[..8].copy_from_slice(&clock_ms.to_le_bytes());
        bytes[8..40].copy_from_slice(&claimant_id.to_bytes());
        bytes[40..].copy_from_slice(&definition_id.to_bytes());
        let digest = sha256(&bytes);
        let x = u64::from_le_bytes(digest[..8].try_into().expect("8 bytes"));
        let span = MAX_AMOUNT - MIN_AMOUNT + 1;
        MIN_AMOUNT + u128::from(x) % span
    }
}

#[cfg(feature = "helpers")]
pub use helpers::*;
