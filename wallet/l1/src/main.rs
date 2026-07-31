//! medusa-l1: phase-1 Bedrock L1 client for the Medusa wallet.
//!
//! Phase 1 is NODE-SIDE CUSTODY: the zk keys live in the node's own wallet
//! service, this binary holds no key material and reads no password. It talks
//! to the node's REST API through the pinned logos-blockchain client crates
//! (the same git rev the LEZ tree in wallet/.lez-build pins), so requests and
//! responses are wire-compatible with the deployed node.
//!
//! Reads (info, balance, withdraw-status) are plain GETs. The two write verbs
//! (transfer, deposit) POST node-side signing requests and are gated behind
//! MEDUSA_L1_ALLOW_WRITES=1 so the binary is read-only by default.
//!
//! Every subcommand prints a single JSON object to stdout:
//! `{"ok":true,...}` on success, `{"error":"..."}` on failure (exit code 1),
//! matching the medusa-faucet-client convention.
//!
//! Node URL resolution (first non-empty wins, mirroring the zone endpoint
//! pattern in module/src/plugin/WalletPlugin.cpp):
//!   --url flag  >  env MEDUSA_L1_URL  >  ~/.config/medusa-l1.url  >  default.

use std::{
    collections::{HashMap, HashSet},
    time::{Duration, Instant},
};

use anyhow::{Context as _, Result, anyhow, bail};
use base58::FromBase58 as _;
use clap::{Parser, Subcommand};
use logos_blockchain_common_http_client::{BasicAuthCredentials, CommonHttpClient};
use logos_blockchain_core::{
    header::HeaderId,
    mantle::{
        ledger::{Inputs, NoteId},
        ops::channel::{
            ChannelId,
            deposit::{DepositOp, Metadata},
        },
    },
};
use logos_blockchain_groth16::fr_from_bytes;
use logos_blockchain_http_api_common::{
    bodies::{
        channel::{ChannelDepositRequestBody, ChannelDepositResponseBody},
        wallet::{balance::WalletBalanceResponseBody, transfer_funds::WalletTransferFundsRequestBody},
    },
    paths::{CHANNEL_DEPOSIT, MANTLE_SDP_DECLARATIONS},
    settings::default_max_body_size,
};
use logos_blockchain_key_management_system_keys::keys::ZkPublicKey;
use url::Url;

/// Default node: the Paradox Computer Logos testnet Bedrock node.
const DEFAULT_NODE_URL: &str = "https://logos-testnet.paradox.computer/";

/// Default bridge channel for `deposit`: the 0x88...88 channel (64 hex '8's),
/// the channel the Paradox zone settles through.
const DEFAULT_CHANNEL_HEX: &str = "8888888888888888888888888888888888888888888888888888888888888888";

/// Flat max fee for a bridge deposit tx, same value integration_tests/bridge.rs
/// uses against the Bedrock node. It is a CEILING the node enforces, not a
/// reservation this client makes: channel_deposit prices the funded tx and
/// refuses when tx_fee > max_tx_fee, before signing and before mempooling
/// (upstream nodes/node/binary/src/api/handlers.rs, channel_deposit), so an
/// overpriced deposit fails without moving anything. There is deliberately no
/// counterpart for the exact-note mint: WalletTransferFundsRequestBody has no
/// fee field and post_transactions_transfer_funds performs no fee check at all,
/// so this client cannot bound the mint's fee and does not pretend to. See the
/// pre-flight in deposit() for what is checked instead.
const DEPOSIT_MAX_TX_FEE: u64 = 1_000;

/// Phase-1 mapping of the node's 404 "The requested address could not be found
/// in the wallet" wallet-balance answer.
const NOT_REGISTERED_ERROR: &str =
    "key not registered on this node's wallet service - phase 1 serves node-registered keys only";

/// How long `deposit` polls for the freshly minted exact-value note before
/// giving up. Overridable via MEDUSA_L1_POLL_SECS (slots on the public nodes
/// can be up to a minute long, so the default allows a few blocks).
const DEFAULT_POLL_SECS: u64 = 180;

/// Ceiling on MEDUSA_L1_POLL_SECS. NOT a style preference: the poll deadline is
/// `Instant::now() + Duration::from_secs(poll_secs())`, and `Instant`'s Add
/// PANICS on overflow. That line runs AFTER the irreversible mint, so an absurd
/// value in the environment would replace the JSON error object (which carries
/// the mint's tx hash, the user's only handle on a write that already landed)
/// with a bare Rust panic. A day is far longer than any real wait: the note is
/// expected within a few slots.
const MAX_POLL_SECS: u64 = 24 * 60 * 60;

/// Per-request HTTP timeout. reqwest has no default request timeout and
/// CommonHttpClient::new does not set one, so without this any subcommand can
/// block forever against a black-holing node (MEDUSA_L1_POLL_SECS bounds only
/// the deposit poll LOOP, never an individual request), which for the GUI
/// wallet shelling out to this binary means a frozen wallet. Overridable via
/// MEDUSA_L1_HTTP_TIMEOUT_SECS, 0 disables it.
///
/// The default sits strictly ABOVE the node's own request deadline (upstream
/// nodes/api-common/src/settings.rs, default_timeout() is 30s) instead of
/// matching it, so the server always gets to answer first. Equal deadlines race:
/// a request the node was about to fail at 29.9s would be aborted client side
/// and its real error replaced by an opaque reqwest timeout. That is worst on
/// the two write verbs, where the node has already funded and zk-signed the tx
/// by then and may still mempool it, leaving the user with a mint whose tx hash
/// they never see. The margin is set by that server deadline and nothing else:
/// the write verbs' latency against a public node has never been measured (the
/// ~2s figure on record is a read-only channel-state GET).
const DEFAULT_HTTP_TIMEOUT_SECS: u64 = 35;

#[derive(Parser)]
#[command(
    name = "medusa-l1",
    about = "Phase-1 Bedrock L1 client for the Medusa wallet (node-side custody; writes gated)"
)]
struct Args {
    /// Node base URL. Falls back to MEDUSA_L1_URL, then ~/.config/medusa-l1.url,
    /// then the built-in default.
    #[arg(long, global = true)]
    url: Option<String>,

    #[command(subcommand)]
    command: Command,
}

