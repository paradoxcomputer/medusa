//! Operator/claimant client for the `medusa_faucet` LEZ program.
//!
//! Modeled on `examples/program_deployment/src/bin/run_hello_world_with_authorization.rs`
//! and the wallet's program facades (`lez/wallet/src/program_facades/pinata.rs`),
//! reusing the wallet crate's own storage/unlock path exactly like the wallet CLI main
//! (password on stdin), so the Medusa wrapper's `runWalletCommandInput` convention
//! keeps working.
//!
//! Every subcommand prints a single JSON object to stdout:
//! `{"ok":true,...}` on success, `{"error":"..."}` on failure (exit code 1).
//!
//! Deployment to any real zone is a separate, explicit operator step - this binary
//! only talks to the sequencer configured in the wallet home it is pointed at
//! (`LEE_WALLET_HOME_DIR`).

use std::{borrow::Cow, path::PathBuf, time::Duration};

use anyhow::{Context as _, Result, bail};
use clap::{Parser, Subcommand};
use common::{HashType, transaction::LeeTransaction};
use lee::{AccountId, ProgramDeploymentTransaction, program::Program};
use lee_core::program::{DEFAULT_PROGRAM_ID, ProgramId};
use medusa_faucet_shared as shared;
use sequencer_service_rpc::{RpcClient as _, SequencerClient};
use wallet::{
    AccountIdentity, WalletCore,
    cli::read_password_from_stdin,
    helperfunctions::{fetch_config_path, fetch_persistent_storage_path},
};

/// Default number of blocks to wait for inclusion before declaring the tx silently
/// rejected. Overridable via MEDUSA_FAUCET_POLL_BLOCKS.
const DEFAULT_POLL_BLOCKS: u64 = 4;

#[derive(Parser)]
#[command(
    name = "medusa-faucet-client",
    about = "Deploy / initialize / claim the medusa_faucet LEZ program (Medusa wallet)"
)]
struct Args {
    #[command(subcommand)]
    command: Command,
}

#[derive(Subcommand)]
enum Command {
    /// Print the program id (risc0 ImageID) of a guest .bin, offline (no wallet, no
    /// network). Must match the ImageID printed by `cargo risczero build`.
    Info {
        /// Path to the cargo-risczero built guest .bin.
        #[arg(long)]
        bin: PathBuf,
    },
    /// Deploy the medusa_faucet guest binary as a LEZ program.
    Deploy {
        /// Path to the cargo-risczero built guest .bin.
        #[arg(long)]
        bin: PathBuf,
    },
    /// Initialize the faucet's treasury holding for a token definition (idempotent).
    InitTreasury {
        /// Path to the guest .bin (used to recompute the program id).
        #[arg(long)]
        bin: PathBuf,
        /// Token definition account id (bare base58 or Public/<id>).
        #[arg(long)]
        definition: String,
    },
    /// Claim a pseudorandom amount of each listed token from the faucet.
    Claim {
        /// Path to the guest .bin (used to recompute the program id).
        #[arg(long)]
        bin: PathBuf,
        /// Recipient token-holding account id(s), comma-separated, one per definition,
        /// in the same order as --definitions. All must be owned by this wallet (they
        /// are signed as the claimant); the FIRST one is the claimant the cooldown
        /// marker is bound to. A single account can hold only one token definition.
        #[arg(long)]
        account: String,
        /// Token definition account id(s), comma-separated.
        #[arg(long)]
        definitions: String,
    },
}

#[tokio::main]
async fn main() {
    let args = Args::parse();
    match run(args).await {
        Ok(value) => println!("{value}"),
        Err(err) => {
            println!("{}", serde_json::json!({ "error": format!("{err:#}") }));
            std::process::exit(1);
        }
    }
}

