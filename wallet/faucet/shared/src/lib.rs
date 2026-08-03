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

    /// The first token definition that appears more than once in `definition_ids`, or `None`
    /// when they are all distinct.
    ///
    /// A claim pays `claim_amount` once per (recipient, treasury) PAIR, and the whole claim
    /// binds a SINGLE cooldown marker (the one derived from the first recipient). So a claim
    /// that names the same definition in several pairs is paid several times over while
    /// spending one cooldown: passing the GOLD treasury n times is n * claim_amount out of
    /// that one treasury, bounded only by how many accounts fit in a transaction. The pairs
    /// are individually well-formed (each treasury really is that definition's PDA), so no
    /// per-pair check can catch it. Distinctness is a property OF THE SET and has to be
    /// checked as one.
    #[must_use]
    pub fn duplicate_definition(definition_ids: &[AccountId]) -> Option<AccountId> {
        for (i, id) in definition_ids.iter().enumerate() {
            if definition_ids[i + 1..].contains(id) {
                return Some(*id);
            }
        }
        None
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

#[cfg(all(test, feature = "helpers"))]
mod tests {
    use lee_core::{account::AccountId, program::ProgramId};

    use super::*;

    fn id(byte: u8) -> AccountId {
        AccountId::new([byte; 32])
    }

    fn program() -> ProgramId {
        [9_u32; 8]
    }

    #[test]
    fn marker_data_roundtrips() {
        for ms in [0_u64, 1, 21_600_000, u64::MAX] {
            assert_eq!(decode_marker_data(&encode_marker_data(ms)), Some(ms));
        }
    }

    // A marker whose data is any other length is not a marker. Accepting a short or long
    // record would let a claimant present something the faucet never wrote as a cooldown.
    #[test]
    fn marker_data_of_the_wrong_length_is_rejected() {
        for len in [0_usize, 1, 7, 9, 32] {
            assert_eq!(decode_marker_data(&vec![0_u8; len]), None, "len {len}");
        }
    }

    // The two PDA namespaces MUST NOT collide: with a shared preimage, one account id could
    // be read as both a treasury and a cooldown marker for the same id.
    #[test]
    fn treasury_and_marker_seeds_are_domain_separated() {
        let a = id(1);
        assert_ne!(treasury_seed(&a), marker_seed(&a));
        assert_ne!(
            treasury_account_id(&program(), &a),
            marker_account_id(&program(), &a)
        );
    }

    #[test]
    fn seeds_are_deterministic_and_distinct_per_account() {
        assert_eq!(treasury_seed(&id(1)), treasury_seed(&id(1)));
        assert_ne!(treasury_seed(&id(1)), treasury_seed(&id(2)));
        assert_eq!(marker_seed(&id(1)), marker_seed(&id(1)));
        assert_ne!(marker_seed(&id(1)), marker_seed(&id(2)));
    }

    // The dispensed amount is what a claimant actually receives, so it must stay inside the
    // advertised band for every input - a value outside it would either dispense nothing or
    // drain the treasury.
    #[test]
    fn claim_amount_always_lands_inside_the_advertised_band() {
        let def = id(7);
        for clock in [0_u64, 1, 1_700_000_000_000, u64::MAX] {
            for who in 0_u8..32 {
                let amt = claim_amount(clock, &id(who), &def);
                assert!(
                    (MIN_AMOUNT..=MAX_AMOUNT).contains(&amt),
                    "clock {clock} who {who} gave {amt}"
                );
            }
        }
    }

    // Deterministic in all three inputs: the guest recomputes it from the same values the
    // host used, so any drift makes every claim fail verification.
    #[test]
    fn claim_amount_is_a_pure_function_of_its_three_inputs() {
        let (clock, who, def) = (1_700_000_000_000_u64, id(3), id(7));
        assert_eq!(claim_amount(clock, &who, &def), claim_amount(clock, &who, &def));
        assert_ne!(claim_amount(clock, &who, &def), claim_amount(clock + 1, &who, &def));
        assert_ne!(claim_amount(clock, &who, &def), claim_amount(clock, &id(4), &def));
        assert_ne!(claim_amount(clock, &who, &def), claim_amount(clock, &who, &id(8)));
    }

    // The wire encoding both sides depend on: the variant index is what the guest matches
    // on, so a reordering of this enum silently turns InitTreasury into Claim.
    #[test]
    fn instruction_encoding_is_stable_across_the_host_guest_boundary() {
        for (ins, want) in [(Instruction::InitTreasury, 0_u32), (Instruction::Claim, 1)] {
            let words = risc0_zkvm::serde::to_vec(&ins).expect("encodes");
            assert_eq!(words.first().copied(), Some(want), "{ins:?}");
            let back: Instruction = risc0_zkvm::serde::from_slice(&words).expect("decodes");
            assert_eq!(back, ins);
        }
    }

    // The set-level rule the guest cannot express per pair. Every pair in a duplicated claim
    // is individually valid, so this is the only thing standing between a treasury and being
    // drained n * claim_amount at the cost of one cooldown.
    #[test]
    fn distinct_definitions_are_accepted_and_repeats_are_caught() {
        assert_eq!(duplicate_definition(&[]), None);
        assert_eq!(duplicate_definition(&[id(1)]), None);
        assert_eq!(duplicate_definition(&[id(1), id(2), id(3)]), None);
        // adjacent
        assert_eq!(duplicate_definition(&[id(1), id(1)]), Some(id(1)));
        // NOT adjacent: the drain does not have to put the repeats side by side
        assert_eq!(duplicate_definition(&[id(1), id(2), id(1)]), Some(id(1)));
        assert_eq!(duplicate_definition(&[id(4), id(2), id(3), id(2)]), Some(id(2)));
        // the whole claim being one repeated definition is the maximal drain
        assert_eq!(duplicate_definition(&[id(7); 8]), Some(id(7)));
    }

    #[test]
    fn the_cooldown_is_the_documented_six_hours() {
        assert_eq!(COOLDOWN_MS, 6 * 60 * 60 * 1_000);
    }
}
