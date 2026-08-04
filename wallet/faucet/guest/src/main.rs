//! `medusa_faucet`: an on-chain token faucet for the Medusa wallet.
//!
//! Replaces the client-side treasury faucet (a wallet that `token send`s from supply
//! accounts) with a LEZ program that owns its token treasuries as PDAs and enforces a
//! per-claimant cooldown on-chain.
//!
//! Modeled on the in-tree programs:
//! - `lez/programs/pinata_token` - dispensing tokens via a chained call into the token
//!   program with `pda_seeds`,
//! - `lez/programs/associated_token_account` (`create.rs`/`transfer.rs`) - initializing
//!   and spending a token holding at the program's own PDA, deriving the token program
//!   id from `account.program_owner` (never hardcoding it),
//! - `test_programs/guest/src/bin/pinata_cooldown.rs` - the `CLOCK_01`-based cooldown
//!   with the last-claim timestamp persisted in program-owned account data.
//!
//! PDAs (all in this program's public namespace, `AccountId::for_public_pda`):
//! - treasury(def_id): seed `SHA-256("MEDUSA/treasury/" || def_id)` - a token holding
//!   at the faucet's PDA address (the ATA trick),
//! - marker(claimant_id): seed `SHA-256("MEDUSA/claim/" || claimant_id)` - faucet-owned,
//!   data = `last_claim_ms: u64` (borsh).
//!
//! Everything is public-flow; no privacy paths.

use clock_core::{CLOCK_01_PROGRAM_ACCOUNT_ID, ClockAccountData};
use lee_core::{
    account::{Account, AccountId, AccountWithMetadata},
    program::{
        AccountPostState, ChainedCall, Claim, DEFAULT_PROGRAM_ID, ProgramId, ProgramInput,
        ProgramOutput, read_lee_inputs,
    },
};
use associated_token_account_core::verify_ata_and_get_seed;
use medusa_faucet_shared::{self as shared, Instruction};
use token_core::TokenHolding;

fn main() {
    let (
        ProgramInput {
            self_program_id,
            caller_program_id,
            pre_states,
            instruction,
        },
        instruction_words,
    ) = read_lee_inputs::<Instruction>();

    // Mirrors lez/programs/faucet: top-level invocation only, so a hostile caller
    // cannot combine our PDA authority with its own call graph.
    assert!(
        caller_program_id.is_none(),
        "medusa_faucet cannot be invoked through chained calls"
    );

    let (post_states, chained_calls) = match instruction {
        Instruction::InitTreasury => init_treasury(self_program_id, &pre_states),
        Instruction::Claim { ata_program_id } => {
            claim(self_program_id, ata_program_id, &pre_states)
        }
    };

    ProgramOutput::new(
        self_program_id,
        caller_program_id,
        instruction_words,
        pre_states,
        post_states,
    )
    .with_chained_calls(chained_calls)
    .write();
}

/// `InitTreasury`: pre-states `[definition, treasury_pda]`.
///
/// Chain-calls the token program's `InitializeAccount` with
/// `pda_seeds = [treasury_seed(def_id)]`, mirroring `ata::Create` (idempotent, token
/// program id derived from the definition's `program_owner`).
fn init_treasury(
    self_program_id: ProgramId,
    pre_states: &[AccountWithMetadata],
) -> (Vec<AccountPostState>, Vec<ChainedCall>) {
    let [definition, treasury] = pre_states else {
        panic!("InitTreasury requires exactly 2 accounts: [definition, treasury]");
    };

    // The token program that owns this definition - never hardcoded (mirrors ata::Create).
    let token_program_id = definition.account.program_owner;
    assert_ne!(
        token_program_id, DEFAULT_PROGRAM_ID,
        "Definition account is not initialized"
    );

    let seed = shared::treasury_seed(&definition.account_id);
    assert_eq!(
        treasury.account_id,
        AccountId::for_public_pda(&self_program_id, &seed),
        "Treasury account must be the faucet's PDA for this definition"
    );

    let post_states = vec![
        AccountPostState::new(definition.account.clone()),
        AccountPostState::new(treasury.account.clone()),
    ];

    // Idempotent: already initialized -> no-op (mirrors ata::Create).
    if treasury.account != Account::default() {
        return (post_states, vec![]);
    }

    let mut treasury_auth = treasury.clone();
    treasury_auth.is_authorized = true;

    let chained_call = ChainedCall::new(
        token_program_id,
        vec![definition.clone(), treasury_auth],
        &token_core::Instruction::InitializeAccount,
    )
    .with_pda_seeds(vec![seed]);

    (post_states, vec![chained_call])
}