async fn run(args: Args) -> Result<serde_json::Value> {
    // `info` is fully offline: no wallet unlock, no sequencer.
    if let Command::Info { bin } = &args.command {
        let (bytes, program_id) = load_program(bin)?;
        return Ok(serde_json::json!({
            "ok": true,
            "programId": program_id_hex(&program_id),
            "binSizeBytes": bytes.len(),
        }));
    }

    // Password arrives on stdin exactly like the wallet CLI; unlock the same storage.
    let wallet = open_wallet()?;
    let client = wallet.sequencer_client.clone();

    match args.command {
        Command::Info { .. } => unreachable!("handled before wallet unlock"),
        Command::Deploy { bin } => deploy(&client, &bin).await,
        Command::InitTreasury { bin, definition } => {
            init_treasury(&wallet, &client, &bin, &definition).await
        }
        Command::Claim {
            bin,
            account,
            definitions,
        } => claim(&wallet, &client, &bin, &account, &definitions).await,
    }
}

fn open_wallet() -> Result<WalletCore> {
    let config_path = fetch_config_path().context("Could not fetch config path")?;
    let storage_path =
        fetch_persistent_storage_path().context("Could not fetch persistent storage path")?;
    if !storage_path.exists() {
        bail!(
            "wallet storage not found at {} - initialize the wallet with the wallet CLI first",
            storage_path.display()
        );
    }
    let password = read_password_from_stdin().context("Could not read password from stdin")?;
    WalletCore::open_encrypted(config_path, storage_path, None, &password)
        .context("Failed to unlock wallet storage")
}

async fn deploy(client: &SequencerClient, bin: &PathBuf) -> Result<serde_json::Value> {
    let (bytes, program_id) = load_program(bin)?;

    let start_block = current_block(client).await?;
    let message = lee::program_deployment_transaction::Message::new(bytes);
    let transaction = ProgramDeploymentTransaction::new(message);
    let tx_hash = client
        .send_transaction(LeeTransaction::ProgramDeployment(transaction))
        .await
        .context("Transaction submission error")?;

    let included_at = poll_inclusion(client, tx_hash, start_block).await?;
    Ok(serde_json::json!({
        "ok": true,
        "programId": program_id_hex(&program_id),
        "txHash": tx_hash.to_string(),
        "includedAtBlock": included_at,
    }))
}

async fn init_treasury(
    wallet: &WalletCore,
    client: &SequencerClient,
    bin: &PathBuf,
    definition: &str,
) -> Result<serde_json::Value> {
    let (_bytes, program_id) = load_program(bin)?;
    let definition_id = parse_account_id(definition)?;
    let treasury_id = shared::treasury_account_id(&program_id, &definition_id);

    // Fail fast with a clear message; the guest re-asserts this on-chain.
    let definition_account = wallet
        .get_account_public(definition_id)
        .await
        .context("Failed to fetch definition account")?;
    if definition_account.program_owner == DEFAULT_PROGRAM_ID {
        bail!("definition {definition_id} is not an initialized token definition");
    }

    let instruction_data = Program::serialize_instruction(shared::Instruction::InitTreasury)
        .context("Instruction should serialize")?;

    let start_block = current_block(client).await?;
    let tx_hash = wallet
        .send_pub_tx(
            vec![
                AccountIdentity::PublicNoSign(definition_id),
                AccountIdentity::PublicNoSign(treasury_id),
            ],
            instruction_data,
            program_id,
        )
        .await
        .context("Failed to send InitTreasury transaction")?;

    let included_at = poll_inclusion(client, tx_hash, start_block).await?;
    Ok(serde_json::json!({
        "ok": true,
        "programId": program_id_hex(&program_id),
        "definition": definition_id.to_string(),
        "treasury": treasury_id.to_string(),
        "txHash": tx_hash.to_string(),
        "includedAtBlock": included_at,
    }))
}