#[derive(Subcommand)]
enum Command {
    /// Consensus info + time info merged into one JSON object.
    Info,
    /// Wallet balance of a node-registered zk public key.
    Balance {
        /// zk public key: 64 hex chars (Bn254 Fr, little-endian byte order,
        /// the same hex the node uses in its /wallet/:public_key/balance path).
        #[arg(long)]
        pk: String,
        /// Optional block id (64 hex chars) to evaluate the balance at.
        #[arg(long)]
        tip: Option<String>,
    },
    /// Transfer funds between node-registered keys. Gated: MEDUSA_L1_ALLOW_WRITES=1.
    Transfer {
        /// Recipient zk public key (64 hex chars).
        #[arg(long)]
        to: String,
        /// Amount to transfer.
        #[arg(long)]
        amount: u64,
        /// Funding zk public key (64 hex chars); must be node-registered.
        #[arg(long)]
        funding_pk: String,
        /// Change zk public key; defaults to --funding-pk.
        #[arg(long)]
        change_pk: Option<String>,
    },
    /// Bridge-deposit L1 funds to a zone account via /channel/deposit.
    /// Gated: MEDUSA_L1_ALLOW_WRITES=1. Needs a note of exactly --amount; if
    /// none exists one is minted first with a self transfer (reported as
    /// "mintedExactNote": true).
    Deposit {
        /// Zone recipient account id (bare base58 or Public/<id>).
        #[arg(long)]
        recipient: String,
        /// Amount to deposit.
        #[arg(long)]
        amount: u64,
        /// Bridge channel id (64 hex chars).
        #[arg(long, default_value = DEFAULT_CHANNEL_HEX)]
        channel: String,
        /// Funding zk public key (64 hex chars); must be node-registered.
        #[arg(long)]
        funding_pk: String,
        /// Refuse the deposit outright when the node's SDP-locked note set
        /// cannot be read, instead of falling back to the looser check that
        /// counts locked notes as spendable ("sdpDeclarationsRead":false).
        #[arg(long)]
        strict_locked_check: bool,
    },
    /// Read-only channel state: balance, withdrawal nonce, tip slot, key count.
    WithdrawStatus {
        /// Channel id (64 hex chars).
        #[arg(long)]
        channel: String,
        /// Optionally also report this zk public key's wallet balance.
        #[arg(long)]
        pk: Option<String>,
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
    let base = base_url(args.url.as_deref())?;
    let client = http_client()?;

    match args.command {
        Command::Info => info(&client, &base).await,
        Command::Balance { pk, tip } => balance(&client, &base, &pk, tip.as_deref()).await,
        Command::Transfer {
            to,
            amount,
            funding_pk,
            change_pk,
        } => transfer(&client, &base, &to, amount, &funding_pk, change_pk.as_deref()).await,
        Command::Deposit {
            recipient,
            amount,
            channel,
            funding_pk,
            strict_locked_check,
        } => {
            deposit(
                &client,
                &base,
                &recipient,
                amount,
                &channel,
                &funding_pk,
                strict_locked_check,
            )
            .await
        }
        Command::WithdrawStatus { channel, pk } => {
            withdraw_status(&client, &base, &channel, pk.as_deref()).await
        }
    }
}

// ---------------------------------------------------------------------------
// Node URL / auth resolution
// ---------------------------------------------------------------------------

/// First non-empty of: --url flag, MEDUSA_L1_URL, ~/.config/medusa-l1.url,
/// baked-in default. Pure so the precedence is unit-testable.
fn resolve_node_url(flag: Option<&str>, env: Option<&str>, file: Option<&str>) -> String {
    for candidate in [flag, env, file] {
        if let Some(raw) = candidate {
            let trimmed = raw.trim();
            if !trimmed.is_empty() {
                return trimmed.to_owned();
            }
        }
    }
    DEFAULT_NODE_URL.to_owned()
}

fn base_url(flag: Option<&str>) -> Result<Url> {
    let env = std::env::var("MEDUSA_L1_URL").ok();
    let file = std::env::home_dir()
        .map(|home| home.join(".config/medusa-l1.url"))
        .and_then(|path| std::fs::read_to_string(path).ok());
    let raw = resolve_node_url(flag, env.as_deref(), file.as_deref());
    let mut url = Url::parse(&raw).with_context(|| format!("invalid node URL '{raw}'"))?;
    // Url::join replaces the last path segment unless the base ends in '/'.
    if !url.path().ends_with('/') {
        url.set_path(&format!("{}/", url.path()));
    }
    Ok(url)
}

/// The node client, built exactly like CommonHttpClient::new does it (same
/// http2 initial stream window) and going through new_with_client only to add
/// the per-request timeout that `new` leaves unset.
fn http_client() -> Result<CommonHttpClient> {
    let initial_stream_window_size: u32 =
        u32::try_from(6 * default_max_body_size() / 10).unwrap_or(4 * 1025);
    let mut builder =
        reqwest::ClientBuilder::new().http2_initial_stream_window_size(initial_stream_window_size);
    let timeout_secs = http_timeout_secs();
    if timeout_secs > 0 {
        builder = builder.timeout(Duration::from_secs(timeout_secs));
    }
    let inner = builder.build().context("failed to build the HTTP client")?;
    Ok(CommonHttpClient::new_with_client(
        inner,
        basic_auth_from_env(),
    ))
}

/// Per-request timeout in seconds; 0 means no timeout. Unparseable values fall
/// back to the default, same as poll_secs.
fn http_timeout_secs() -> u64 {
    std::env::var("MEDUSA_L1_HTTP_TIMEOUT_SECS")
        .ok()
        .and_then(|v| v.parse().ok())
        .unwrap_or(DEFAULT_HTTP_TIMEOUT_SECS)
}

/// Optional basic auth: MEDUSA_L1_BASIC_AUTH="user:pass" (or just "user").
fn basic_auth_from_env() -> Option<BasicAuthCredentials> {
    let raw = std::env::var("MEDUSA_L1_BASIC_AUTH").ok()?;
    let trimmed = raw.trim();
    if trimmed.is_empty() {
        return None;
    }
    Some(match trimmed.split_once(':') {
        Some((user, pass)) => {
            BasicAuthCredentials::new(user.to_owned(), Some(pass.to_owned()))
        }
        None => BasicAuthCredentials::new(trimmed.to_owned(), None),
    })
}

// ---------------------------------------------------------------------------
// Parsing helpers
// ---------------------------------------------------------------------------

fn parse_hex32(input: &str, what: &str) -> Result<[u8; 32]> {
    let trimmed = input.trim();
    let bare = trimmed.strip_prefix("0x").unwrap_or(trimmed);
    let bytes =
        hex::decode(bare).map_err(|e| anyhow!("invalid {what} '{trimmed}': not hex ({e})"))?;
    bytes.as_slice().try_into().map_err(|_| {
        anyhow!(
            "invalid {what} '{trimmed}': expected 32 bytes (64 hex chars), got {}",
            bytes.len()
        )
    })
}

/// Parse a zk public key from the node's own hex convention: the 32
/// little-endian bytes of the Bn254 Fr element (hex::encode(fr_to_bytes(pk)),
/// exactly what the node embeds in the /wallet/:public_key/balance path).
fn parse_zk_pk(input: &str) -> Result<ZkPublicKey> {
    let bytes = parse_hex32(input, "zk public key")?;
    let fr = fr_from_bytes(&bytes)
        .map_err(|e| anyhow!("invalid zk public key '{}': {e}", input.trim()))?;
    Ok(ZkPublicKey::new(fr))
}

fn parse_header_id(input: &str) -> Result<HeaderId> {
    Ok(HeaderId::from(parse_hex32(input, "block id")?))
}

fn parse_channel_id(input: &str) -> Result<ChannelId> {
    Ok(ChannelId::from(parse_hex32(input, "channel id")?))
}

fn normalize_hex(input: &str) -> String {
    let trimmed = input.trim();
    trimmed
        .strip_prefix("0x")
        .unwrap_or(trimmed)
        .to_ascii_lowercase()
}

/// Parse a zone account id (lee AccountId) into its 32 raw bytes WITHOUT
/// depending on the lee crates: base58-decode + length check, the exact
/// inverse of lee AccountId's Display/FromStr.
fn parse_zone_account(input: &str) -> Result<[u8; 32]> {
    let trimmed = input.trim();
    if trimmed.starts_with("Private/") {
        bail!("bridge deposits are public-flow only, got private account '{trimmed}'");
    }
    let bare = trimmed.strip_prefix("Public/").unwrap_or(trimmed);
    let bytes = bare
        .from_base58()
        .map_err(|e| anyhow!("invalid zone account id '{trimmed}': {e:?}"))?;
    bytes.as_slice().try_into().map_err(|_| {
        anyhow!(
            "invalid zone account id '{trimmed}': expected 32 bytes, got {}",
            bytes.len()
        )
    })
}

/// Pick any note holding exactly `amount` from a notes map (the /channel/deposit
/// DepositOp consumes whole notes, so the input must match the amount exactly).
fn select_exact_note<K>(notes: &HashMap<K, u64>, amount: u64) -> Option<K>
where
    K: Copy + Eq + std::hash::Hash,
{
    notes
        .iter()
        .find_map(|(note_id, value)| (*value == amount).then_some(*note_id))
}

/// First `max` chars of a value's JSON rendering, so an error message can quote
/// an unexpected body without splatting the whole thing. `{value:.120}` does
/// NOT do this: serde_json's Display goes through write_str, which ignores the
/// formatter's precision, and this response is the whole network's declaration
/// list.
fn json_excerpt(value: &serde_json::Value, max: usize) -> String {
    let rendered = value.to_string();
    match rendered.char_indices().nth(max) {
        Some((cut, _)) => format!("{}...", &rendered[..cut]),
        None => rendered,
    }
}

/// Pull the SDP-locked note ids out of a GET /mantle/sdp/declarations body.
/// Pure, so the wire shape is unit-testable against a recorded response.
///
/// Deliberately NOT deserialized into lb_core::sdp::Declaration. The deployed
/// public node answers this route with a `withdraw_at` field where the pinned
/// rev's struct declares `withdrawn` (verified live against
/// https://logos-testnet.paradox.computer/), so the node is NOT on the pinned
/// rev for this type and binding to the whole struct would bind to fields that
/// already drifted. Only `locked_note_id` is read, which makes every other
/// field's shape irrelevant; anything unexpected in THAT field fails the whole
/// lookup rather than quietly yielding a short set, because a partial locked
/// set would understate what is locked and that is the unsafe direction.
fn parse_locked_note_ids(body: &serde_json::Value) -> Result<HashSet<NoteId>> {
    let entries = body.as_array().ok_or_else(|| {
        anyhow!(
            "expected a JSON array of SDP declarations, got {}",
            json_excerpt(body, 120)
        )
    })?;
    entries
        .iter()
        .map(|entry| {
            let raw = entry
                .get("locked_note_id")
                .and_then(serde_json::Value::as_str)
                .ok_or_else(|| {
                    anyhow!(
                        "SDP declaration without a string locked_note_id: {}",
                        json_excerpt(entry, 120)
                    )
                })?;
            let bytes = parse_hex32(raw, "locked note id")?;
            let fr = fr_from_bytes(&bytes)
                .map_err(|e| anyhow!("invalid locked note id '{}': {e}", raw.trim()))?;
            Ok(NoteId(fr))
        })
        .collect()
}

/// The TRUE spendable note set: everything GET /wallet/:pk/balance reports,
/// MINUS everything the SDP ledger has locked.
///
/// The balance endpoint counts locked notes: WalletState::balance() walks the
/// same pk_index it sums and does not consult locked_notes (upstream
/// wallet/src/lib.rs), and the handler returns that map verbatim. fund_tx, on
/// the other hand, chains WalletState::locked_notes into the consumed_or_locked
/// filter and cannot spend them. Every guard in deposit() therefore reasons
/// over THIS map and not over bal.notes.
///
/// WHAT THIS SUBTRACTION IS AND IS NOT. It is correct for every note the
/// declarations endpoint knows about, and the endpoint is known upstream to
/// FORGET notes that stay locked permanently, so a residual survives even when
/// the lookup SUCCEEDS. It is NOT a superset of the ledger's locked set, and no
/// guard here may be justified as if it were. Both directions, on the pinned
/// rev:
///
/// Listed but no longer locked (harmless, over-cautious). A declaration that has
/// been withdrawn stays listed until garbage collection while the ledger has
/// already released its note, so subtracting it can refuse a deposit that would
/// have worked. Nothing is written, so this direction is safe.
///
/// Locked but no longer listed (the residual, and it is real).
///   - ledger/src/mantle/sdp/mod.rs:210 unlock_notes_from_withdrawn_declarations
///     unlocks ONLY when declaration.withdrawn is Some and epoch >= withdrawn,
///     and it is the only non-test caller of LockedNotes::unlock in the tree;
///   - ledger/src/mantle/sdp/mod.rs:252 is_expired is true on withdrawn OR
///     inactive, and the inactive arm (active + inactivity_period +
///     retention_period < epoch) needs no withdrawal at all;
///   - ledger/src/mantle/sdp/mod.rs:230 gc_declarations then REMOVES such a
///     declaration, and core/src/mantle/ops/sdp/withdraw.rs:42
///     SDPWithdrawOp::validate fails DeclarationNotFound afterwards, so the note
///     can never be unlocked again.
/// A provider that simply goes quiet without withdrawing therefore leaves its
/// note locked FOREVER and absent from this endpoint, and the shipped devnet
/// settings (deployment/cfgsync/deployment-settings.yaml, inactivity_period: 1,
/// retention_period: 1) put that three epochs after the provider stops.
///
/// So: a note this function REMOVES is proved unspendable-or-recently-released,
/// which makes every refusal downstream at worst over-cautious. A note it KEEPS
/// is NOT proved spendable. The guards below are refusals, so they stay sound;
/// what may not be claimed is that surviving them proves the deposit fundable.
fn spendable_notes(
    notes: &HashMap<NoteId, u64>,
    locked: &HashSet<NoteId>,
) -> HashMap<NoteId, u64> {
    notes
        .iter()
        .filter(|(note_id, _)| !locked.contains(*note_id))
        .map(|(note_id, value)| (*note_id, *value))
        .collect()
}

/// Sum of a spendable note map. Saturating rather than `sum()`: this is a
/// subset of a total the node already fitted into a u64 so it cannot overflow,
/// and a wrap (or a debug panic) would be a terrible way to discover otherwise
/// on the path guarding an irreversible write.
fn spendable_balance(notes: &HashMap<NoteId, u64>) -> u64 {
    notes
        .values()
        .fold(0u64, |acc, value| acc.saturating_add(*value))
}

/// Re-render an error with the degraded-lookup notice appended, so the JSON
/// `{"error":...}` string carries it too and not just the success object.
/// anyhow renders context outermost-first, so the notice is appended by hand
/// instead of pushed on with `.context()`, which would print it before the
/// actual failure.
fn note_degraded_lock_check(err: anyhow::Error, warning: Option<&str>) -> anyhow::Error {
    match warning {
        Some(warning) => anyhow!(
            "{err:#} (the SDP-locked-note pre-check could not run, so this verdict was reached \
             from a balance that may count notes the node cannot spend: {warning})"
        ),
        None => err,
    }
}

/// Append an already-landed mint's tx hash to an error. The mint is the one
/// irreversible step in a deposit, so no failure after it may be reported
/// without the handle the user needs to recover: not the poll's, not the
/// deposit POST's. Appended by hand for the same reason as
/// note_degraded_lock_check.
fn note_landed_mint(err: anyhow::Error, mint_hash: Option<&serde_json::Value>) -> anyhow::Error {
    match mint_hash {
        Some(hash) => anyhow!(
            "{err:#} (the exact-value note minted by tx {hash} is already on the funding key, so \
             top the key up by any amount and re-run rather than minting a second one)"
        ),
        None => err,
    }
}

/// A deposit of 0 can never succeed, so it is refused before any request. The
/// exact-note mint would ask the node for a zero-valued output, and the LEDGER
/// refuses it: Outputs::validate returns OutputsError::ZeroValueNote for any
/// note with value == 0 (core/src/mantle/ledger.rs:113-114), reached from
/// TransferOp::validate (core/src/mantle/ops/transfer.rs:84). The tx BUILDER is
/// not what stops it, despite the "we cannot create zero-valued outputs" comment
/// at core/src/mantle/tx_builder.rs:160 that an earlier revision of this comment
/// cited: that line sits inside return_change and only explains why the
/// Ordering::Equal arm emits no CHANGE note. add_ledger_output/
/// extend_ledger_outputs (tx_builder.rs:131-146) push a note with no value check
/// at all, and with_dummy_change_note (tx_builder.rs:186) deliberately adds a
/// Note { value: 0, .. }. Without this guard the tool would mint nothing, poll
/// the full MEDUSA_L1_POLL_SECS for a note that is never coming, and then
/// report a timeout as if the note were merely late.
fn ensure_depositable_amount(amount: u64) -> Result<()> {
    if amount == 0 {
        bail!("deposit amount must be greater than 0");
    }
    Ok(())
}

/// The node answers the wallet-balance GET for an unknown key with
/// 404 "The requested address could not be found in the wallet"; the http
/// client folds that into its error text. Map it to the phase-1 message.
fn map_wallet_balance_error(err_text: &str) -> Option<&'static str> {
    err_text
        .contains("could not be found")
        .then_some(NOT_REGISTERED_ERROR)
}

