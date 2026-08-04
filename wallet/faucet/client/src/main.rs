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
use associated_token_account_core::{compute_ata_seed, get_associated_token_account_id};
use wallet::program_facades::ata::Ata;
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
    /// Initialize an owned account as a token holding, so it can be shielded from.
    ///
    /// A shield proves spend authority from a signing key, so its source must be a keytree
    /// account; an ATA is a PDA and can never be one. But nothing else can BOOTSTRAP such a
    /// holding from ATA funds: crediting an uninitialized account only works when that account
    /// is authorized, and `ata send` passes its recipient unauthorized. This does the one thing
    /// that closes the gap, and it needs no faucet program at all: the token program's own
    /// InitializeAccount, with the holding signed because the wallet owns it.
    InitHolding {
        /// Token definition the holding will be for.
        #[arg(long)]
        definition: String,
        /// An account this wallet owns, not yet initialized as a holding.
        #[arg(long)]
        holding: String,
    },

    /// Claim a pseudorandom amount of each listed token from the faucet.
    Claim {
        /// Path to the guest .bin (used to recompute the program id).
        #[arg(long)]
        bin: PathBuf,
        /// The ONE account claiming, which must be owned by this wallet: it is signed, it
        /// is what proves who is claiming, and the cooldown marker binds to it. Tokens are
        /// paid into that account's associated token accounts, derived per definition and
        /// created here if missing, so no holding account is passed or minted.
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
        Command::InitHolding { definition, holding } => {
            init_holding(&wallet, &client, &definition, &holding).await
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

async fn init_holding(
    wallet: &WalletCore,
    client: &SequencerClient,
    definition: &str,
    holding: &str,
) -> Result<serde_json::Value> {
    let definition_id = parse_account_id(definition).context("invalid --definition")?;
    let holding_id = parse_account_id(holding).context("invalid --holding")?;

    // The holding is SIGNED, which is the whole point: the token program initializes an
    // uninitialized recipient only when it is authorized, and that signature is what an
    // `ata send` cannot provide.
    if wallet.get_account_public_signing_key(holding_id).is_none() {
        bail!("holding {holding_id} is not owned by this wallet (no signing key)");
    }

    // Token program id from the DEFINITION's owner, never hardcoded (mirrors the guest).
    let definition_account = wallet
        .get_account_public(definition_id)
        .await
        .context("Failed to fetch the token definition")?;
    let token_program_id = definition_account.program_owner;
    if token_program_id == DEFAULT_PROGRAM_ID {
        bail!("definition {definition_id} is not initialized");
    }

    // Idempotent, like init-treasury: an account that is already a holding is left alone.
    if let Ok(existing) = wallet.get_account_public(holding_id).await {
        if existing.program_owner != DEFAULT_PROGRAM_ID {
            return Ok(serde_json::json!({
                "ok": true,
                "alreadyInitialized": true,
                "definition": definition_id.to_string(),
                "holding": holding_id.to_string(),
            }));
        }
    }

    let instruction_data =
        Program::serialize_instruction(token_core::Instruction::InitializeAccount)
            .context("Instruction should serialize")?;

    let start_block = current_block(client).await?;
    let tx_hash = wallet
        .send_pub_tx(
            vec![
                AccountIdentity::PublicNoSign(definition_id),
                AccountIdentity::Public(holding_id),
            ],
            instruction_data,
            token_program_id,
        )
        .await
        .context("Failed to send InitializeAccount transaction")?;

    let included_at = poll_inclusion(client, tx_hash, start_block).await?;
    Ok(serde_json::json!({
        "ok": true,
        "definition": definition_id.to_string(),
        "holding": holding_id.to_string(),
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

    // ONE owner, not a list. Tokens land in the claimant's own associated token accounts, and an
    // ATA is derived from (owner, definition), so the caller no longer supplies a holding per
    // token: it supplies the account the tokens belong to and the definitions it wants.
    let owners = parse_account_id_list(account).context("invalid --account")?;
    if owners.len() != 1 {
        bail!(
            "--account takes exactly ONE account id, the claimant whose ATAs receive the tokens              (got {})",
            owners.len()
        );
    }
    let owner_id = owners[0];
    let definition_ids = parse_account_id_list(definitions).context("invalid --definitions")?;
    if definition_ids.is_empty() {
        bail!("--definitions must name at least one token definition");
    }
    {
        // A definition repeated in one claim would be paid twice against a single cooldown.
        // The guest refuses it too; catching it here saves a round trip and names the argument.
        let mut seen = std::collections::HashSet::new();
        if !definition_ids.iter().all(|id| seen.insert(*id)) {
            bail!("duplicate token definitions are not allowed within --definitions");
        }
    }

    // The owner is the only signature in the claim: it is what proves who is claiming, and the
    // cooldown binds to it.
    if wallet.get_account_public_signing_key(owner_id).is_none() {
        bail!("account {owner_id} is not owned by this wallet (no signing key)");
    }

    let ata_program_id = programs::ata().id();   // the zone's built-in ATA program
    let ata_ids: Vec<AccountId> = definition_ids
        .iter()
        .map(|def| {
            get_associated_token_account_id(&ata_program_id, &compute_ata_seed(owner_id, *def))
        })
        .collect();

    let marker_id = shared::marker_account_id(&program_id, &owner_id);
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

    // The ATA must EXIST before the claim: the token program credits a destination without
    // authorizing it, but it will not conjure one. `ata create` is permissionless and
    // idempotent, so this is safe to run every time and costs nothing when they are already
    // there. Doing it here rather than in the guest keeps the claim a single atomic transfer.
    let mut created = Vec::new();
    for (ata_id, definition_id) in ata_ids.iter().zip(&definition_ids) {
        let existing = wallet.get_account_public(*ata_id).await;
        let needs_create = match existing {
            Ok(a) => a.program_owner == DEFAULT_PROGRAM_ID,
            Err(_) => true,
        };
        if needs_create {
            let start = current_block(client).await?;
            let hash = Ata(wallet)
                .send_create(AccountIdentity::Public(owner_id), *definition_id)
                .await
                .map_err(|e| anyhow::anyhow!("failed to create the ATA for {definition_id}: {e:?}"))?;
            // WAIT FOR IT TO LAND. send_create only SUBMITS. Firing all three and going straight
            // to the claim meant the claim executed against ATAs that did not exist yet:
            // observed live, the first had landed and the other two were still Uninitialized, so
            // the chained token transfer failed and the sequencer dropped the whole claim with
            // nothing to say beyond "program execution failed on-chain".
            poll_inclusion(client, hash, start)
                .await
                .with_context(|| format!("the ATA for {definition_id} was not created in time"))?;
            created.push(ata_id.to_string());
        }
    }

    // [owner, atas.., treasuries.., marker, clock] - the order the guest splits on.
    let mut accounts: Vec<AccountIdentity> = vec![AccountIdentity::Public(owner_id)];
    accounts.extend(ata_ids.iter().map(|id| AccountIdentity::PublicNoSign(*id)));
    accounts.extend(
        treasury_ids
            .iter()
            .map(|id| AccountIdentity::PublicNoSign(*id)),
    );
    accounts.push(AccountIdentity::PublicNoSign(marker_id));
    accounts.push(AccountIdentity::PublicNoSign(
        clock_core::CLOCK_01_PROGRAM_ACCOUNT_ID,
    ));

    let instruction_data =
        Program::serialize_instruction(shared::Instruction::Claim { ata_program_id })
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
        "claimant": owner_id.to_string(),
        "marker": marker_id.to_string(),
        "definitions": definition_ids.iter().map(ToString::to_string).collect::<Vec<_>>(),
        "atas": ata_ids.iter().map(ToString::to_string).collect::<Vec<_>>(),
        "atasCreated": created,
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