async fn claim(
    wallet: &WalletCore,
    client: &SequencerClient,
    bin: &PathBuf,
    account: &str,
    definitions: &str,
) -> Result<serde_json::Value> {
    let (_bytes, program_id) = load_program(bin)?;

    let recipients = parse_account_id_list(account).context("invalid --account")?;
    let definition_ids = parse_account_id_list(definitions).context("invalid --definitions")?;
    if recipients.len() != definition_ids.len() {
        bail!(
            "got {} recipient account(s) for {} definition(s) - a holding account can hold \
             exactly one token definition, so pass one comma-separated recipient per definition \
             (same order)",
            recipients.len(),
            definition_ids.len()
        );
    }
    for window in [&recipients, &definition_ids] {
        let mut seen = std::collections::HashSet::new();
        if !window.iter().all(|id| seen.insert(*id)) {
            bail!("duplicate account ids are not allowed within --account/--definitions");
        }
    }

    // Recipients are the claimant's signatures: the wallet must own their keys.
    for recipient in &recipients {
        if wallet.get_account_public_signing_key(*recipient).is_none() {
            bail!("recipient account {recipient} is not owned by this wallet (no signing key)");
        }
    }

    let claimant_id = recipients[0];
    let marker_id = shared::marker_account_id(&program_id, &claimant_id);
    let treasury_ids: Vec<AccountId> = definition_ids
        .iter()
        .map(|def| shared::treasury_account_id(&program_id, def))
        .collect();

    // Fail fast on the obvious silent-rejection causes (the guest re-asserts on-chain):
    // uninitialized treasuries and an unelapsed cooldown.
    for (treasury_id, definition_id) in treasury_ids.iter().zip(&definition_ids) {
        let treasury = wallet
            .get_account_public(*treasury_id)
            .await
            .context("Failed to fetch treasury account")?;
        if treasury.program_owner == DEFAULT_PROGRAM_ID {
            bail!(
                "treasury for definition {definition_id} is not initialized - run init-treasury \
                 first"
            );
        }
    }
    check_cooldown(wallet, marker_id).await?;

    let mut accounts: Vec<AccountIdentity> = recipients
        .iter()
        .map(|id| AccountIdentity::Public(*id))
        .collect();
    accounts.extend(
        treasury_ids
            .iter()
            .map(|id| AccountIdentity::PublicNoSign(*id)),
    );
    accounts.push(AccountIdentity::PublicNoSign(marker_id));
    accounts.push(AccountIdentity::PublicNoSign(
        clock_core::CLOCK_01_PROGRAM_ACCOUNT_ID,
    ));

    let instruction_data = Program::serialize_instruction(shared::Instruction::Claim)
        .context("Instruction should serialize")?;

    let start_block = current_block(client).await?;
    let tx_hash = wallet
        .send_pub_tx(accounts, instruction_data, program_id)
        .await
        .context("Failed to send Claim transaction")?;

    let included_at = poll_inclusion(client, tx_hash, start_block).await?;
    Ok(serde_json::json!({
        "ok": true,
        "programId": program_id_hex(&program_id),
        "claimant": claimant_id.to_string(),
        "marker": marker_id.to_string(),
        "definitions": definition_ids.iter().map(ToString::to_string).collect::<Vec<_>>(),
        "recipients": recipients.iter().map(ToString::to_string).collect::<Vec<_>>(),
        "treasuries": treasury_ids.iter().map(ToString::to_string).collect::<Vec<_>>(),
        "txHash": tx_hash.to_string(),
        "includedAtBlock": included_at,
    }))
}