/// The write gate itself: only an exact "1" opens it, everything else (unset,
/// "true", "yes", "0", blank) fails closed. Pure over the env value so it is
/// unit-testable without mutating the process environment, the same way
/// resolve_node_url is.
fn writes_enabled(env: Option<&str>) -> bool {
    env.is_some_and(|v| v.trim() == "1")
}

/// writes_enabled as a Result, carrying the documented error message.
fn ensure_writes_allowed(env: Option<&str>) -> Result<()> {
    if !writes_enabled(env) {
        bail!("writes disabled: set MEDUSA_L1_ALLOW_WRITES=1");
    }
    Ok(())
}

fn ensure_writes_enabled() -> Result<()> {
    ensure_writes_allowed(std::env::var("MEDUSA_L1_ALLOW_WRITES").ok().as_deref())
}

/// Pre-flight for the IRREVERSIBLE exact-note mint in deposit(). Pure over the
/// two numbers the verdict turns on, the same way writes_enabled is pure over
/// the env value, so the boundary can be unit-tested without a write.
///
/// The mint is POST /wallet/transactions/transfer-funds sending `amount` back
/// to the funding key. Upstream builds it as a single output of `amount` handed
/// to fund_tx (services/wallet/src/api.rs, transfer_funds), and fund_tx spends
/// the key's notes largest-first, stopping at the first prefix whose
/// funding_delta reaches 0 (wallet/src/lib.rs). Three outcomes:
///   delta < 0 on every prefix - the mint itself fails, nothing moves;
///   delta > 0                 - return_change emits a change note, which is
///                               exactly the second note the deposit needs;
///   delta == 0                - the Ordering::Equal arm returns with NO change
///                               note, and if that prefix was the whole note
///                               set the key is left holding only the exact
///                               note and the deposit is then guaranteed to
///                               fail AFTER an irreversible write.
///
/// Which one hits is decided by the fee, and this client cannot compute it. On
/// the pinned rev it is provably zero: GENESIS_STORAGE_GAS_PRICE and
/// GENESIS_EXECUTION_GAS_PRICE are both GasPrice::new(0)
/// (core/src/mantle/genesis_tx.rs), every branch of update_storage_market and
/// update_execution_market MULTIPLIES the previous price
/// (ledger/src/cryptarchia/mod.rs) so zero is an absorbing state, and
/// total_gas_cost is gas * price (core/src/mantle/gas.rs, GasCost::calculate).
/// At a zero fee the zero-delta-on-the-whole-note-set case happens on exactly
/// one input, spendable == amount, so that is the whole guard. Nothing is
/// reserved and no fee constant is invented; a chain with non-zero gas prices
/// moves the case to `spendable == amount + mint fee`, which is documented as
/// residual risk in README.md rather than papered over with a guessed number.
///
/// `spendable` MUST be the SPENDABLE balance, i.e. spendable_balance() over
/// spendable_notes(), not the raw figure from the balance endpoint. fund_tx
/// cannot touch SDP-locked notes, so feeding it the reported balance is what
/// let a key holding {3000, 2000} spendable plus a 100000 locked note pass this
/// guard at --amount 5000 and mint irreversibly into a deposit that could never
/// be funded.
fn ensure_mint_leaves_a_second_note(spendable: u64, amount: u64) -> Result<()> {
    if spendable < amount {
        bail!("insufficient funds: spendable balance {spendable} < deposit amount {amount}");
    }
    if spendable == amount {
        bail!(
            "spendable balance is exactly the deposit amount {amount}, so minting the exact-value \
             note would spend every spendable note and leave nothing to fund the deposit tx: top \
             the funding key up by any amount and re-run"
        );
    }
    Ok(())
}

/// MEDUSA_L1_POLL_SECS, clamped to MAX_POLL_SECS. Pure over the env value like
/// writes_enabled, so the clamp is unit-testable; see MAX_POLL_SECS for why an
/// unclamped value is a correctness problem and not just an ergonomic one.
fn clamp_poll_secs(env: Option<&str>) -> u64 {
    env.and_then(|v| v.trim().parse::<u64>().ok())
        .unwrap_or(DEFAULT_POLL_SECS)
        .min(MAX_POLL_SECS)
}

fn poll_secs() -> u64 {
    clamp_poll_secs(std::env::var("MEDUSA_L1_POLL_SECS").ok().as_deref())
}

async fn query_wallet_balance(
    client: &CommonHttpClient,
    base: &Url,
    pk: ZkPublicKey,
    tip: Option<HeaderId>,
) -> Result<WalletBalanceResponseBody> {
    client
        .get_wallet_balance(base.clone(), pk, tip)
        .await
        .map_err(|e| match map_wallet_balance_error(&e.to_string()) {
            Some(mapped) => anyhow!(mapped),
            None => anyhow!(e).context("wallet balance query failed"),
        })
}

/// Read-only GET /mantle/sdp/declarations, reduced to the locked note-id set.
/// This is the whole network's SDP declaration list, not just the funding key's,
/// so on a busy node it can be large; nothing here streams it, and the per-request
/// MEDUSA_L1_HTTP_TIMEOUT_SECS is what bounds it. That is also why a failure is
/// treated as degradation rather than as a fatal error by default: see deposit().
async fn query_locked_note_ids(client: &CommonHttpClient, base: &Url) -> Result<HashSet<NoteId>> {
    // Relative join for the same reason as the /channel/deposit POST below: the
    // upstream path constants are absolute but every client verb strips the
    // leading '/' before joining, so a base URL with a path prefix keeps it.
    let request_url = base.join(MANTLE_SDP_DECLARATIONS.trim_start_matches('/'))?;
    let body = client
        .get::<(), serde_json::Value>(request_url, None)
        .await
        .map_err(|e| anyhow!(e).context("SDP declarations query failed"))?;
    parse_locked_note_ids(&body)
}

// ---------------------------------------------------------------------------
// Subcommands
// ---------------------------------------------------------------------------

async fn info(client: &CommonHttpClient, base: &Url) -> Result<serde_json::Value> {
    let chain = client
        .consensus_info(base.clone())
        .await
        .map_err(|e| anyhow!(e).context("consensus info query failed"))?;
    let time = client
        .time_info(base.clone())
        .await
        .map_err(|e| anyhow!(e).context("time info query failed"))?;

    Ok(serde_json::json!({
        "ok": true,
        "height": chain.cryptarchia_info.height,
        "slot": u64::from(chain.cryptarchia_info.slot),
        "libSlot": u64::from(chain.cryptarchia_info.lib_slot),
        "lib": serde_json::to_value(chain.cryptarchia_info.lib)?,
        "mode": serde_json::to_value(&chain.mode)?,
        "slotDurationMs": time.slot_duration_ms,
        "currentEpoch": time.current_epoch,
    }))
}

async fn balance(
    client: &CommonHttpClient,
    base: &Url,
    pk_hex: &str,
    tip_hex: Option<&str>,
) -> Result<serde_json::Value> {
    let pk = parse_zk_pk(pk_hex)?;
    let tip = tip_hex.map(parse_header_id).transpose()?;
    let resp = query_wallet_balance(client, base, pk, tip).await?;

    Ok(serde_json::json!({
        "ok": true,
        "pk": normalize_hex(pk_hex),
        "balance": resp.balance,
        "notes": resp.notes.len(),
        "address": serde_json::to_value(resp.address)?,
    }))
}