/// `Claim`: pre-states `[owner, ata_1..ata_n, treasury_1..treasury_n, marker, clock]`.
///
/// Dispenses into the CLAIMANT'S OWN associated token accounts. A LEZ account holds exactly
/// one token definition, so a person's account cannot hold three; their ATAs can, and an ATA
/// is derived from (owner, definition), so the balance is already that account's. Nothing new
/// appears in the user's account list, and the cooldown binds to the account they actually
/// have rather than to a holding that can be re-minted for a fresh cooldown.
fn claim(
    self_program_id: ProgramId,
    ata_program_id: ProgramId,
    pre_states: &[AccountWithMetadata],
) -> (Vec<AccountPostState>, Vec<ChainedCall>) {
    assert!(
        pre_states.len() >= 5 && pre_states.len() % 2 == 1,
        "Claim requires 2n + 3 accounts: [owner, atas.., treasuries.., marker, clock]"
    );
    let n = (pre_states.len() - 3) / 2;
    let (owner, rest) = pre_states.split_first().expect("length checked above");
    let (atas, rest) = rest.split_at(n);
    let (treasuries, tail) = rest.split_at(n);
    let [marker, clock] = tail else {
        unreachable!("tail is exactly [marker, clock] by construction");
    };

    // THE ONLY THING PROVING WHO IS CLAIMING. The ATAs below carry no signature of their own
    // (they are PDAs, and the token program authorizes the SENDER only), so without this a
    // caller could name somebody else's ATAs, or their own with a marker they picked.
    assert!(
        owner.is_authorized,
        "The claimant's owner account must be authorized"
    );

    // Clock account: read-only system pre-state (mirrors pinata_cooldown).
    assert_eq!(
        clock.account_id, CLOCK_01_PROGRAM_ACCOUNT_ID,
        "Last account must be the CLOCK_01 system clock"
    );
    let now_ms = ClockAccountData::from_bytes(&clock.account.data.clone().into_inner()).timestamp;

    // The cooldown binds to the OWNER, not to any holding it happens to be paid into.
    let claimant_id = owner.account_id;
    let m_seed = shared::marker_seed(&claimant_id);
    assert_eq!(
        marker.account_id,
        AccountId::for_public_pda(&self_program_id, &m_seed),
        "Marker account must be the faucet's claim PDA for the claimant"
    );

    // Cooldown: a fresh (default) marker means the claimant has never claimed.
    let last_claim_ms = if marker.account == Account::default() {
        0
    } else {
        shared::decode_marker_data(&marker.account.data.clone().into_inner())
            .expect("Marker data must be a borsh u64 timestamp")
    };
    let elapsed = now_ms.saturating_sub(last_claim_ms);
    assert!(
        elapsed >= shared::COOLDOWN_MS,
        "Cooldown not elapsed: {elapsed}ms since last claim, need {}ms",
        shared::COOLDOWN_MS,
    );

    let mut post_states = Vec::with_capacity(pre_states.len());
    let mut chained_calls = Vec::with_capacity(n);
    let mut definition_ids = Vec::with_capacity(n);

    post_states.push(AccountPostState::new(owner.account.clone()));

    for (ata, treasury) in atas.iter().zip(treasuries) {
        // Token program id derived from the treasury holding's owner - never hardcoded
        // (mirrors ata::transfer).
        let token_program_id = treasury.account.program_owner;
        assert_ne!(
            token_program_id, DEFAULT_PROGRAM_ID,
            "Treasury is not initialized; run InitTreasury first"
        );

        let definition_id = TokenHolding::try_from(&treasury.account.data)
            .expect("Treasury must hold a valid token")
            .definition_id();
        let t_seed = shared::treasury_seed(&definition_id);
        assert_eq!(
            treasury.account_id,
            AccountId::for_public_pda(&self_program_id, &t_seed),
            "Treasury account must be the faucet's PDA for its own definition"
        );

        // The destination must be THIS owner's ATA for THIS definition, checked against the
        // derivation rather than trusted because it sits in the right slot. Without this the
        // claim would pay whatever account the caller put here while still spending the
        // owner's cooldown.
        let _ata_seed = verify_ata_and_get_seed(ata, owner, definition_id, ata_program_id);

        definition_ids.push(definition_id);

        let amount = shared::claim_amount(now_ms, &claimant_id, &definition_id);

        // Flip authorization for the chained call; the seed proves our authority
        // over the treasury PDA (mirrors pinata_token / ata::transfer).
        let mut treasury_auth = treasury.clone();
        treasury_auth.is_authorized = true;

        chained_calls.push(
            ChainedCall::new(
                token_program_id,
                vec![treasury_auth, ata.clone()],
                &token_core::Instruction::Transfer {
                    amount_to_transfer: amount,
                },
            )
            .with_pda_seeds(vec![t_seed]),
        );

        // Balances move inside the chained token transfer; at faucet level every account
        // other than the marker is unchanged.
        post_states.push(AccountPostState::new(ata.account.clone()));
    }

    // ONE COOLDOWN PAYS FOR THE WHOLE CLAIM, so the claim must not be able to name the same
    // token twice. Every pair above is individually well-formed - each treasury really is its
    // own definition's PDA, and each ATA really is this owner's for that definition - which is
    // exactly why no per-pair assert can see this: repeat the GOLD pair n times and each
    // iteration legitimately transfers claim_amount out of the one GOLD treasury, n *
    // claim_amount in total, against a single marker. Distinctness is a property of the SET.
    if let Some(dup) = shared::duplicate_definition(&definition_ids) {
        panic!("Token definition {dup:?} appears in more than one pair of a single claim");
    }

    for treasury in treasuries {
        post_states.push(AccountPostState::new(treasury.account.clone()));
    }

    // Create-or-update the marker: claimed via PDA seed on first use (mirrors
    // pinata_cooldown's new_claimed_if_default), plain data update once faucet-owned.
    let mut marker_post = marker.account.clone();
    marker_post.data = shared::encode_marker_data(now_ms)
        .to_vec()
        .try_into()
        .expect("8 bytes fit in account data");
    post_states.push(AccountPostState::new_claimed_if_default(
        marker_post,
        Claim::Pda(m_seed),
    ));

    // Clock account is read-only.
    post_states.push(AccountPostState::new(clock.account.clone()));

    (post_states, chained_calls)
}