/// Client-side cooldown pre-check so an unelapsed cooldown fails with a clear message
/// instead of burning a signed tx on a guaranteed silent rejection.
async fn check_cooldown(wallet: &WalletCore, marker_id: AccountId) -> Result<()> {
    let marker = wallet
        .get_account_public(marker_id)
        .await
        .context("Failed to fetch marker account")?;
    if marker.program_owner == DEFAULT_PROGRAM_ID {
        return Ok(()); // never claimed
    }
    let Some(last_claim_ms) = shared::decode_marker_data(marker.data.as_ref()) else {
        return Ok(()); // unreadable marker: let the guest decide
    };
    let clock = wallet
        .get_account_public(clock_core::CLOCK_01_PROGRAM_ACCOUNT_ID)
        .await
        .context("Failed to fetch clock account")?;
    let now_ms = clock_core::ClockAccountData::from_bytes(clock.data.as_ref()).timestamp;
    let elapsed = now_ms.saturating_sub(last_claim_ms);
    if elapsed < shared::COOLDOWN_MS {
        let remaining_ms = shared::COOLDOWN_MS - elapsed;
        bail!(
            "cooldown not elapsed: {} minutes remaining before the next claim",
            remaining_ms.div_ceil(60_000)
        );
    }
    Ok(())
}

fn load_program(bin: &PathBuf) -> Result<(Vec<u8>, ProgramId)> {
    let bytes = std::fs::read(bin)
        .with_context(|| format!("Failed to read program binary at {}", bin.display()))?;
    let program = Program::new(Cow::Owned(bytes.clone()))
        .context("Invalid risc0 program binary (expected a cargo-risczero built .bin)")?;
    Ok((bytes, program.id()))
}

fn parse_account_id(input: &str) -> Result<AccountId> {
    let trimmed = input.trim();
    if trimmed.starts_with("Private/") {
        bail!("faucet operations are public-flow only, got private account '{trimmed}'");
    }
    let bare = trimmed.strip_prefix("Public/").unwrap_or(trimmed);
    bare.parse::<AccountId>()
        .map_err(|e| anyhow::anyhow!("invalid account id '{trimmed}': {e}"))
}

fn parse_account_id_list(input: &str) -> Result<Vec<AccountId>> {
    let ids: Vec<AccountId> = input
        .split(',')
        .filter(|part| !part.trim().is_empty())
        .map(parse_account_id)
        .collect::<Result<_>>()?;
    if ids.is_empty() {
        bail!("expected at least one account id");
    }
    Ok(ids)
}

fn program_id_hex(program_id: &ProgramId) -> String {
    hex::encode(bytemuck::cast::<ProgramId, [u8; 32]>(*program_id))
}

async fn current_block(client: &SequencerClient) -> Result<u64> {
    client
        .get_last_block_id()
        .await
        .context("sequencer unreachable (getLastBlockId failed)")
}

/// Polls `getTransaction` until the tx is included, or fails once the chain advances
/// `MEDUSA_FAUCET_POLL_BLOCKS` (default 4) blocks past submission: an unincluded tx is
/// the sequencer's "silent rejection" failure mode (program execution failed), and it
/// must be surfaced, not spun on.
async fn poll_inclusion(
    client: &SequencerClient,
    tx_hash: HashType,
    start_block: u64,
) -> Result<u64> {
    let max_blocks: u64 = std::env::var("MEDUSA_FAUCET_POLL_BLOCKS")
        .ok()
        .and_then(|v| v.parse().ok())
        .unwrap_or(DEFAULT_POLL_BLOCKS);
    // Hard wall-clock stop so an unreachable endpoint cannot spin forever
    // (block clocks can be slow - allow 120s per block, plus one for margin).
    let deadline = std::time::Instant::now()
        + Duration::from_secs(max_blocks.saturating_add(1).saturating_mul(120));

    loop {
        if let Ok(Some(_tx)) = client.get_transaction(tx_hash).await {
            return Ok(client.get_last_block_id().await.unwrap_or(start_block));
        }
        if let Ok(now_block) = client.get_last_block_id().await
            && now_block >= start_block.saturating_add(max_blocks)
        {
            bail!(
                "transaction {tx_hash} was not included within {max_blocks} blocks - the \
                 sequencer rejected it silently (program execution failed on-chain)"
            );
        }
        if std::time::Instant::now() >= deadline {
            bail!("timed out polling for transaction {tx_hash}");
        }
        tokio::time::sleep(Duration::from_secs(3)).await;
    }
}