async fn transfer(
    client: &CommonHttpClient,
    base: &Url,
    to_hex: &str,
    amount: u64,
    funding_hex: &str,
    change_hex: Option<&str>,
) -> Result<serde_json::Value> {
    ensure_writes_enabled()?;

    let recipient = parse_zk_pk(to_hex)?;
    let funding = parse_zk_pk(funding_hex)?;
    let change = match change_hex {
        Some(hex_str) => parse_zk_pk(hex_str)?,
        None => funding,
    };

    let body = WalletTransferFundsRequestBody {
        tip: None,
        change_public_key: change,
        funding_public_keys: vec![funding],
        recipient_public_key: recipient,
        amount,
    };
    let resp = client
        .transfer_funds(base.clone(), body)
        .await
        .map_err(|e| anyhow!(e).context("transfer-funds request failed"))?;

    Ok(serde_json::json!({
        "ok": true,
        "txHash": serde_json::to_value(resp.hash)?,
    }))
}

async fn deposit(
    client: &CommonHttpClient,
    base: &Url,
    recipient: &str,
    amount: u64,
    channel_hex: &str,
    funding_hex: &str,
    strict_locked_check: bool,
) -> Result<serde_json::Value> {
    ensure_writes_enabled()?;
    ensure_depositable_amount(amount)?;

    // The SDP-locked note set, read-only, BEFORE anything is written.
    //
    // Failing to read it is DEGRADATION, not a fatal error, unless the caller
    // asked for --strict-locked-check. The reasoning, spelled out because it
    // decides whether an irreversible mint is allowed to proceed on incomplete
    // information:
    //   - this lookup REFINES a guard that is already sound on its own terms.
    //     With an empty set every note counts as spendable, which is exactly how
    //     this tool behaved before the check existed, so a failed lookup is
    //     never WORSE than the previous release, only no better;
    //   - the residual it fails to catch costs no funds. The mint lands, the
    //     deposit POST then fails, and the exact-value note stays on the key: a
    //     top-up plus a re-run completes the deposit;
    //   - hard-failing would introduce false negatives, and non-deterministic
    //     ones. One 500 on an unrelated route, an older node without the route,
    //     or a proxy that filters it would refuse a deposit that plainly works,
    //     for the overwhelmingly common key that owns no locked note at all.
    //     Trading a certain, frequent failure for an unlikely, recoverable one
    //     is a bad trade to make silently;
    //   - so it is made loudly instead: "sdpDeclarationsRead":false plus
    //     "lockedNotesWarning" in the JSON on success, the same notice appended
    //     to the JSON error on failure, and --strict-locked-check for callers
    //     (automation, large amounts) that would rather not write at all.
    let (locked_ids, locked_warning) = match query_locked_note_ids(client, base).await {
        Ok(ids) => (ids, None),
        Err(err) if strict_locked_check => {
            return Err(err.context(
                "--strict-locked-check is set, so the deposit is refused rather than run against \
                 a balance that may count notes the node cannot spend",
            ));
        }
        Err(err) => (HashSet::new(), Some(format!("{err:#}"))),
    };

    let mut outcome = deposit_with_locked_set(
        client,
        base,
        recipient,
        amount,
        channel_hex,
        funding_hex,
        &locked_ids,
    )
    .await
    .map_err(|err| note_degraded_lock_check(err, locked_warning.as_deref()))?;

    annotate_lock_check(&mut outcome, &locked_ids, locked_warning.as_deref());
    Ok(outcome)
}

/// Record on a successful deposit object WHAT WAS DONE about the SDP-locked
/// lookup, rather than what is guaranteed. Pure over the two inputs the fields
/// derive from, so the JSON contract is unit-testable without a write.
///
/// This replaces a single `"lockedNotesChecked": true|false`, which asserted
/// more than this code can support, twice over. First, the subtraction is not a
/// superset of the ledger's locked set: see spendable_notes for the GC path that
/// leaves a note locked forever and unlisted, so "checked" was never the same as
/// "clear". Second, a 200 with an empty array is indistinguishable from
/// "nothing was looked at". Upstream produces exactly that body from a missing
/// tip state (services/chain/chain-service/src/lib.rs:897-910 answers
/// `cryptarchia.ledger.state(&tip).map(..).unwrap_or_default()`), and so does
/// any caching or filtering proxy in front of the node.
///
/// So two facts are reported in place of one promise:
///   - `sdpDeclarationsRead`: whether GET /mantle/sdp/declarations answered and
///     parsed. It states that the lookup RAN, and nothing about completeness.
///   - `sdpLockedNotesSeen`: how many distinct locked note ids that answer
///     carried, network-wide. This is what separates a populated answer, where
///     the node demonstrably had SDP state and none of it was this key's, from
///     an empty one that may mean the node had no state to consult. A caller
///     that cares (automation, large amounts) can treat `read: true, seen: 0`
///     with the suspicion it deserves, which the old boolean made impossible.
///     Null rather than 0 when the read failed, because a 0 there would read as
///     a real count.
fn annotate_lock_check(
    outcome: &mut serde_json::Value,
    locked: &HashSet<NoteId>,
    warning: Option<&str>,
) {
    let obj = outcome
        .as_object_mut()
        .expect("deposit_with_locked_set builds a json object");
    obj.insert(
        "sdpDeclarationsRead".into(),
        serde_json::json!(warning.is_none()),
    );
    obj.insert(
        "sdpLockedNotesSeen".into(),
        match warning {
            None => serde_json::json!(locked.len()),
            Some(_) => serde_json::Value::Null,
        },
    );
    if let Some(warning) = warning {
        obj.insert("lockedNotesWarning".into(), serde_json::json!(warning));
    }
}

/// The deposit itself, once the SDP-locked note set is in hand. `locked` empty
/// means either "nothing is locked" or "the lookup failed and we are falling
/// back"; deposit() above is what distinguishes the two for the user.
async fn deposit_with_locked_set(
    client: &CommonHttpClient,
    base: &Url,
    recipient: &str,
    amount: u64,
    channel_hex: &str,
    funding_hex: &str,
    locked: &HashSet<NoteId>,
) -> Result<serde_json::Value> {
    let recipient_bytes = parse_zone_account(recipient)?;
    let channel_id = parse_channel_id(channel_hex)?;
    let funding = parse_zk_pk(funding_hex)?;

    // 1) Find (or mint) a note of exactly `amount`: DepositOp consumes whole
    //    notes, so a value-exact input is required. Modeled on
    //    integration_tests/tests/bridge.rs (submit_bedrock_deposit).
    let bal = query_wallet_balance(client, base, funding, None).await?;
    let mut spendable = spendable_notes(&bal.notes, locked);
    // Recomputed on every successful poll below, alongside `spendable`. It used
    // to be a `let` taken once here and still reported at the end, so on a
    // minted deposit the JSON described a note set that no longer existed while
    // the field's comment read as current.
    let mut locked_here = bal.notes.len() - spendable.len();
    let mut minted_exact_note = false;
    // Set as soon as the mint lands, so every later failure can carry it.
    let mut mint_hash: Option<serde_json::Value> = None;
    // Selected from the SPENDABLE map, so a locked note is never offered to the
    // DepositOp as its input either; the node would reject that deposit.
    let mut selected_note = select_exact_note(&spendable, amount);

    // The pre-flight. What the node requires is not an amount of currency, it
    // is that once the DepositOp has consumed its exact-value note WHOLE, at
    // least one OTHER spendable note is left to fund the deposit tx: fund_tx
    // filters out every note the tx already consumes AND every note the SDP
    // ledger has locked (upstream core/src/mantle/tx_builder.rs,
    // consumed_or_locked_notes lists Op::ChannelDeposit inputs, and
    // wallet/src/lib.rs fund_tx chains WalletState::locked_notes onto it), and
    // with an empty candidate pool its `for i in 0..utxos.len()` body never
    // runs, so it returns InsufficientFunds { available: 0 } however small the
    // fee is. `spendable` reproduces BOTH halves of that filter, which is why
    // one spendable note PROVES the POST fails and is refused up front.
    if selected_note.is_some() {
        if spendable.len() < 2 {
            bail!(
                "the exact-value note is the only SPENDABLE note on this key ({locked_here} more \
                 are SDP-locked and cannot fund a tx), so nothing is left to fund the deposit tx: \
                 top the funding key up by any amount and re-run"
            );
        }
    } else {
        // No exact note, so one has to be minted, and the mint is the one
        // irreversible step here. This is where conservatism gets spent.
        ensure_mint_leaves_a_second_note(spendable_balance(&spendable), amount)?;

        // Self transfer of `amount` (recipient = change = funding key) mints
        // an exact-value note.
        let mint_body = WalletTransferFundsRequestBody {
            tip: None,
            change_public_key: funding,
            funding_public_keys: vec![funding],
            recipient_public_key: funding,
            amount,
        };
        let mint = client
            .transfer_funds(base.clone(), mint_body)
            .await
            .map_err(|e| anyhow!(e).context("exact-note mint (self transfer) failed"))?;
        minted_exact_note = true;
        // Rendered ONCE, here, and never again: a `?` on this call inside one of
        // the bails below would be one more way to lose the hash of a write that
        // has already landed.
        let hash = serde_json::to_value(mint.hash)?;
        mint_hash = Some(hash.clone());

        let deadline = Instant::now() + Duration::from_secs(poll_secs());
        loop {
            tokio::time::sleep(Duration::from_secs(3)).await;
            // A poll failure must NEVER discard the hash of a mint that has
            // already landed, so it is carried to the deadline and retried
            // instead of propagated. The deadline still bounds the loop and the
            // bail below reports both the hash and the last error.
            let poll_error = match query_wallet_balance(client, base, funding, None).await {
                Ok(fresh) => {
                    spendable = spendable_notes(&fresh.notes, locked);
                    locked_here = fresh.notes.len() - spendable.len();
                    selected_note = select_exact_note(&spendable, amount);
                    if selected_note.is_some() {
                        break;
                    }
                    None
                }
                Err(err) => Some(format!("{err:#}")),
            };
            if Instant::now() >= deadline {
                match poll_error {
                    Some(err) => bail!(
                        "gave up waiting for the exact-value note minted by tx {hash} because the \
                         wallet balance query kept failing ({err}) - the mint has already landed, \
                         so re-run the deposit once the node answers again"
                    ),
                    None => bail!(
                        "timed out waiting for the exact-value note minted by tx {hash} - re-run \
                         the deposit once it lands"
                    ),
                }
            }
        }

        // Same proof as above, re-checked on the post-mint SPENDABLE set. This
        // is genuinely reachable, unlike the bal.notes.len() version it
        // replaces: that one counted locked notes, so in the very scenario it
        // existed for (locked notes inflating the pre-flight's balance) the
        // post-mint set was always >= 2 and the check could never fire. Over
        // the spendable set it fires when the locked lookup was unavailable and
        // fell back, or when the note set changed under us between the
        // pre-flight and the mint. The mint has already landed and cannot be
        // undone, so the best available outcome is an actionable message
        // carrying its tx hash, instead of the node's bare InsufficientFunds
        // from the POST below.
        if spendable.len() < 2 {
            bail!(
                "the exact-value note minted by tx {hash} is now the only SPENDABLE note on this \
                 key, so nothing is left to fund the deposit tx (SDP-locked notes are counted by \
                 the balance endpoint but cannot be spent) - top the funding key up by any amount \
                 and re-run, the exact-value note is already there"
            );
        }
    }
    let note_id = selected_note.expect("note selected or bailed above");

    // 2) The deposit metadata is the borsh-serialized zone (lee) AccountId.
    //    lee AccountId is a plain 32-byte newtype, so its borsh form is exactly
    //    the 32 raw bytes; mirror that here without depending on the lee crates.
    #[derive(borsh::BorshSerialize)]
    struct DepositMetadata {
        recipient_id: [u8; 32],
    }
    // Every `?` from here on is on the far side of the irreversible mint, so
    // each one is decorated with the mint hash rather than propagated bare.
    // These three are practically unreachable (32 fixed bytes, MAX_METADATA_SIZE,
    // a base URL that already joined once), but "practically unreachable" is not
    // "reports the hash", and the README claims the latter without hedging.
    let encoded_metadata = borsh::to_vec(&DepositMetadata {
        recipient_id: recipient_bytes,
    })
    .context("Failed to encode deposit metadata")
    .map_err(|err| note_landed_mint(err, mint_hash.as_ref()))?;
    let metadata: Metadata = encoded_metadata
        .try_into()
        .context("Encoded metadata is too big")
        .map_err(|err| note_landed_mint(err, mint_hash.as_ref()))?;

    // 3) POST /channel/deposit with the exact note as the single input.
    let body = ChannelDepositRequestBody {
        tip: None,
        deposit: DepositOp {
            channel_id,
            inputs: Inputs::new(note_id),
            metadata,
        },
        change_public_key: funding,
        funding_public_keys: vec![funding],
        max_tx_fee: DEPOSIT_MAX_TX_FEE.into(),
    };
    // The leading '/' is stripped ON PURPOSE, do not "fix" it: the upstream
    // path constants are absolute but the client's own verbs strip them before
    // joining, so the join is RELATIVE to the base URL and a base with a path
    // prefix keeps that prefix. consensus_info, time_info, channel_state and
    // transfer_funds all do exactly this (upstream nodes/node/http-client, the
    // `.join(X.trim_start_matches('/'))` calls), which is why base_url() above
    // has to append the trailing '/'. Only get_wallet_balance joins absolutely
    // and it is the odd one out upstream, not the pattern to copy here.
    let request_url = base
        .join(CHANNEL_DEPOSIT.trim_start_matches('/'))
        .map_err(|e| {
            note_landed_mint(
                anyhow!(e).context("could not build the channel deposit URL"),
                mint_hash.as_ref(),
            )
        })?;
    let resp: ChannelDepositResponseBody = client
        .post(request_url, &body)
        .await
        .map_err(|e| {
            note_landed_mint(
                anyhow!(e).context("channel deposit request failed"),
                mint_hash.as_ref(),
            )
        })?;

    // Same reason as the metadata encode above: rendered before the object is
    // built, so a serialization failure here still carries the mint hash.
    let deposit_hash = serde_json::to_value(resp.hash)
        .map_err(|e| {
            note_landed_mint(
                anyhow!(e).context("could not render the deposit tx hash"),
                mint_hash.as_ref(),
            )
        })?;
    let deposit_note = serde_json::to_value(note_id).map_err(|e| {
        note_landed_mint(
            anyhow!(e).context("could not render the deposit note id"),
            mint_hash.as_ref(),
        )
    })?;

    Ok(serde_json::json!({
        "ok": true,
        "txHash": deposit_hash,
        "channel": channel_id.to_string(),
        "noteId": deposit_note,
        "amount": amount,
        "mintedExactNote": minted_exact_note,
        // How many of the funding key's own notes the SDP-locked set removed
        // from consideration, over the LAST balance this deposit read (the
        // post-mint one when a note was minted), not the pre-flight one. Always
        // 0 when the lookup fell back, which "sdpDeclarationsRead":false says.
        "lockedNotesExcluded": locked_here,
    }))
}

async fn withdraw_status(
    client: &CommonHttpClient,
    base: &Url,
    channel_hex: &str,
    pk_hex: Option<&str>,
) -> Result<serde_json::Value> {
    let channel_id = parse_channel_id(channel_hex)?;
    let state = client
        .channel_state(base.clone(), channel_id)
        .await
        .map_err(|e| anyhow!(e).context("channel state query failed"))?
        .ok_or_else(|| anyhow!("channel {channel_id} not found on this node"))?;

    let mut out = serde_json::json!({
        "ok": true,
        "channel": channel_id.to_string(),
        "channelBalance": state.balance,
        "withdrawalNonce": state.withdrawal_nonce,
        "tipSlot": u64::from(state.tip_slot),
        "accreditedKeys": state.accredited_keys.len(),
    });

    if let Some(pk_hex) = pk_hex {
        let pk = parse_zk_pk(pk_hex)?;
        let obj = out.as_object_mut().expect("json object built above");
        obj.insert("pk".into(), serde_json::json!(normalize_hex(pk_hex)));
        // Non-fatal: the channel state above is still useful when the key is
        // not registered on this node.
        match client.get_wallet_balance(base.clone(), pk, None).await {
            Ok(bal) => {
                obj.insert("pkBalance".into(), serde_json::json!(bal.balance));
                obj.insert("pkNotes".into(), serde_json::json!(bal.notes.len()));
            }
            Err(e) => match map_wallet_balance_error(&e.to_string()) {
                Some(mapped) => {
                    obj.insert("pkBalance".into(), serde_json::Value::Null);
                    obj.insert("pkError".into(), serde_json::json!(mapped));
                }
                None => return Err(anyhow!(e).context("wallet balance query failed")),
            },
        }
    }

    Ok(out)
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

#[cfg(test)]
mod tests {
    use base58::ToBase58 as _;

    use super::*;

    // -- node URL resolution precedence --------------------------------------

    #[test]
    fn url_flag_wins_over_everything() {
        assert_eq!(
            resolve_node_url(Some("https://a/"), Some("https://b/"), Some("https://c/")),
            "https://a/"
        );
    }

    #[test]
    fn url_env_wins_when_no_flag() {
        assert_eq!(
            resolve_node_url(None, Some("https://b/"), Some("https://c/")),
            "https://b/"
        );
    }

    #[test]
    fn url_file_wins_when_no_flag_or_env() {
        assert_eq!(
            resolve_node_url(None, None, Some("https://c/\n")),
            "https://c/"
        );
    }

    #[test]
    fn url_default_when_nothing_set() {
        assert_eq!(resolve_node_url(None, None, None), DEFAULT_NODE_URL);
    }

    #[test]
    fn url_blank_candidates_are_skipped() {
        assert_eq!(
            resolve_node_url(Some("  "), Some(""), Some("https://c/")),
            "https://c/"
        );
        assert_eq!(resolve_node_url(Some(""), Some("   \n"), None), DEFAULT_NODE_URL);
    }

    // -- write gate (MEDUSA_L1_ALLOW_WRITES) ---------------------------------
    // Pure over the env value, so these never mutate the process environment
    // and stay correct under the default parallel test harness.

    #[test]
    fn write_gate_blocks_when_unset() {
        assert!(!writes_enabled(None));
    }

    #[test]
    fn write_gate_blocks_everything_that_is_not_exactly_one() {
        for value in ["true", "TRUE", "yes", "on", "0", "", " ", "\t\n", "11", "1x", "-1", "01"] {
            assert!(!writes_enabled(Some(value)), "gate opened for {value:?}");
        }
    }

    #[test]
    fn write_gate_opens_only_for_exactly_one() {
        assert!(writes_enabled(Some("1")));
        // Surrounding whitespace is trimmed; the value itself is never coerced.
        assert!(writes_enabled(Some(" 1 ")));
        assert!(writes_enabled(Some("1\n")));
    }

    #[test]
    fn ensure_writes_allowed_reports_the_documented_error() {
        let err = ensure_writes_allowed(None).unwrap_err().to_string();
        assert_eq!(err, "writes disabled: set MEDUSA_L1_ALLOW_WRITES=1");
        assert!(ensure_writes_allowed(Some("yes")).is_err());
        assert!(ensure_writes_allowed(Some("1")).is_ok());
    }

    // -- the write gate on the COMMAND paths ---------------------------------
    // The tests above only pin the pure helpers down; deleting the
    // `ensure_writes_enabled()?` line from transfer() or deposit() leaves every
    // one of them green. These two drive the command functions themselves with
    // the env unset, against a closed loopback port so that reaching any socket
    // is an unmistakable failure, and assert the GATE's message rather than a
    // transport error. Arguments are all valid, so without the gate these get
    // as far as the request and fail with a connection error instead.

    const UNROUTABLE_URL: &str = "http://127.0.0.1:1/";

    fn gate_test_url() -> Url {
        // Never set MEDUSA_L1_ALLOW_WRITES to run these: the point is the
        // unset environment. Fail loudly rather than issue a real write if
        // something in the environment has opened the gate.
        assert!(
            std::env::var("MEDUSA_L1_ALLOW_WRITES").is_err(),
            "MEDUSA_L1_ALLOW_WRITES must be unset for the write-gate tests"
        );
        Url::parse(UNROUTABLE_URL).expect("valid url")
    }

    #[tokio::test]
    async fn transfer_command_is_blocked_by_the_write_gate_before_any_request() {
        let base = gate_test_url();
        let client = http_client().expect("client builds");
        let pk = "11".repeat(32);
        let err = transfer(&client, &base, &pk, 1, &pk, None)
            .await
            .unwrap_err()
            .to_string();
        assert_eq!(err, "writes disabled: set MEDUSA_L1_ALLOW_WRITES=1");
    }

    #[tokio::test]
    async fn deposit_command_is_blocked_by_the_write_gate_before_any_request() {
        let base = gate_test_url();
        let client = http_client().expect("client builds");
        let pk = "11".repeat(32);
        let recipient = [7u8; 32].to_base58();
        let err = deposit(&client, &base, &recipient, 1, DEFAULT_CHANNEL_HEX, &pk, false)
            .await
            .unwrap_err()
            .to_string();
        assert_eq!(err, "writes disabled: set MEDUSA_L1_ALLOW_WRITES=1");
        // The gate also precedes the SDP-locked lookup, which is itself a
        // request: --strict-locked-check must not turn into a way to reach the
        // network with writes disabled.
        let err = deposit(&client, &base, &recipient, 1, DEFAULT_CHANNEL_HEX, &pk, true)
            .await
            .unwrap_err()
            .to_string();
        assert_eq!(err, "writes disabled: set MEDUSA_L1_ALLOW_WRITES=1");
    }

    // -- exact-note mint pre-flight ------------------------------------------

    #[test]
    fn mint_preflight_refuses_a_balance_below_the_amount() {
        let err = ensure_mint_leaves_a_second_note(999, 1000)
            .unwrap_err()
            .to_string();
        assert_eq!(
            err,
            "insufficient funds: spendable balance 999 < deposit amount 1000"
        );
    }

    #[test]
    fn mint_preflight_refuses_a_balance_equal_to_the_amount() {
        // The one input on which the mint succeeds and still strands the key:
        // fund_tx consumes every note, the funding delta lands on exactly 0 and
        // the Ordering::Equal arm emits no change note.
        let err = ensure_mint_leaves_a_second_note(1000, 1000)
            .unwrap_err()
            .to_string();
        assert!(
            err.contains("exactly the deposit amount 1000"),
            "got: {err}"
        );
    }

    #[test]
    fn mint_preflight_allows_any_strict_surplus() {
        // One unit over is enough: the mint emits a 1-unit change note and that
        // is what funds the deposit. No fee is reserved on top.
        assert!(ensure_mint_leaves_a_second_note(1001, 1000).is_ok());
        assert!(ensure_mint_leaves_a_second_note(u64::MAX, 1000).is_ok());
        // A zero-amount deposit is refused by the equality arm, not by u64
        // arithmetic: there is nothing to overflow any more.
        assert!(ensure_mint_leaves_a_second_note(0, 0).is_err());
    }

    // -- zero-amount deposits -------------------------------------------------

    #[test]
    fn zero_amount_deposits_are_refused_before_any_request() {
        // The node cannot create a zero-valued output, so the mint would never
        // produce a note and the tool would poll for the full MEDUSA_L1_POLL_SECS
        // and then blame a timeout.
        let err = ensure_depositable_amount(0).unwrap_err().to_string();
        assert_eq!(err, "deposit amount must be greater than 0");
        assert!(ensure_depositable_amount(1).is_ok());
        assert!(ensure_depositable_amount(u64::MAX).is_ok());
    }

    // -- SDP-locked notes -----------------------------------------------------

    /// A distinct NoteId per seed. Byte 0 only, so the little-endian Fr is a
    /// small integer and always below the Bn254 modulus.
    fn note_id(seed: u8) -> NoteId {
        let mut bytes = [0u8; 32];
        bytes[0] = seed;
        NoteId(fr_from_bytes(&bytes).expect("small Fr is in range"))
    }

    /// Verbatim from the live public node, GET
    /// https://logos-testnet.paradox.computer/mantle/sdp/declarations
    /// (2026-07-31, HTTP 200, application/json, 5 entries, 2 kept here).
    /// NOTE the `withdraw_at` field: the pinned rev's lb_core::sdp::Declaration
    /// calls that field `withdrawn`, so the deployed node is NOT on the pinned
    /// rev for this type. This test is the reason parse_locked_note_ids reads
    /// `locked_note_id` out of a plain Value instead of deserializing the whole
    /// upstream struct.
    const LIVE_SDP_DECLARATIONS: &str = r#"[
      {"service_type":"BN",
       "provider_id":"7fce82aa171b349a40f2a98ce9a05b5db62415adfd3ab4ffea6f214443d5909d",
       "locked_note_id":"dfcf888cdb5d071d1b7eb3e5f9a4b5869de65c907676e6336dc098885ab8ec09",
       "locators":["/ip4/65.109.51.37/udp/3401/quic-v1"],
       "zk_id":"ae9a47f76c9a2c00c40310cc82a1da9f5292af9ee223b898c38478c85fd39515",
       "created":0,"active":73,"withdraw_at":null,"nonce":59},
      {"service_type":"BN",
       "provider_id":"35d60d973560b8344f83dc266a3fe89e35a3dcf9959c492d0a7a0b7a85c5d2ce",
       "locked_note_id":"024ae4d401dd450d2518ee61896545d4db931a48a139030140c2fc2e5cc2bf23",
       "locators":["/ip4/209.38.241.182/udp/3400/quic-v1"],
       "zk_id":"16970636b0333935082d0fa28ca96279acadb6fcfa97afef2825a0bc37c9391a",
       "created":1,"active":74,"withdraw_at":null,"nonce":71}
    ]"#;

    #[test]
    fn parses_the_live_nodes_sdp_declarations() {
        let body: serde_json::Value =
            serde_json::from_str(LIVE_SDP_DECLARATIONS).expect("recorded body is valid json");
        let locked = parse_locked_note_ids(&body).expect("live shape parses");
        assert_eq!(locked.len(), 2);

        // The ids really are the ones on the wire: same hex, round-tripped
        // through the pinned NoteId type.
        let expected = "dfcf888cdb5d071d1b7eb3e5f9a4b5869de65c907676e6336dc098885ab8ec09";
        let bytes = parse_hex32(expected, "note id").expect("valid hex");
        assert!(locked.contains(&NoteId(fr_from_bytes(&bytes).expect("valid Fr"))));
    }

    #[test]
    fn the_declarations_url_is_joined_relative_to_the_base() {
        // The reason the leading '/' is stripped: a base URL carrying a path
        // prefix (a reverse-proxied node) must keep it, exactly as the
        // /channel/deposit POST does.
        let joined = |base: &str| {
            Url::parse(base)
                .expect("valid base")
                .join(MANTLE_SDP_DECLARATIONS.trim_start_matches('/'))
                .expect("valid join")
                .to_string()
        };
        assert_eq!(
            joined("https://logos-testnet.paradox.computer/"),
            "https://logos-testnet.paradox.computer/mantle/sdp/declarations"
        );
        assert_eq!(
            joined("https://example.test/node/"),
            "https://example.test/node/mantle/sdp/declarations"
        );
    }

    #[test]
    fn error_bodies_are_quoted_but_truncated() {
        let big = serde_json::json!("x".repeat(500));
        let quoted = json_excerpt(&big, 120);
        assert_eq!(quoted.chars().count(), 123, "got: {quoted}");
        assert!(quoted.ends_with("..."), "got: {quoted}");
        // Short bodies are quoted whole, with no ellipsis.
        assert_eq!(json_excerpt(&serde_json::json!([]), 120), "[]");
    }

    #[test]
    fn an_empty_declaration_list_locks_nothing() {
        let body = serde_json::json!([]);
        assert!(parse_locked_note_ids(&body).unwrap().is_empty());
    }

    #[test]
    fn repeated_locked_note_ids_collapse() {
        let hex = "01".repeat(32);
        let body = serde_json::json!([
            { "locked_note_id": hex.clone() },
            { "locked_note_id": hex },
        ]);
        assert_eq!(parse_locked_note_ids(&body).unwrap().len(), 1);
    }

    #[test]
    fn a_malformed_declaration_list_fails_the_whole_lookup() {
        // Never a partial set: a short locked set understates what is locked,
        // which is the unsafe direction. Callers turn these into the documented
        // degraded fallback instead.
        assert!(parse_locked_note_ids(&serde_json::json!({})).is_err());
        assert!(parse_locked_note_ids(&serde_json::json!("nope")).is_err());
        assert!(parse_locked_note_ids(&serde_json::json!([{ "service_type": "BN" }])).is_err());
        assert!(parse_locked_note_ids(&serde_json::json!([{ "locked_note_id": 7 }])).is_err());
        assert!(
            parse_locked_note_ids(&serde_json::json!([{ "locked_note_id": "zz" }])).is_err()
        );
    }

    #[test]
    fn spendable_notes_drops_locked_ones() {
        let notes: HashMap<NoteId, u64> =
            [(note_id(1), 3000), (note_id(2), 2000), (note_id(3), 100_000)]
                .into_iter()
                .collect();
        let locked: HashSet<NoteId> = [note_id(3)].into_iter().collect();

        let spendable = spendable_notes(&notes, &locked);
        assert_eq!(spendable.len(), 2);
        assert_eq!(spendable_balance(&spendable), 5000);
        assert!(!spendable.contains_key(&note_id(3)));

        // A locked id the key does not own changes nothing: this is a set
        // intersection, and the declaration list is the whole network's.
        let unrelated: HashSet<NoteId> = [note_id(9)].into_iter().collect();
        assert_eq!(spendable_notes(&notes, &unrelated).len(), 3);
        assert_eq!(spendable_balance(&spendable_notes(&notes, &unrelated)), 105_000);
    }

    #[test]
    fn the_locked_note_scenario_is_now_refused() {
        // The exact scenario from the review: spendable {3000, 2000} plus an
        // SDP-locked {100000}, deposit --amount 5000. Reported balance 105000.
        let notes: HashMap<NoteId, u64> =
            [(note_id(1), 3000), (note_id(2), 2000), (note_id(3), 100_000)]
                .into_iter()
                .collect();
        let locked: HashSet<NoteId> = [note_id(3)].into_iter().collect();
        let spendable = spendable_notes(&notes, &locked);

        // No exact note either way, so deposit() takes the mint branch.
        assert_eq!(select_exact_note(&spendable, 5000), None);

        // What the guard used to be handed, and why it waved the mint through:
        // 105000 > 5000. The node would then fund the mint from {3000, 2000}
        // only, land exactly on 5000, emit no change note, and the deposit POST
        // would fail with InsufficientFunds AFTER an irreversible write.
        assert!(ensure_mint_leaves_a_second_note(105_000, 5000).is_ok());

        // What it is handed now.
        let err = ensure_mint_leaves_a_second_note(spendable_balance(&spendable), 5000)
            .unwrap_err()
            .to_string();
        assert!(
            err.contains("spendable balance is exactly the deposit amount 5000"),
            "got: {err}"
        );
    }

    #[test]
    fn a_failed_lookup_falls_back_to_treating_every_note_as_spendable() {
        // The decided degradation: an empty locked set reproduces the old
        // behaviour exactly, so a failed lookup is never worse than the release
        // before this check existed. The user is told via sdpDeclarationsRead
        // and lockedNotesWarning; --strict-locked-check refuses instead.
        let notes: HashMap<NoteId, u64> =
            [(note_id(1), 3000), (note_id(2), 2000), (note_id(3), 100_000)]
                .into_iter()
                .collect();
        let spendable = spendable_notes(&notes, &HashSet::new());
        assert_eq!(spendable, notes);
        assert_eq!(spendable_balance(&spendable), 105_000);
        assert!(ensure_mint_leaves_a_second_note(spendable_balance(&spendable), 5000).is_ok());
    }

    #[test]
    fn the_degraded_notice_is_appended_to_the_error_not_prepended() {
        let err = note_degraded_lock_check(anyhow!("boom"), Some("404 Not Found"))
            .to_string();
        assert!(err.starts_with("boom ("), "got: {err}");
        assert!(err.contains("404 Not Found"), "got: {err}");
        // Nothing is added when the lookup did run.
        assert_eq!(
            note_degraded_lock_check(anyhow!("boom"), None).to_string(),
            "boom"
        );
    }

    #[test]
    fn a_landed_mint_hash_is_appended_to_every_later_failure() {
        // Including the deposit POST's own InsufficientFunds, which is the one
        // outcome the pre-flight cannot rule out when the locked-note lookup
        // fell back. The hash is the user's only handle on an irreversible
        // write, so it is never dropped.
        let hash = serde_json::json!("0xabc");
        let err = note_landed_mint(anyhow!("channel deposit request failed"), Some(&hash))
            .to_string();
        assert!(err.starts_with("channel deposit request failed ("), "got: {err}");
        assert!(err.contains("minted by tx \"0xabc\""), "got: {err}");
        // Nothing is added when no mint happened.
        assert_eq!(
            note_landed_mint(anyhow!("boom"), None).to_string(),
            "boom"
        );
    }

    #[test]
    fn the_post_mint_recheck_now_reasons_over_spendable_notes() {
        // Post-mint set: the fresh exact-value note plus one SDP-locked note.
        // bal.notes.len() is 2 here, which is why the old check could not fire;
        // the spendable set has a single note, which is what proves the deposit
        // POST cannot be funded.
        let notes: HashMap<NoteId, u64> = [(note_id(1), 5000), (note_id(3), 100_000)]
            .into_iter()
            .collect();
        let locked: HashSet<NoteId> = [note_id(3)].into_iter().collect();
        assert_eq!(notes.len(), 2);
        assert_eq!(spendable_notes(&notes, &locked).len(), 1);
    }

    // -- the reported lock-check facts ----------------------------------------

    #[test]
    fn the_json_reports_what_was_done_not_what_is_guaranteed() {
        // A populated answer: the node demonstrably had SDP state.
        let mut out = serde_json::json!({ "ok": true });
        let locked: HashSet<NoteId> = [note_id(1), note_id(2)].into_iter().collect();
        annotate_lock_check(&mut out, &locked, None);
        assert_eq!(out["sdpDeclarationsRead"], serde_json::json!(true));
        assert_eq!(out["sdpLockedNotesSeen"], serde_json::json!(2));
        assert!(out.get("lockedNotesWarning").is_none());

        // An empty 200. The lookup ran, and that is ALL the object may say: an
        // empty list is what upstream answers from a missing tip state too, so
        // the count is what lets a caller tell the two apart. The old single
        // "lockedNotesChecked":true could not express this at all.
        let mut out = serde_json::json!({ "ok": true });
        annotate_lock_check(&mut out, &HashSet::new(), None);
        assert_eq!(out["sdpDeclarationsRead"], serde_json::json!(true));
        assert_eq!(out["sdpLockedNotesSeen"], serde_json::json!(0));

        // A failed lookup: no count at all, not a zero count.
        let mut out = serde_json::json!({ "ok": true });
        annotate_lock_check(&mut out, &HashSet::new(), Some("404 Not Found"));
        assert_eq!(out["sdpDeclarationsRead"], serde_json::json!(false));
        assert_eq!(out["sdpLockedNotesSeen"], serde_json::Value::Null);
        assert_eq!(out["lockedNotesWarning"], serde_json::json!("404 Not Found"));

        // The retired name is gone rather than kept as an alias: an honest
        // field and a dishonest one side by side is just the dishonest one.
        assert!(out.get("lockedNotesChecked").is_none());
    }

    // -- MEDUSA_L1_POLL_SECS clamp -------------------------------------------

    #[test]
    fn the_poll_deadline_cannot_be_pushed_into_an_instant_overflow() {
        assert_eq!(clamp_poll_secs(None), DEFAULT_POLL_SECS);
        assert_eq!(clamp_poll_secs(Some("nonsense")), DEFAULT_POLL_SECS);
        assert_eq!(clamp_poll_secs(Some("30")), 30);
        assert_eq!(clamp_poll_secs(Some(" 30 ")), 30);
        // The reachable panic: Instant::now() + Duration::from_secs(..) panics on
        // overflow, and that line runs AFTER the irreversible mint, so an absurd
        // env value would replace the JSON error carrying the mint hash with a
        // bare Rust panic. Clamped, the addition cannot overflow.
        assert_eq!(clamp_poll_secs(Some(&u64::MAX.to_string())), MAX_POLL_SECS);
        assert_eq!(clamp_poll_secs(Some("999999999999999999")), MAX_POLL_SECS);
        assert!(
            Instant::now()
                .checked_add(Duration::from_secs(clamp_poll_secs(Some(
                    &u64::MAX.to_string()
                ))))
                .is_some()
        );
    }

    // -- the deposit WIRING, not just the helpers ----------------------------
    //
    // Everything above pins pure helpers. None of it can see whether
    // deposit_with_locked_set actually FEEDS them the spendable set. The review
    // proved that gap by substitution: replacing
    //     let mut spendable = spendable_notes(&bal.notes, locked);
    // with `bal.notes.clone()`, and
    //     spendable = spendable_notes(&fresh.notes, locked);
    // with `fresh.notes.clone()`, deletes the entire money-path fix while every
    // helper and every helper test stays untouched, and the suite stayed green.
    //
    // The two tests below drive deposit_with_locked_set itself against a
    // scripted loopback HTTP server and assert on WHAT IT SENT, so either
    // substitution turns into a failure: the first one starts sending the mint
    // POST it must refuse to send, the second one starts sending the
    // /channel/deposit POST it must refuse to send.
    //
    // They speak only to 127.0.0.1 and never to a node, and they never touch
    // MEDUSA_L1_ALLOW_WRITES: deposit_with_locked_set is the inner half, below
    // the write gate, which is exactly what lets a write path be exercised when
    // no write exists. The gate over the outer deposit() is pinned separately
    // above, including with --strict-locked-check set.

    /// A scripted HTTP/1.1 responder on 127.0.0.1. Answers request N with entry
    /// N of the script and records "METHOD PATH" for every request it saw, so a
    /// test can assert on the requests that were NOT made. Anything past the end
    /// of the script gets a 500, so a call site that sends more than the test
    /// expects fails loudly instead of hanging. Hand-rolled over std::net on
    /// purpose: no new dependency, and no extra tokio feature, for ~60 lines.
    struct StubNode {
        port: u16,
        log: std::sync::Arc<std::sync::Mutex<Vec<String>>>,
        stop: std::sync::Arc<std::sync::atomic::AtomicBool>,
        server: Option<std::thread::JoinHandle<()>>,
    }

    impl StubNode {
        fn start(script: Vec<(u16, String)>) -> Self {
            let listener =
                std::net::TcpListener::bind("127.0.0.1:0").expect("stub node binds a loopback port");
            let port = listener.local_addr().expect("bound address").port();
            listener
                .set_nonblocking(true)
                .expect("stub node listener goes non-blocking");

            let log = std::sync::Arc::new(std::sync::Mutex::new(Vec::new()));
            let stop = std::sync::Arc::new(std::sync::atomic::AtomicBool::new(false));
            let (thread_log, thread_stop) = (std::sync::Arc::clone(&log), std::sync::Arc::clone(&stop));

            let server = std::thread::spawn(move || {
                let mut next = 0usize;
                while !thread_stop.load(std::sync::atomic::Ordering::Relaxed) {
                    match listener.accept() {
                        Ok((mut sock, _)) => {
                            sock.set_nonblocking(false).expect("stub stream blocks");
                            sock.set_read_timeout(Some(Duration::from_secs(10))).ok();
                            let head = Self::read_head(&mut sock);
                            let request_line = head.lines().next().unwrap_or_default();
                            let mut parts = request_line.split_whitespace();
                            let method = parts.next().unwrap_or("?").to_owned();
                            let path = parts.next().unwrap_or("?").to_owned();
                            thread_log
                                .lock()
                                .expect("stub log is not poisoned")
                                .push(format!("{method} {path}"));

                            let (status, body) = script.get(next).cloned().unwrap_or_else(|| {
                                (500, format!("\"unscripted request: {method} {path}\""))
                            });
                            next += 1;
                            Self::respond(&mut sock, status, &body);
                        }
                        Err(err) if err.kind() == std::io::ErrorKind::WouldBlock => {
                            std::thread::sleep(Duration::from_millis(2));
                        }
                        Err(_) => break,
                    }
                }
            });

            Self {
                port,
                log,
                stop,
                server: Some(server),
            }
        }

        /// Read the request head, then drain any Content-Length body, so the
        /// client is never reset mid-POST and reports the real response.
        fn read_head(sock: &mut std::net::TcpStream) -> String {
            use std::io::Read as _;
            let mut buf: Vec<u8> = Vec::new();
            let mut chunk = [0u8; 1024];
            loop {
                if let Some(end) = buf.windows(4).position(|w| w == b"\r\n\r\n") {
                    let head = String::from_utf8_lossy(&buf[..end]).into_owned();
                    let want: usize = head
                        .lines()
                        .filter_map(|line| line.split_once(':'))
                        .find(|(name, _)| name.eq_ignore_ascii_case("content-length"))
                        .and_then(|(_, value)| value.trim().parse().ok())
                        .unwrap_or(0);
                    if buf.len() - (end + 4) >= want {
                        return head;
                    }
                }
                match sock.read(&mut chunk) {
                    Ok(0) | Err(_) => return String::from_utf8_lossy(&buf).into_owned(),
                    Ok(read) => buf.extend_from_slice(&chunk[..read]),
                }
            }
        }

        fn respond(sock: &mut std::net::TcpStream, status: u16, body: &str) {
            use std::io::Write as _;
            let reason = if status == 200 {
                "OK"
            } else {
                "Internal Server Error"
            };
            let response = format!(
                "HTTP/1.1 {status} {reason}\r\nContent-Type: application/json\r\n\
                 Content-Length: {}\r\nConnection: close\r\n\r\n{body}",
                body.len()
            );
            let _ = sock.write_all(response.as_bytes());
            let _ = sock.flush();
            let _ = sock.shutdown(std::net::Shutdown::Both);
        }

        fn base(&self) -> Url {
            Url::parse(&format!("http://127.0.0.1:{}/", self.port)).expect("valid loopback base")
        }

        fn requests(&self) -> Vec<String> {
            self.log.lock().expect("stub log is not poisoned").clone()
        }
    }

    impl Drop for StubNode {
        fn drop(&mut self) {
            self.stop
                .store(true, std::sync::atomic::Ordering::Relaxed);
            if let Some(server) = self.server.take() {
                let _ = server.join();
            }
        }
    }

    /// A GET /wallet/:pk/balance body, built from the REAL response type so the
    /// wire shape is the node's and not this test's idea of it.
    fn balance_body(address: ZkPublicKey, notes: &[(NoteId, u64)]) -> String {
        let body = WalletBalanceResponseBody {
            tip: HeaderId::from([0u8; 32]),
            balance: notes.iter().map(|(_, value)| *value).sum(),
            notes: notes.iter().copied().collect(),
            address,
        };
        serde_json::to_string(&body).expect("the pinned balance body serializes")
    }

    /// POST /wallet/transactions/transfer-funds answers `{"hash": <32 bytes>}`;
    /// TxHash's human-readable serde form is plain hex (upstream
    /// core/src/utils/mod.rs, serde_bytes_newtype).
    const STUB_MINT_HASH: &str = "abababababababababababababababababababababababababababababababab";

    fn assert_is_balance_get(request: &str) {
        assert!(
            request.starts_with("GET /wallet/") && request.ends_with("/balance"),
            "expected the read-only balance GET, got: {request}"
        );
    }

    #[tokio::test]
    async fn the_preflight_guard_is_wired_to_the_spendable_set_so_nothing_is_minted() {
        // The review's scenario, driven through the real call site: {3000, 2000}
        // spendable plus an SDP-locked {100000}, deposit --amount 5000. The node
        // reports a balance of 105000 and three notes.
        let funding_hex = "11".repeat(32);
        let funding = parse_zk_pk(&funding_hex).expect("valid funding pk");
        let notes = [
            (note_id(1), 3000u64),
            (note_id(2), 2000),
            (note_id(3), 100_000),
        ];
        let locked: HashSet<NoteId> = [note_id(3)].into_iter().collect();
        // Only the balance GET is scripted. A mint POST therefore gets a 500 and
        // a recorded request line, both of which this test fails on.
        let stub = StubNode::start(vec![(200, balance_body(funding, &notes))]);
        let client = http_client().expect("client builds");

        let err = deposit_with_locked_set(
            &client,
            &stub.base(),
            &[7u8; 32].to_base58(),
            5000,
            DEFAULT_CHANNEL_HEX,
            &funding_hex,
            &locked,
        )
        .await
        .unwrap_err()
        .to_string();

        assert!(
            err.contains("spendable balance is exactly the deposit amount 5000"),
            "got: {err}"
        );
        // The load-bearing assertion: ONE request, read-only, and no self
        // transfer. Hand the guard bal.notes instead of the spendable set and
        // 105000 > 5000 waves it through, so a POST to
        // /wallet/transactions/transfer-funds appears here: an irreversible mint
        // into a deposit that can never be funded.
        let requests = stub.requests();
        assert_eq!(requests.len(), 1, "extra requests: {requests:?}");
        assert_is_balance_get(&requests[0]);
    }

    #[tokio::test]
    async fn the_post_mint_recheck_is_wired_to_the_refreshed_spendable_set() {
        // Pre-flight: {3000, 2000} spendable plus a locked {100000}, --amount
        // 4000. 5000 > 4000, so the mint is correctly allowed and lands. The
        // post-mint balance is then the fresh 4000 note plus the same locked
        // note: two notes reported, ONE spendable, which proves the deposit POST
        // cannot be funded and must be refused with the mint hash in hand.
        //
        // Takes ~3s: the poll sleeps before its first query, and that interval is
        // production behaviour rather than something to weaken for a test.
        let funding_hex = "22".repeat(32);
        let funding = parse_zk_pk(&funding_hex).expect("valid funding pk");
        let locked: HashSet<NoteId> = [note_id(3)].into_iter().collect();
        let stub = StubNode::start(vec![
            (
                200,
                balance_body(
                    funding,
                    &[
                        (note_id(1), 3000u64),
                        (note_id(2), 2000),
                        (note_id(3), 100_000),
                    ],
                ),
            ),
            (200, serde_json::json!({ "hash": STUB_MINT_HASH }).to_string()),
            (
                200,
                balance_body(funding, &[(note_id(4), 4000u64), (note_id(3), 100_000)]),
            ),
        ]);
        let client = http_client().expect("client builds");

        let err = deposit_with_locked_set(
            &client,
            &stub.base(),
            &[7u8; 32].to_base58(),
            4000,
            DEFAULT_CHANNEL_HEX,
            &funding_hex,
            &locked,
        )
        .await
        .unwrap_err()
        .to_string();

        assert!(
            err.contains("is now the only SPENDABLE note on this key"),
            "got: {err}"
        );
        assert!(err.contains(STUB_MINT_HASH), "mint hash dropped: {err}");

        // Refresh the poll's note set from fresh.notes instead of the spendable
        // set and the re-check sees two notes, so this walks straight into the
        // /channel/deposit POST that cannot be funded. It must not appear here.
        let requests = stub.requests();
        assert_eq!(requests.len(), 3, "unexpected requests: {requests:?}");
        assert_is_balance_get(&requests[0]);
        assert_eq!(requests[1], "POST /wallet/transactions/transfer-funds");
        assert_is_balance_get(&requests[2]);
        assert!(
            !requests.iter().any(|r| r.contains("/channel/deposit")),
            "the deposit POST was sent after the re-check should have bailed: {requests:?}"
        );
    }

    // -- 404 "address could not be found" mapping ----------------------------

    #[test]
    fn maps_the_node_not_found_answer() {
        // What CommonHttpClient produces for the node's 404 body.
        let err = "Unexpected response [404 Not Found]: The requested address \
                   could not be found in the wallet";
        assert_eq!(map_wallet_balance_error(err), Some(NOT_REGISTERED_ERROR));
    }

    #[test]
    fn passes_through_other_errors() {
        assert_eq!(map_wallet_balance_error("Internal server error: boom"), None);
        assert_eq!(map_wallet_balance_error("connection reset by peer"), None);
    }

    // -- zone account (base58) parsing ---------------------------------------

    #[test]
    fn zone_account_roundtrips_32_bytes() {
        let bytes = [7u8; 32];
        let encoded = bytes.to_base58();
        assert_eq!(parse_zone_account(&encoded).unwrap(), bytes);
        // The Public/ prefix form is accepted too.
        assert_eq!(parse_zone_account(&format!("Public/{encoded}")).unwrap(), bytes);
    }

    #[test]
    fn zone_account_rejects_wrong_length() {
        let short = [7u8; 31].to_base58();
        let err = parse_zone_account(&short).unwrap_err().to_string();
        assert!(err.contains("expected 32 bytes"), "got: {err}");
    }

    #[test]
    fn zone_account_rejects_bad_base58_and_private() {
        assert!(parse_zone_account("not-base58-0OIl").is_err());
        assert!(parse_zone_account("Private/abc").is_err());
    }

    // -- exact-note selection -------------------------------------------------

    #[test]
    fn selects_a_note_of_exactly_the_amount() {
        let notes: HashMap<u32, u64> = [(1, 5), (2, 7), (3, 7)].into_iter().collect();
        let picked = select_exact_note(&notes, 7).unwrap();
        assert!(picked == 2 || picked == 3);
        assert_eq!(select_exact_note(&notes, 5), Some(1));
    }

    #[test]
    fn no_exact_note_means_none() {
        let notes: HashMap<u32, u64> = [(1, 5), (2, 8)].into_iter().collect();
        assert_eq!(select_exact_note(&notes, 7), None); // 5+8 covers 7, but no exact note
        assert_eq!(select_exact_note(&HashMap::<u32, u64>::new(), 1), None);
    }

    // -- zk pk hex parsing ----------------------------------------------------

    #[test]
    fn zk_pk_accepts_64_hex_chars() {
        let hex64 = "11".repeat(32);
        assert!(parse_zk_pk(&hex64).is_ok());
        assert!(parse_zk_pk(&format!("0x{hex64}")).is_ok());
    }

    #[test]
    fn zk_pk_rejects_wrong_length_or_nonhex() {
        assert!(parse_zk_pk("1234").is_err());
        assert!(parse_zk_pk(&"zz".repeat(32)).is_err());
    }
}
