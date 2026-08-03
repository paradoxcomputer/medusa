#include "WalletPlugin.h"

#include <QSettings>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QTimer>
#include <QRegularExpression>
#include <QProcessEnvironment>
#include <QUrl>
#include <QRandomGenerator>
#include <QCryptographicHash>
#include <QSet>

#include <algorithm>
#include <utility>   // std::as_const
#include <cstring>
#include <cerrno>
#include <dlfcn.h>   // dladdr: locate the module's own install dir (bundled binaries)
#include <unistd.h>       // access(), geteuid(), close()
#include <fcntl.h>        // O_NONBLOCK
#include <poll.h>         // poll(): the connect() timeout for the port probe
#include <sys/socket.h>   // the port probe that replaced the shell-out (see tcpPortOpen)
#include <netinet/in.h>
#include <arpa/inet.h>

static constexpr const char* kCliPathKey = "medusa-wallet/cliPath";
static constexpr const char* kNetworkKey = "medusa-wallet/network";   // active zone id
static constexpr const char* kZonesKey   = "medusa-wallet/zones";     // user-added remote zones (JSON)

// Operator endpoints for the built-in "Paradox Computer" zones, resolved at runtime:
//   • "Paradox Computer · Tor"      sequencer .onion: env MEDUSA_SEQ_ONION     | ~/.config/medusa-sequencer.onion
//   • "Paradox Computer · clearnet" sequencer URL:    env MEDUSA_CLEARNET_URL  | ~/.config/medusa-clearnet.url
// The .onion is never baked in - when unset, the Tor zone is simply unavailable.
// The clearnet sequencer IS baked in (below): it is public infrastructure, already published in
// examples/tip-jar (preferredZone) and sdk/medusa-connect.test.js, and without it the built-in
// clearnet zone was selectable but resolved to an empty URL, which wrote "sequencer_addr":"" into
// wallet_config.json and broke every subsequent wallet call with
//   Failed to deserialize wallet config ... relative URL without a base: ""
static QString endpointFromConfig(const char* envVar, const QString& cfgFile)
{
    const QString env = qEnvironmentVariable(envVar).trimmed();
    if (!env.isEmpty()) return env;
    QFile f(QDir::homePath() + QStringLiteral("/.config/") + cfgFile);
    if (f.open(QIODevice::ReadOnly)) return QString::fromUtf8(f.readAll()).trimmed();
    return QString();
}
// Default clearnet sequencer, overridden by env / ~/.config as above.
static constexpr const char* kDefaultClearnetUrl = "https://seq-testnet.paradox.computer/";
static QString clearnetUrl()
{
    const QString u = endpointFromConfig("MEDUSA_CLEARNET_URL", QStringLiteral("medusa-clearnet.url"));
    return u.isEmpty() ? QString::fromLatin1(kDefaultClearnetUrl) : u;
}
// The official Logos public testnet (logos-co). Runs LEZ v0.2.0, the same engine this build is
// compiled against, so the program ImageIDs match and the wallet can transact there. Kept as a
// separate preset zone: accounts/keys are shared across zones but balances are per-zone.
static constexpr const char* kDefaultLogosTestnetUrl = "https://testnet.lez.logos.co/";
static QString logosTestnetUrl()
{
    const QString u = endpointFromConfig("MEDUSA_LOGOS_TESTNET_URL",
                                         QStringLiteral("medusa-logos-testnet.url"));
    return u.isEmpty() ? QString::fromLatin1(kDefaultLogosTestnetUrl) : u;
}
// The deployed `medusa_faucet` LEZ program (wallet/faucet/guest), as ONE constant for every
// zone. That is not an assumption about our zones, it is what a LEZ program id IS: `Program::new`
// derives it with `compute_image_id` over the guest ELF alone (lee/state_machine/src/program.rs),
// with no channel, deployer or endpoint mixed in. The same 437 884-byte binary therefore deploys
// under this id everywhere, which was confirmed empirically when it was deployed to BOTH operator
// zones on 2026-07-31 and each sequencer accepted it under exactly this id:
//   Paradox   https://seq-testnet.paradox.computer/  block 975
//   Logos     https://testnet.lez.logos.co/          block 44810
// A per-zone table here would be a table with one distinct value in it, and would invite the
// belief that a redeploy could change one entry without changing the program.
//
// It is also the wallet's trust anchor for the local copy of the guest: `medusa-faucet-client`
// recomputes the ImageID from whatever .bin it is handed, so a planted .bin would silently point
// the claim at a DIFFERENT program. faucetPreflight() runs `info --bin` (offline: no wallet, no
// network) and refuses unless the answer is exactly this string.
static constexpr const char* kFaucetProgramId =
    "3c56ad0e1d2be3b57582d91187892daa8be2b63d300c2c9d9df318a494dcb885";

// ── WHICH ZONES HAVE THE TOKEN FAUCET, AND WHAT IT DISPENSES ON EACH ──────────────────────────
// The program ID above is one constant for every zone (see why, above). The TOKENS are not: a
// token definition is an account on one specific chain, and its treasury is a PDA holding on that
// same chain. So the per-zone facts live here, in ONE table keyed by zone id, and nowhere else.
// Adding a zone to the faucet is adding a row; there is no second list to keep in step.
//
// THIS TABLE *IS* THE CAPABILITY. A zone with no row has no token faucet, and the token half is
// never attempted there, which is the rule stated per zone class:
//   • "devnet"   - a local sandbox chain this wallet starts itself. Nothing is deployed on it.
//   • "diaphani" - the Tor-fronted operator sequencer, not yet carrying the faucet.
//   • every USER-ADDED zone - somebody else's sequencer. The program is not deployed there, and
//     even if an operator deployed it, THESE definitions are accounts on OUR chains and do not
//     exist on theirs. A user zone can therefore never acquire the capability by configuration.
// On all of those the faucet claims native LEZ through pinata and nothing else, which is exactly
// the behaviour the owner specified.
//
// The lookup below is by zone id against THIS table only. It never reads the stored zone record,
// so a userZones entry planted straight into QSettings (an ordinary user-writable INI - see
// cliPath()) cannot grant itself a token faucet by carrying a "tokenFaucet":true key. getZones()
// and zoneObj() PUBLISH the flag they compute here; they never echo a stored one back.
//
// The treasury is the faucet program's own PDA for a definition, derived by medusa_faucet_shared
// as AccountId::for_public_pda(program_id, SHA-256("MEDUSA/treasury/" || definition_id)).
// medusa-faucet-client re-derives it on every call and the module never sends it - re-deriving a
// PDA in C++ is precisely the drift that shared crate exists to prevent. It is recorded here
// because it is the account an operator funds and the account faucetStatus() has to be able to
// name, and because a row that gives only the definition is half a fact.
//
// THESE ADDRESSES BELONG TO THE PROGRAM ID ABOVE. A treasury is a PDA of the faucet program,
// so changing the guest changes its ImageID, which changes the program id, which moves every
// treasury below. The two are one fact and must be edited together: wallet/faucet/shared's
// `shipped_table_matches_the_program_id` test derives these six from kFaucetProgramId and fails
// if anyone updates one without the other.
//
// Re-derived 2026-08-03 for the guest build that rejects a claim naming the same token twice
// (the previous build let one claim drain n * claim_amount from a treasury against a single
// cooldown). DEPLOYED AND FUNDED the same day on both zones: program at paradox-clearnet block
// 4637 and on logos-testnet, every treasury below initialized and minted to 1000000 / 5000000 /
// 20000000, and a live claim on logos-testnet (block 48541) dispensed 295 / 331 / 195, each
// inside the advertised 10..500 band. The addresses were derived offline first and the chain
// then reported the same ones back from init-treasury.
struct FaucetToken {
    const char* ticker;      // display name, e.g. "GOLD"
    const char* definition;  // token definition account id (base58)
    const char* treasury;    // the faucet program's treasury PDA for that definition (base58)
};
struct FaucetZoneRow {
    const char* zoneId;      // must match a zone id getZones() publishes
    FaucetToken tokens[3];
};
static constexpr FaucetZoneRow kFaucetZones[] = {
    // Paradox Computer clearnet - https://seq-testnet.paradox.computer/
    { "paradox-clearnet", {
        { "GOLD", "5YEhWdY2edtRFkCruXjtnFH5F62VkCiCxXmNAvHuVkEY",
                  "Fed1dmPD9aNNyMQrPkSbLznyqVBCJ7Q25bbzQV1rxGnL" },
        { "SILV", "HUDERmRqyX6swMnuk9FT5vmqNbcdLNbVxDRtLEdzsMXk",
                  "CWd7PbmCfebZ9ziK1bvmjj8RiDi5XCXW78H4qYdWCTMP" },
        { "BRNZ", "3zS3bGdToZcqPU9jBZC8c1aK9MQvpekse9EJ52nD1wiM",
                  "7HvJ5wqSL3NXf1CmGpoDDLj8nk42S39wULuM2TAtzmXx" } } },
    // Logos public testnet - https://testnet.lez.logos.co/
    { "logos-testnet", {
        { "GOLD", "7ZZGE941fzSGCAfxxdkPWQszSspBhZEcjHUkLqWrrnz6",
                  "D8ScxGNvPLtCeLbPphWSKeei36Lj5tbmFxGGgxr66jsV" },
        { "SILV", "CfuvpaUhbxEzWd6ZtLDiKWVg5DZLiYj14Q8HgtDUwuS6",
                  "DZQnfiJBz9YZkzkmMDFhHjgk2zZEKLM5oSrfX8SmXXdB" },
        { "BRNZ", "EEMUsdWL1WxrQBi1SmNFUKVcMUjgVcky12NRv2BjBuxp",
                  "CZxne337Uh7ezNVZcNASN5yN9G7uEkCm1MLpHHdVRPD" } } },
};

// The row for a zone, or nullptr when that zone has no token faucet. The ONE place the
// capability is decided.
static const FaucetZoneRow* faucetZoneRow(const QString& zoneId)
{
    const QString z = zoneId.trimmed();
    for (const FaucetZoneRow& row : kFaucetZones)
        if (z == QLatin1String(row.zoneId))
            return &row;
    return nullptr;
}

// Bundled-Tor SOCKS port (distinct from a system Tor on 9050, so the two never clash).
static constexpr int kTorSocksPort = 9250;
// Bundled-Tor control port (for the onion-connection-stage monitor).
static constexpr int kTorControlPort = 9251;
// …unless the LAUNCHER's environment names different ones. This is NOT a crack in invariant B:
// invariant B is about which BINARY runs, and a loopback port number names no program. The
// environment belongs to whoever started this process (Basecamp, a developer, the test suite) and
// a co-resident module cannot write it - one that could would already own MEDUSA_WALLET_CLI, i.e.
// outright code execution, so this adds no capability to that attacker. Same knob idiom as
// MEDUSA_IDLE_LOCK_MS. It buys two things the bare constants could not: a second wallet instance
// (or a box where 9250 is already taken by something else) brings up its OWN Tor instead of
// silently adopting a stranger's, and the suite can exercise the real bring-up/teardown on a
// developer box that is already running the wallet's Tor on the default port.
static int torPortFromEnv(const char* envVar, int fallback)
{
    bool ok = false;
    const int p = qEnvironmentVariableIntValue(envVar, &ok);
    return (ok && p >= 1024 && p <= 65535) ? p : fallback;
}
int WalletPlugin::torSocksPort()   { return torPortFromEnv("MEDUSA_TOR_SOCKS_PORT",   kTorSocksPort); }
int WalletPlugin::torControlPort() { return torPortFromEnv("MEDUSA_TOR_CONTROL_PORT", kTorControlPort); }
// A just-spawned standalone sequencer needs a few seconds before checkHealth answers; only
// after this window does a silent non-answer count as a reportable "unhealthy" failure.
static constexpr qint64 kSeqLaunchGraceMs = 15000;
// seqMode/seqUrl are STORED AND REPORTED but influence nothing: applySequencer() derives the
// endpoint from the active zone alone, and neither key is read on any execution path. They are
// kept so an existing UI's settings round-trip still works. (medusa-wallet/seqPort was declared
// here and never read at all; it is gone, so the enumeration of "settings that matter" is real.)
static constexpr const char* kSeqModeKey = "medusa-wallet/seqMode";   // "local" | "hosted" (inert)
static constexpr const char* kSeqUrlKey  = "medusa-wallet/seqUrl";    // hosted sequencer URL (inert)
// Read in getSequencerConfig() and NOWHERE else: see seqPath() for why this stopped being the
// thing the module executes.
static constexpr const char* kSeqPathKey = "medusa-wallet/seqPath";   // sequencer binary (disowned)

// The bundled standalone-sequencer config - the rc5-shaped genesis with a templated home
// and a dead bedrock node_url (standalone mocks bedrock, so no L1 is ever contacted).
// __SEQ_HOME__ is replaced with the per-wallet sequencer home at first launch; the rc5
// sequencer builds its base state from the hardcoded testnet_initial_state (pinata + debug
// accounts) and applies this `genesis` array on top: fund the system bridge account, plus a
// couple of supply accounts. (The diaphani L1 zone rewrites node_url in writeSeqConfig.)
static const char* kSeqConfigTemplate =
R"SEQ({"home":"__SEQ_HOME__","max_num_tx_in_block":20,"max_block_size":"1 MiB","mempool_max_size":1000,"block_create_timeout":"3s","retry_pending_blocks_timeout":"5s","bedrock_config":{"channel_id":"0202020202020202020202020202020202020202020202020202020202020202","node_url":"http://127.0.0.1:1"},"signing_key":[37,37,37,37,37,37,37,37,37,37,37,37,37,37,37,37,37,37,37,37,37,37,37,37,37,37,37,37,37,37,37,37],"genesis":[{"supply_bridge_account":{"balance":1000000000}},{"supply_account":{"account_id":"CbgR6tj5kWx5oziiFptM7jMvrQeYY3Mzaao6ciuhSr2r","balance":100000000}},{"supply_account":{"account_id":"2RHZhw9h534Zr3eq2RGhQete2Hh667foECzXPmSkGni2","balance":100000000}}]})SEQ";

// ── Helpers ───────────────────────────────────────────────────────────────────

QString WalletPlugin::errorJson(const QString& msg, const QString& reason)
{
    QJsonObject o;
    o[QStringLiteral("error")] = msg;
    if (!reason.isEmpty())
        o[QStringLiteral("reason")] = reason;
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

QString WalletPlugin::okJson()
{
    QJsonObject o;
    o[QStringLiteral("ok")] = true;
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

// ── Proof-of-user gate ────────────────────────────────────────────────────────

bool WalletPlugin::constantTimeEquals(const QString& a, const QString& b)
{
    // Hash first, then compare fixed-width digests: a byte-wise compare of the raw strings
    // would return early on the first mismatch and would branch on length, which is a usable
    // oracle for a caller that can invoke a gated verb in a loop. SHA-256 of a wrong guess
    // tells the caller nothing about the right one, and both digests are always 32 bytes.
    const QByteArray ha = QCryptographicHash::hash(a.toUtf8(), QCryptographicHash::Sha256);
    const QByteArray hb = QCryptographicHash::hash(b.toUtf8(), QCryptographicHash::Sha256);
    quint8 diff = static_cast<quint8>(ha.size() ^ hb.size());
    for (int i = 0; i < ha.size() && i < hb.size(); ++i)
        diff = static_cast<quint8>(diff | (static_cast<quint8>(ha[i]) ^ static_cast<quint8>(hb[i])));
    return diff == 0;
}

// ── INVARIANT C: THE GATE IS A FUNCTION OF THE SECRET, AND OF NOTHING ELSE ────────────────────
//
// Two lines, in this order, and there is nothing else in this function on purpose:
//     a session exists  ->  the presented password equals it, compared in constant time.
//
// What used to be here was a short-circuit `if (storageIsPlaintext()) return true;`, placed
// BEFORE both checks, and it was the round-3 hole. The argument for it was sound about a store
// that really is plaintext (its keys are in a file any process at this uid can read, so the gate
// has no secret to defend there) and wrong about everything else: storageIsPlaintext() is not a
// fact about protection, it is a 256-byte guess about a file a co-resident attacker fully
// controls. Re-wrapping the still-encrypted store so its "kdf"/"ct" markers fall past byte 256 -
// or catching the instant the CLI truncates it mid-write, or making it unreadable - flipped that
// guess to true over a store that was still fully encrypted. authorize() then returned true
// WITHOUT comparing anything, the verb called runWalletCommand, and runWalletCommand piped the
// real in-memory session password to the CLI, which decrypted the real ciphertext and handed back
// the seed. Observed: an attacker presented "i-do-not-know-the-password" and got the 24 words.
//
// The general rule this fixes, which is the one that lost rounds 1 to 3 in three different
// disguises: A CHECK MUST NOT BE KEYED ON STATE THE ATTACKER CAN WRITE. QSettings was that state
// for cliPath and seqPath; m_password.isEmpty() was that state for round 2's recovery gates;
// storage.json's first 256 bytes was that state here. No file predicate can be repaired into
// safety, because even a whole-file parse can be swapped between the check and the CLI spawn - so
// the file is not consulted at all.
//
// The plaintext user is NOT stranded by this, and is not served by the gate either. See
// authRefusal() below for the route they get instead (migration, which needs no session), and
// the class-by-class walk in the WalletPlugin.h comment on this function.
bool WalletPlugin::authorize(const QString& password)
{
    // Locked: there is no established password to compare against, so nothing can be proved.
    // Fail closed - never treat "no password set" as "no password needed".
    if (m_password.isEmpty())
        return false;
    if (!constantTimeEquals(m_password, password))
        return false;
    // Passing the gate IS the definition of user activity here, and it is the only honest one
    // available: the module has no view of the UI, and the UI polls listAccounts every 10s, so
    // treating any CLI call as activity would mean the idle lock never fired at all.
    touchActivity();
    return true;
}

// Can a password be proved against the store on disk at all? Note what this is NOT: it is not
// "is a session live". Round 2 gated the recovery verbs on `!m_password.isEmpty() &&
// !storageIsPlaintext()`, and the first half of that is flipped by clearSessionPassword(), which
// is ungated by design - so an attacker turned a gated verb into an ungated one in one call. This
// is a fact about the filesystem instead, which no caller can flip.
bool WalletPlugin::storeCanProvePassword()
{
    return storageExists() && !storageIsPlaintext();
}

// The message only. authorize() has already decided, on the secret alone; nothing read here can
// change that verdict, which is why consulting the store is safe HERE and was not safe there.
// The order matters for honesty, not for enforcement: a live session means the store was
// encrypted when it was opened, so "unauthorized" is the true reason even if the file on disk has
// since been made to look like something else.
QString WalletPlugin::authRefusal() const
{
    if (!m_password.isEmpty())
        return errorJson(QStringLiteral("wallet password required for this operation"),
                         QStringLiteral("unauthorized"));
    // No session, and the store on disk carries no crypto envelope. There is no password that
    // could pass this gate (unlock() refuses to hand out a session for a store it cannot verify a
    // password against), so the refusal has to carry the ROUTE OUT with it: encryptPlaintextWallet
    // needs no session, keeps the accounts, copies the old store aside, and afterwards the normal
    // unlock works and every verb below is reachable again. The UI turns this reason into exactly
    // that instruction (Security & Backup -> set a password).
    if (storageIsPlaintext())
        return errorJson(QStringLiteral("this wallet's storage is not encrypted, so no password "
                                        "can prove who is asking - set a password on it (Security "
                                        "& Backup) to use this operation; your accounts are kept"),
                         QStringLiteral("unencrypted"));
    return errorJson(QStringLiteral("wallet is locked - unlock first"),
                     QStringLiteral("locked"));
}

// "token" → the token program; anything else → the native authenticated-transfer
// program (the asset the wallet's balance/faucet/Send already operate on).
QString WalletPlugin::assetProgram(const QString& asset)
{
    return (asset.trimmed().toLower() == QStringLiteral("token"))
         ? QStringLiteral("token")
         : QStringLiteral("auth-transfer");
}

// ── The two spec parsers ──────────────────────────────────────────────────────
// Why these exist at all: the Logos bridge invokes 0-5 arguments and silently drops anything
// wider (qt_provider_object.cpp:20-34 - the full account is at the top of the header). Each
// parser collapses arguments that were always one concept, which is what keeps every spend
// verb at 4 and leaves the session password on the end where callGated() puts it.

bool WalletPlugin::parseValueSpec(const QString& spec, QString* asset, QString* definitionId,
                                  QString* amount, QString* error)
{
    const auto fail = [&](const QString& msg) {
        if (error) *error = msg;
        return false;
    };
    if (asset)        asset->clear();
    if (definitionId) definitionId->clear();
    if (amount)       amount->clear();
    if (error)        error->clear();

    const QString s = spec.trimmed();
    if (s.isEmpty())
        return fail(QStringLiteral("amount is required"));

    QString a = QStringLiteral("native"), def, amt;
    if (s.startsWith(QLatin1Char('{'))) {
        const QJsonDocument doc = QJsonDocument::fromJson(s.toUtf8());
        if (!doc.isObject())
            return fail(QStringLiteral("value must be an amount (\"12\") or a JSON object "
                                       "{\"asset\":\"token\",\"definitionId\":…,\"amount\":…}"));
        const QJsonObject o = doc.object();
        // A JSON number is as legitimate a way to write 12 as the string "12"; take either.
        // Via QVariant, which never ROUNDS: a fixed-precision conversion would turn 12.5 into
        // "13", i.e. quietly move an amount the caller did not ask to move. Anything that does
        // not survive as a plain number fails the check below and is reported, not spent.
        const QJsonValue amtV = o.value(QStringLiteral("amount"));
        amt = amtV.isDouble() ? amtV.toVariant().toString().trimmed()
                              : amtV.toString().trimmed();
        def = o.value(QStringLiteral("definitionId")).toString().trimmed();
        const QString rawAsset = o.value(QStringLiteral("asset")).toString().trimmed();
        if (!rawAsset.isEmpty()) a = rawAsset.toLower();
    } else {
        amt = s;   // the bare form is a native amount
    }

    if (a != QStringLiteral("native") && a != QStringLiteral("token"))
        return fail(QStringLiteral("unknown asset \"%1\" - use \"native\" or \"token\"").arg(a));
    if (amt.isEmpty())
        return fail(QStringLiteral("amount is required"));
    static const QRegularExpression amtRe(QStringLiteral("^[0-9]+(\\.[0-9]+)?$"));
    if (!amtRe.match(amt).hasMatch())
        return fail(QStringLiteral("\"%1\" is not an amount - pass a number, or "
                                   "{\"asset\":…,\"definitionId\":…,\"amount\":…}").arg(amt));
    if (a == QStringLiteral("token") && def.isEmpty())
        return fail(QStringLiteral("a token value needs its definitionId "
                                   "({\"asset\":\"token\",\"definitionId\":…,\"amount\":…})"));
    if (a == QStringLiteral("native") && !def.isEmpty())
        return fail(QStringLiteral("a definitionId only belongs on a token value - "
                                   "set \"asset\":\"token\""));

    if (asset)        *asset        = a;
    if (definitionId) *definitionId = def;
    if (amount)       *amount       = amt;
    return true;
}

bool WalletPlugin::parseRecipientSpec(const QString& spec, QString* accountId, QString* npk,
                                      QString* vpk, QString* identifier, QString* error)
{
    const auto fail = [&](const QString& msg) {
        if (error) *error = msg;
        return false;
    };
    if (accountId)  accountId->clear();
    if (npk)        npk->clear();
    if (vpk)        vpk->clear();
    if (identifier) identifier->clear();
    if (error)      error->clear();

    const QString s = spec.trimmed();
    if (s.isEmpty())
        return fail(QStringLiteral("to account is required"));

    if (!s.startsWith(QLatin1Char('{'))) {   // an owned account id
        if (accountId) *accountId = s;
        return true;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(s.toUtf8());
    if (!doc.isObject())
        return fail(QStringLiteral("recipient must be an account id or a JSON object "
                                   "{\"npk\":…,\"vpk\":…,\"identifier\":…}"));
    const QJsonObject o = doc.object();
    const QString n = o.value(QStringLiteral("npk")).toString().trimmed();
    const QString v = o.value(QStringLiteral("vpk")).toString().trimmed();
    const QString i = o.value(QStringLiteral("identifier")).toString().trimmed();
    // Named individually: a foreign private transfer is unattributable and unrecoverable, so
    // "which of the three keys did I leave out" has to be answerable before the proof runs.
    if (n.isEmpty()) return fail(QStringLiteral("recipient npk is required"));
    if (v.isEmpty()) return fail(QStringLiteral("recipient vpk is required"));
    if (i.isEmpty()) return fail(QStringLiteral("recipient identifier is required"));

    if (npk)        *npk        = n;
    if (vpk)        *vpk        = v;
    if (identifier) *identifier = i;
    return true;
}

// Normalise an account id to the canonical "<kind>/<bare>" the CLI expects (it
// only accepts capitalised Public/ and Private/). A bare id is prefixed; an id
// already carrying the *other* prefix is a conflict (Public/X and Private/X are
// distinct accounts), reported via *conflict with an empty return. Existing
// prefixes are matched case-insensitively so list output in either case is safe.
QString WalletPlugin::withPrivacyPrefix(const QString& id, const QString& kind, bool* conflict)
{
    if (conflict) *conflict = false;
    const QString trimmed = id.trimmed();
    const QString lower    = trimmed.toLower();

    QString existing;            // "Public" | "Private" | ""
    QString bare = trimmed;
    if (lower.startsWith(QStringLiteral("public/")))       { existing = QStringLiteral("Public");  bare = trimmed.mid(7); }
    else if (lower.startsWith(QStringLiteral("private/"))) { existing = QStringLiteral("Private"); bare = trimmed.mid(8); }

    if (!existing.isEmpty() && existing != kind) {
        if (conflict) *conflict = true;
        return QString();
    }
    return kind + QStringLiteral("/") + bare;
}

// Pull a tx hash out of a normalised CLI result. The fake test CLIs emit
// {"txId":…}/{"txHash":…}; the real wrapper emits {"output":"Transaction hash is <hash> …"}.
QString WalletPlugin::extractTxHash(const QString& normalizedJson)
{
    QJsonObject o = QJsonDocument::fromJson(normalizedJson.toUtf8()).object();
    for (const char* k : {"txId", "txHash", "tx_hash"}) {
        QString v = o.value(QLatin1String(k)).toString();
        if (!v.isEmpty()) return v;
    }
    const QString output = o.value(QStringLiteral("output")).toString();
    if (!output.isEmpty()) {
        static const QRegularExpression re(
            QStringLiteral("Transaction hash is\\s+(\\S+)"));
        QRegularExpressionMatch m = re.match(output);
        if (m.hasMatch())
            return m.captured(1);
    }
    return QString();
}

void WalletPlugin::appendLog(const QString& line, const QString& level)
{
    if (m_log.size() >= kMaxLogLines)
        m_log.removeFirst();
    LogEntry e;
    e.ts    = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
    e.msg   = line.trimmed();
    e.level = level;
    m_log.append(e);
}

namespace {
// Anchor symbol: dladdr resolves its address to THIS plugin .so, so the module can find
// binaries bundled alongside it (.../modules/medusa_core/bin/) and run self-contained.
// dladdr lives in glibc, so no extra -ldl is needed.
void medusaModuleAnchor() {}

// .../modules/medusa_core/bin -- where a packaged (downloaded) install keeps its binaries.
QString moduleBinDir()
{
    Dl_info info;
    if (dladdr(reinterpret_cast<void*>(&medusaModuleAnchor), &info) && info.dli_fname && *info.dli_fname) {
        const QString dir = QFileInfo(QString::fromUtf8(info.dli_fname)).absolutePath();
        if (!dir.isEmpty()) return dir + QStringLiteral("/bin");
    }
    return QString();
}

// ── INVARIANT B: the single resolver for every binary this module executes ────────────────
// Order, and there is no other order anywhere in this file:
//   1. `envVar` from THIS PROCESS'S ENVIRONMENT. The environment belongs to whoever launched the
//      module (Basecamp, a developer, the test suite); a co-resident module cannot write it.
//   2. The module's OWN bundle, <dir containing this .so>/bin/<name>. If that directory exists at
//      all the install is a packaged one and the bundle is the ONLY place consulted - the path is
//      returned even when the file is missing, so the caller reports "reinstall the module"
//      instead of silently running something else that happens to be lying around.
//   3. Only when there is no bundle directory at all (the unpackaged dev install, where
//      scripts/install-dev.sh stages everything into ~/.local/bin): ~/.local/bin/<name>, then the
//      bare name for a PATH lookup.
// QSettings is not in that list and is never consulted for a path (see cliPath() and seqPath()).
//
// What this defends and what it does not, stated plainly so no one reads more into it: it removes
// PERSISTED CONFIGURATION as a code-execution primitive - a data write that needs no executable
// bit, leaves the binaries untouched, survives a reboot and survives uninstalling the module that
// planted it. It does NOT defend against an attacker who overwrites the bundled binaries
// themselves. Nothing this module can do would: it runs at the same uid as its attacker, so
// <module>/bin/medusa-wallet is as writable as ~/.local/bin/medusa-wallet. What differs is
// that overwriting the bundle destroys a shipped file (a reinstall undoes it, an integrity check
// sees it) whereas step 3 lets an attacker ADD a file that was never there - which is why step 3
// is confined to installs that have no bundle to speak of.
// ── INVARIANT B2: nothing is ever launched by a BARE NAME ─────────────────────────────────
// A bare program name is resolved by QProcess against the PATH this process INHERITED, and that
// PATH is not ours: on an ordinary Linux desktop ~/.profile prepends ~/.local/bin (plus whatever
// else: ~/.npm-global/bin, ~/go/bin), every one of them writable by a co-resident module at this
// uid. Dropping a file called `curl` there and waiting for the 10-second status poll was
// unauthenticated arbitrary code execution as the user, on a PACKAGED install, with nobody at the
// keyboard - the same "persisted file becomes code execution" primitive that was removed from
// cliPath (round 2) and seqPath (round 3), one call-site class over.
//
// So there are exactly two ways a program string may be produced in this file:
//   • resolveBin()       - binaries this module SHIPS (wallet, sequencer, tor, forward, monitor):
//                          launcher-owned env var, else the module's own bundle, else a dev
//                          install's ~/.local/bin. Never a bare name any more (see below).
//   • resolveSystemBin() - SYSTEM helpers this module does not ship (curl, python3): a fixed list
//                          of root-owned system directories, checked for uid-writability, and
//                          $PATH is never read. Returns "" when the helper is absent, and every
//                          caller degrades gracefully instead of falling back to something else.
// startChild() enforces the result: it refuses to launch anything that is not an absolute path.
//
// The directories a SYSTEM helper may come from. Root-owned on every platform this module ships
// to; each one is still checked for writability at this uid before it is used, so a box where one
// of them is group/user-writable (or a container running as root with a writable /usr/local/bin)
// does not silently reintroduce the primitive.
static const char* const kSystemBinDirs[] = {
    "/usr/bin", "/bin", "/usr/sbin", "/sbin", "/usr/local/bin",
    "/run/current-system/sw/bin",         // NixOS system profile (the Basecamp runtime's box)
    "/nix/var/nix/profiles/default/bin",  // nix multi-user default profile (root-managed)
    "/opt/homebrew/bin", "/opt/local/bin" // macOS
};

// Is `dir` somewhere a co-resident module at this uid could drop a binary? Anything inside the
// user's home is (that is where ~/.local/bin lives), as is anything this process can write.
// Running as root defeats the whole question - everything is writable and the attacker model has
// already lost - so there the write test is skipped rather than rejecting every directory.
bool dirIsUidWritable(const QString& dir)
{
    if (dir.isEmpty() || !dir.startsWith(QLatin1Char('/')))
        return true;                                   // relative/empty: never trusted
    const QString home = QDir::homePath();
    if (!home.isEmpty() && (dir == home || dir.startsWith(home + QLatin1Char('/'))))
        return true;
    if (geteuid() == 0)
        return false;
    return ::access(QFile::encodeName(dir).constData(), W_OK) == 0;
}

// A SYSTEM helper (curl, python3), resolved WITHOUT consulting $PATH. Returns "" when the helper
// is not installed in any trusted directory, which callers must handle - "not found" is a fact to
// report, never a reason to fall back to a name lookup.
QString resolveSystemBin(const QString& name, const char* envVar = nullptr)
{
    // The environment belongs to whoever launched the module (Basecamp, a developer, the test
    // suite), not to a co-resident one - the same rule resolveBin() uses. An absolute path only:
    // a bare name here would be a PATH lookup by another route.
    if (envVar) {
        const QString fromEnv = qEnvironmentVariable(envVar).trimmed();
        if (fromEnv.startsWith(QLatin1Char('/')))
            return fromEnv;
    }
    for (const char* d : kSystemBinDirs) {
        const QString dir = QString::fromLatin1(d);
        const QString cand = dir + QLatin1Char('/') + name;
        if (!QFileInfo(cand).isExecutable())
            continue;
        if (dirIsUidWritable(dir))
            continue;   // a "system" dir this uid can write is not a system dir
        return cand;
    }
    return QString();
}

// The PATH handed to CHILD processes. Our own launches never consult it, but a child can: the
// wallet CLI is a `#!/usr/bin/env python3` script, so the kernel resolves python3 through the
// child's PATH, and a planted ~/.local/bin/python3 would run inside the wallet with the session
// password on its stdin. Entries writable at this uid are dropped rather than the whole variable
// being replaced, so a Nix/Basecamp runtime keeps the /nix/store paths its own tools live in.
QString sanitizedPath()
{
    const QString inherited = qEnvironmentVariable("PATH");
    QStringList keep;
    for (const QString& e : inherited.split(QLatin1Char(':'), Qt::SkipEmptyParts)) {
        const QString dir = e.trimmed();
        if (dir.isEmpty() || dirIsUidWritable(dir))
            continue;
        if (!keep.contains(dir))
            keep << dir;
    }
    if (keep.isEmpty()) {
        for (const char* d : kSystemBinDirs) {
            const QString dir = QString::fromLatin1(d);
            if (QDir(dir).exists() && !dirIsUidWritable(dir))
                keep << dir;
        }
    }
    return keep.join(QLatin1Char(':'));
}

// ── The OTHER search lists a child consults ──────────────────────────────────────────────────
// $PATH decides which FILE runs; these decide what that file, once running, is allowed to LOAD.
// Both filters use dirIsUidWritable(), so there is exactly one definition in this file of "a
// directory a co-resident module could drop a file into". The full enumeration of which variable
// feeds which runtime is above childEnv().

// A colon-separated list of DIRECTORIES, minus every entry writable at this uid. Unlike
// sanitizedPath() there is no "if nothing survives, use the system dirs" clause: an empty $PATH
// would break every launch, whereas an empty LD_LIBRARY_PATH just means the loader's built-in
// search, which is the correct and safe default.
QString dropUidWritableDirs(const QString& list)
{
    QStringList keep;
    for (const QString& e : list.split(QLatin1Char(':'), Qt::SkipEmptyParts)) {
        const QString dir = e.trimmed();
        if (dir.isEmpty() || dirIsUidWritable(dir))
            continue;
        if (!keep.contains(dir))
            keep << dir;
    }
    return keep.join(QLatin1Char(':'));
}

// LD_PRELOAD / LD_AUDIT name FILES (space- or colon-separated) that the dynamic loader maps into
// every dynamically linked child before main() runs. An entry survives only when it is an
// absolute path in a directory this uid cannot write, so a distro-wide preload keeps working and
// a planted one does not. A bare name is dropped outright: the loader would resolve it through
// its own search, which is the same primitive as a bare program name reaching QProcess.
QString dropUidWritableFiles(const QString& list)
{
    static const QRegularExpression sep(QStringLiteral("[\\s:]+"));
    QStringList keep;
    for (const QString& e : list.split(sep, Qt::SkipEmptyParts)) {
        const QString f = e.trimmed();
        if (!f.startsWith(QLatin1Char('/')) || dirIsUidWritable(QFileInfo(f).absolutePath()))
            continue;
        if (!keep.contains(f))
            keep << f;
    }
    return keep.join(QLatin1Char(' '));
}

// Apply one of the two filters to `name` in `env`, dropping the variable entirely when nothing
// survives (an empty LD_PRELOAD is not the same thing as no LD_PRELOAD to every loader).
void filterSearchVar(QProcessEnvironment& env, const char* name, bool entriesAreFiles)
{
    const QString key = QString::fromLatin1(name);
    const QString val = env.value(key);
    if (val.isEmpty())
        return;
    const QString kept = entriesAreFiles ? dropUidWritableFiles(val) : dropUidWritableDirs(val);
    if (kept.isEmpty()) env.remove(key);
    else                env.insert(key, kept);
}

QString resolveBin(const QString& name, const char* envVar = nullptr)
{
    if (envVar) {
        const QString fromEnv = qEnvironmentVariable(envVar).trimmed();
        if (!fromEnv.isEmpty()) {
            // A bare name from the environment is still a $PATH lookup, so it is resolved like a
            // system helper rather than handed to QProcess. An env var that names something not
            // installed resolves to "" and the caller reports it, which is the honest outcome.
            if (fromEnv.contains(QLatin1Char('/')))
                return QFileInfo(fromEnv).absoluteFilePath();
            return resolveSystemBin(fromEnv);
        }
    }
    const QString bdir = moduleBinDir();
    if (!bdir.isEmpty() && QDir(bdir).exists())
        return bdir + QStringLiteral("/") + name;   // packaged install: the bundle, or nothing
    const QString local = QDir::homePath() + QStringLiteral("/.local/bin/") + name;
    if (QFile::exists(local)) return local;
    // Last resort on an unpackaged install: a trusted system directory. This used to `return
    // name`, i.e. hand QProcess a bare name for a PATH lookup - the F1 primitive. Returning the
    // (absolute, non-existent) ~/.local/bin path when nothing is installed keeps the error message
    // pointing at where a dev install puts its binaries.
    const QString sys = resolveSystemBin(name);
    return sys.isEmpty() ? local : sys;
}

// Is something listening on 127.0.0.1:<port>? This used to be
//     bash -c 'exec 3<>/dev/tcp/127.0.0.1/<port>'
// which is (a) a bare-name launch of a shell resolved through $PATH and (b) a whole shell spawned
// to open one socket. A non-blocking connect() with a poll() timeout answers the same question in
// libc, so the shell - and with it the launch that had to be guarded - is simply gone.
bool tcpPortOpen(quint16 port, int timeoutMs)
{
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return false;
    int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags >= 0)
        ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    sockaddr_in sa;
    std::memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port   = htons(port);
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);   // loopback only: never a name lookup
    bool open = false;
    if (::connect(fd, reinterpret_cast<sockaddr*>(&sa), sizeof(sa)) == 0) {
        open = true;
    } else if (errno == EINPROGRESS) {
        pollfd pf;
        pf.fd = fd; pf.events = POLLOUT; pf.revents = 0;
        if (::poll(&pf, 1, timeoutMs) > 0 && (pf.revents & POLLOUT)) {
            int err = 0;
            socklen_t len = sizeof(err);
            open = (::getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) == 0 && err == 0);
        }
    }
    ::close(fd);
    return open;
}

// Resolve a usable Tor binary the way ensureTor() does: the bundled medusa-tor first,
// then a system tor. Returns "" when neither is present (Tor/onion zones can't connect).
QString resolveTorBin()
{
    const QString bundled = resolveBin(QStringLiteral("medusa-tor"), "MEDUSA_TOR_BIN");
    if (QFileInfo::exists(bundled)) return bundled;
    // A system tor, from a root-owned system directory only (resolveSystemBin checks that each
    // candidate's directory is not writable at this uid, and never reads $PATH).
    return resolveSystemBin(QStringLiteral("tor"));
}
}  // namespace

// Which binary the module executes as "the wallet CLI". This decides where the session password
// goes: runWalletCommandInput writes it to this binary's stdin on every call, so whoever controls
// this string gets arbitrary code execution AND the user's password.
//
// The QSettings override (medusa-wallet/cliPath) that used to win here is GONE, not filtered.
// A filter could not be a boundary, for two reasons that compound:
//   • the setting lives in a plain, user-writable INI (~/.config/<Org>/<App>.conf), so a
//     co-resident module writes the key directly - no IPC, no password, nothing to gate; and
//   • the same module runs at the same uid, so any "it must be an executable named wallet* in a
//     known bin dir" rule is satisfied by dropping a file into ~/.local/bin or the module's own
//     bin dir, both of which it can write.
// The alternative the reviewers named, verifying by CONTENT HASH, needs a trusted reference the
// module does not have: the wallet binary is built per release and shipped inside this very
// module, so the only hash available to compare against is the hash of the binary we would have
// run anyway. That leaves removal, which costs nothing real: the legitimate need (a developer
// running a custom build, and the test suite) is served by MEDUSA_WALLET_CLI, which comes from
// THIS process's environment - set by whoever launched the module, not writable by a co-resident
// one - and the user whose bundled binary is missing was never actually served by the setter
// (Save needed the password, the password needed unlock, and unlock needed the CLI).
QString WalletPlugin::cliPath() const
{
    // MEDUSA_WALLET_CLI (launcher-owned) -> the module's bundle -> (unpackaged installs only)
    // ~/.local/bin -> PATH. See resolveBin() for the full rule and for what it does not defend.
    //
    // The wrapper is "medusa-wallet": namespaced like every other binary we ship (medusa-tor,
    // medusa-tor-monitor, diaphani-forward). A bare "wallet" in a shared ~/.local/bin is both a
    // collision risk and the most plantable name there is, which is exactly the surface
    // resolveBin() distrusts on packaged installs.
    const QString named = resolveBin(QStringLiteral("medusa-wallet"), "MEDUSA_WALLET_CLI");
    if (!named.isEmpty())
        return named;
    // Legacy fallback, one release only: installs made before the rename staged the wrapper as
    // "wallet". Without this an upgrade would fail "wallet CLI not found" on every operation.
    return resolveBin(QStringLiteral("wallet"), "MEDUSA_WALLET_CLI");
}

// ── QProcess runner ──────────────────────────────────────────────────────────

// ── INVARIANT B3: every code-search list a child of this module consults ─────────────────────
//
// startChild() decides WHICH FILE is executed. This decides what that file, once it is running,
// is allowed to LOAD - a separate question, and until round 5 it was answered only for $PATH.
// It matters here more than in most programs because the wallet CLI is not a native binary: it
// is a `#!/usr/bin/env python3` script, so the process that receives the session password on its
// stdin first runs a complete CPython startup, and CPython has code-search paths of its own. A
// co-resident module needs no exec bit, no IPC, no user and no password to write a file into one
// of them; round 4 shipped with ~/.local/lib/pythonX.Y/site-packages open, and a one-line .pth
// there was arbitrary code execution inside the wallet CLI plus capture of the password off fd 0.
//
// THE ENUMERATION, per runtime this module can cause to execute. Callers build on childEnv()
// rather than on systemEnvironment(), so no launch can opt out of any of it.
//
//  1. /usr/bin/env, run by the KERNEL for the CLI's shebang line. It resolves `python3` through
//     the child's $PATH -> sanitizedPath(). CLOSED (rounds 3-4).
//
//  2. CPython: the wallet CLI (via its shebang) and the Tor onion-stage monitor (via an explicit
//     interpreter). Its startup executes code found through:
//       a. the PER-USER SITE directory, ~/.local/lib/pythonX.Y/site-packages. site.py exec()s
//          any .pth line that begins with `import `, and imports usercustomize from there. This
//          was the round-4 hole -> PYTHONNOUSERSITE=1. CLOSED.
//       b. $PYTHONPATH, which is prepended AHEAD OF THE STDLIB (so an entry shadows `json`,
//          `os`, `subprocess`) and from which site.py imports `sitecustomize` - which, unlike
//          usercustomize, PYTHONNOUSERSITE does NOT disable -> removed. CLOSED.
//       c. every other PYTHON* knob: $PYTHONHOME (relocates the whole stdlib), $PYTHONSTARTUP,
//          $PYTHONBREAKPOINT, $PYTHONUSERBASE, $PYTHONPLATLIBDIR, $PYTHONINSPECT, and whatever
//          the next release adds. Enumerating a set that grows is how this class keeps
//          reappearing, so instead EVERY variable whose name starts with PYTHON is dropped and
//          only the isolation flags are put back. That is `-E` delivered through the
//          environment, and it is closed by construction rather than by list. CLOSED.
//       d. sys.path[0], the directory the SCRIPT ITSELF lives in, searched BEFORE the stdlib.
//          PYTHONSAFEPATH=1 removes it on CPython >= 3.11. On 3.10 and older it stays and this
//          module cannot remove it: the CLI is launched by its own shebang, and the switches
//          that drop it (`-P`, `-I`) exist only on a command line (see WHY THE ENVIRONMENT,
//          below). NOT CLOSED on CPython <= 3.10 - stated plainly rather than certified. Its
//          scope: that directory is the one holding the wallet binary itself (<module>/bin on a
//          packaged install, ~/.local/bin on a dev one), so writing to it is the residual
//          invariant B already concedes ("it does NOT defend against an attacker who overwrites
//          the bundled binaries themselves"), not a new one.
//       e. .pth files in the interpreter's OWN prefix (/usr/lib/python3/dist-packages,
//          /usr/local/lib/pythonX.Y/dist-packages). Root-owned on a normal install. On a box
//          where /usr/local is user-writable they are writable, and NOTHING this module can do
//          changes that - it is compiled into the interpreter. NOT CLOSEABLE from here. What is
//          closed is the interpreter itself: it is only ever taken from a directory this uid
//          cannot write (resolveSystemBin() for the monitor, sanitizedPath() for the shebang).
//
//  3. ld.so and glibc, for every dynamically linked child (medusa-tor, diaphani-forward,
//     sequencer_service, curl, and wallet-lez, which the wrapper execs with dict(os.environ),
//     so this reaches the grandchild too): $LD_PRELOAD and $LD_AUDIT name objects mapped in
//     before main(), $LD_LIBRARY_PATH is searched before the default paths, and $GCONV_PATH is
//     a directory glibc dlopen()s character-set modules out of the moment anything converts an
//     encoding. All four are FILTERED to entries this uid cannot write rather than deleted, so
//     a Nix/Basecamp runtime keeps the /nix/store paths its own binaries need. DT_RPATH and
//     DT_RUNPATH live INSIDE the binary and are the same conceded residual as 2d. CLOSED for
//     the environment half.
//
//  4. A SHELL: none. Every `bash -c`/`sh -c` in this module was removed in rounds 3 and 4, so
//     $BASH_ENV, $ENV, $SHELLOPTS and $IFS have no reader among its children. NOT APPLICABLE,
//     and it stays that way only because startChild() is the single launch point.
//
//  5. Qt's plugin and QML import search ($QT_PLUGIN_PATH, $QT_QPA_PLATFORM_PLUGIN_PATH,
//     $QML_IMPORT_PATH, $QML2_IMPORT_PATH): no child of this module is a Qt program, so
//     filtering these cannot break anything today and is purely prophylactic for a future one.
//     THIS PROCESS's own plugin path is a different question with an honest answer: it is fixed
//     by the host application before this module is loaded, and a plugin that rewrote
//     QCoreApplication::libraryPaths() would be sabotaging its host. NOT CLOSEABLE from here,
//     and not this module's to close.
//
// WHY THE ENVIRONMENT, AND NOT `python3 -I <script>`. `-I` is stronger by exactly one item (2d
// on CPython <= 3.10). Buying it would mean this module stops executing the file it resolved and
// instead sniffs the file's first two bytes, decides it is python, resolves an interpreter, and
// rebuilds the argv. That (a) FAILS OPEN when no python3 is found in a trusted directory, and
// "fall back to running it directly" is exactly the silent degradation that has cost three
// rounds; (b) breaks any CLI the launcher legitimately points MEDUSA_WALLET_CLI at that is not
// python (the test suite's stand-ins today, a native build tomorrow); and (c) puts a second
// program name on the execution path that startChild() would have to trust. The environment
// reaches the CLI THROUGH its shebang for free, because exec() carries envp across both hops
// (kernel -> /usr/bin/env -> python3). That is asserted, not assumed:
// testAPlantedUserSitePthNeverExecutesInAChild drives a real `#!/usr/bin/env python3` script and
// proves the same plant DOES fire without the hardening. Where the interpreter IS named on a
// command line - the Tor monitor - `-I` is passed as well, because there it costs nothing.
QProcessEnvironment WalletPlugin::childEnv()
{
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("PATH"), sanitizedPath());

    // 2a-2d: CPython, isolated as far as an environment can reach.
    const QStringList inherited = env.keys();
    for (const QString& k : inherited)
        if (k.startsWith(QStringLiteral("PYTHON")))
            env.remove(k);
    env.insert(QStringLiteral("PYTHONNOUSERSITE"), QStringLiteral("1"));  // no user site: no .pth, no usercustomize
    env.insert(QStringLiteral("PYTHONSAFEPATH"),   QStringLiteral("1"));  // >= 3.11: drop sys.path[0]
    env.insert(QStringLiteral("PYTHONBREAKPOINT"), QStringLiteral("0"));  // breakpoint() imports whatever this names
    // Not a boundary, hygiene: the wallet must not leave __pycache__ entries in a directory it
    // does not own. It does not close the "forge a .pyc" path, which needs the same write access
    // as replacing the wallet binary (2d), and it is not claimed to.
    env.insert(QStringLiteral("PYTHONDONTWRITEBYTECODE"), QStringLiteral("1"));

    // 3: the dynamic loader and glibc, for the native children.
    filterSearchVar(env, "LD_LIBRARY_PATH", false);
    filterSearchVar(env, "LD_PRELOAD",      true);
    filterSearchVar(env, "LD_AUDIT",        true);
    filterSearchVar(env, "GCONV_PATH",      false);

    // 5: Qt, prophylactically - no current child reads these.
    filterSearchVar(env, "QT_PLUGIN_PATH",               false);
    filterSearchVar(env, "QT_QPA_PLATFORM_PLUGIN_PATH",  false);
    filterSearchVar(env, "QML_IMPORT_PATH",              false);
    filterSearchVar(env, "QML2_IMPORT_PATH",             false);
    return env;
}

// ── THE ONE LAUNCH POINT (invariant B2) ───────────────────────────────────────────────────────
// Every QProcess in this module is started here, and this refuses anything that is not an
// ABSOLUTE path. That is the whole enforcement: a bare name would be resolved by QProcess against
// the inherited $PATH, which contains directories a co-resident module can write, and three
// rounds of review have now found the same "attacker plants a file, the wallet executes it"
// primitive at three different call sites. A name cannot reach QProcess from here even by
// accident, and a future call site that tries fails loudly in the log instead of silently running
// whatever was found first.
//
//     grep -n "\.start(\|->start(" WalletPlugin.cpp
//         -> QProcess::start appears exactly once, in this function.
//
// It also installs childEnv(), which is a SEPARATE boundary and must not be read as part of this
// one: this function controls which file runs, childEnv() controls what that file may then load.
// The comment that used to stand here said a child "that resolves ITS own helpers by name (the
// wallet CLI is a `#!/usr/bin/env python3` script) cannot be fed a planted one either", and that
// was a certification of a class from one instance. It was true of $PATH and false of the python
// interpreter's own import search, which round 4 shipped wide open: a .pth file in
// ~/.local/lib/pythonX.Y/site-packages ran attacker code inside the wallet CLI and read the
// session password off its stdin. What is and is not closed is now enumerated, per runtime and
// per search list, above childEnv() - including two items that are NOT closed and say so.
// A caller that has already set an environment (the sequencer's RISC0_DEV_MODE) keeps it - those
// build theirs from childEnv().
bool WalletPlugin::startChild(QProcess& p, const QString& program, const QStringList& args)
{
    if (program.isEmpty() || !program.startsWith(QLatin1Char('/'))) {
        appendLog(QStringLiteral("refusing to launch a program that is not an absolute path: '")
                      + (program.isEmpty() ? QStringLiteral("<empty>") : program)
                      + QStringLiteral("' - a bare name would be resolved through $PATH"),
                  QStringLiteral("error"));
        return false;
    }
    if (p.processEnvironment().isEmpty())
        p.setProcessEnvironment(childEnv());
    p.start(program, args);
    return true;
}

QString WalletPlugin::runWalletCommand(const QStringList& args, int timeoutMs)
{
    return runWalletCommandInput(args, m_password + QStringLiteral("\n"), timeoutMs);
}

// Strip the CLI's interactive prompts (now emitted on stderr) so a stderr error
// message can be surfaced cleanly.
QString WalletPlugin::cleanStderr(const QString& raw)
{
    QString s = raw;
    s.remove(QStringLiteral("Input password: "));
    s.remove(QStringLiteral("Input recovery phrase: "));
    s.remove(QStringLiteral("Input private key: "));
    return s.trimmed();
}

QString WalletPlugin::redactedArgs(const QStringList& args)
{
    // No secret travels in argv any more (the imported signing key was the last one and now
    // goes on stdin), but the log line is built from whatever it is handed, so redact by flag
    // name rather than trusting every future call site to remember.
    static const QStringList kSecretFlags{
        QStringLiteral("--private-key"), QStringLiteral("--password"),
        QStringLiteral("--mnemonic"), QStringLiteral("--phrase"), QStringLiteral("--seed")
    };
    QStringList out;
    out.reserve(args.size());
    bool redactNext = false;
    for (const QString& a : args) {
        if (redactNext) { out << QStringLiteral("<redacted>"); redactNext = false; continue; }
        const int eq = a.indexOf(QLatin1Char('='));   // --flag=value form
        if (eq > 0 && kSecretFlags.contains(a.left(eq)))
            out << a.left(eq) + QStringLiteral("=<redacted>");
        else {
            out << a;
            redactNext = kSecretFlags.contains(a);
        }
    }
    return out.join(QLatin1Char(' '));
}

bool WalletPlugin::outputIsSecret(const QStringList& args)
{
    // export-key prints a 64-char hex signing key and export-mnemonic prints the recovery
    // phrase, so their stdout IS the secret; `account import` and restore-keys echo the
    // offending value back on a parse error. 80 chars of an export was 56 of the 64 hex
    // characters, i.e. 224 of 256 bits, and createEncryptedWallet calls exportMnemonic, so a
    // truncated seed phrase used to enter the buffer at wallet CREATION.
    if (args.contains(QStringLiteral("export-key"))
        || args.contains(QStringLiteral("export-mnemonic"))
        || args.contains(QStringLiteral("restore-keys")))
        return true;
    return args.value(0) == QStringLiteral("account")
        && args.value(1) == QStringLiteral("import");
}

QString WalletPlugin::runWalletCommandInput(const QStringList& args,
                                            const QString& stdinData, int timeoutMs)
{
    QString bin = cliPath();
    const bool secret = outputIsSecret(args);
    appendLog(QStringLiteral("run: wallet ") + redactedArgs(args));

    QProcess proc;
    // Keep channels separate: the CLI prompts on stderr and returns results on
    // stdout, so we must not let the "Input password: " prompt pollute stdout.
    proc.setProcessChannelMode(QProcess::SeparateChannels);
    if (!startChild(proc, bin, args)) {
        return errorJson(QStringLiteral("wallet CLI not found: ")
                         + (bin.isEmpty() ? QStringLiteral("<unresolved>") : bin)
                         + QStringLiteral(" - reinstall the medusa_core module, or set "
                                          "MEDUSA_WALLET_CLI to an absolute path before "
                                          "launching"));
    }

    if (!proc.waitForStarted(3000)) {
        appendLog(QStringLiteral("failed to start: ") + proc.errorString(), QStringLiteral("error"));
        // NOT "configure the path in settings": there is no such setting any more (see
        // cliPath()), and pointing the user at one would be advice into a dead end.
        return errorJson(QStringLiteral("wallet CLI not found: ") + bin
                         + QStringLiteral(" - reinstall the medusa_core module, or set "
                                          "MEDUSA_WALLET_CLI before launching"));
    }

    // Feed the password (and, for restore, the mnemonic line) to the CLI's stdin.
    proc.write(stdinData.toUtf8());
    proc.closeWriteChannel();

    if (!proc.waitForFinished(timeoutMs)) {
        proc.kill();
        appendLog(QStringLiteral("timeout after %1ms").arg(timeoutMs), QStringLiteral("error"));
        return errorJson(QStringLiteral("wallet command timed out"));
    }

    const QString out = QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
    const QString err = cleanStderr(QString::fromUtf8(proc.readAllStandardError()));
    const int exitCode = proc.exitCode();

    // Never echo a secret-bearing command's output: on success stdout IS the key/phrase, and on
    // failure clap quotes the value it rejected. Only the size goes in the log.
    if (exitCode != 0)
        appendLog(QStringLiteral("exit %1: ").arg(exitCode)
                      + (secret ? QStringLiteral("<redacted>")
                                : (err.isEmpty() ? out : err).left(120)),
                  QStringLiteral("error"));
    else
        appendLog(QStringLiteral("ok: ") + (secret ? QStringLiteral("<redacted, %1 bytes>")
                                                         .arg(out.size())
                                                   : out.left(80)));

    // On failure the message is on stderr; on success the result is on stdout.
    const QString effective = (exitCode != 0 && out.isEmpty()) ? err : out;
    return normalizeCliOutput(effective, exitCode);
}

// Turn raw merged CLI output + exit code into the module's JSON contract.
// The wallet wrapper script (~/.local/bin/medusa-wallet) already emits JSON for some
// commands and free text for others, so this mirrors that: valid JSON passes
// through untouched; text becomes {"ok":true,"output":…} on success or
// {"error":…} on failure.
QString WalletPlugin::normalizeCliOutput(const QString& rawOut, int exitCode)
{
    QString out = rawOut.trimmed();

    QJsonParseError pe;
    QJsonDocument doc = QJsonDocument::fromJson(out.toUtf8(), &pe);
    bool isJson = (pe.error == QJsonParseError::NoError);

    if (exitCode != 0) {
        if (isJson)
            return out;  // CLI/wrapper already produced a structured error
        return errorJson(out.isEmpty()
                         ? QStringLiteral("wallet command failed (exit %1)").arg(exitCode)
                         : out);
    }

    if (isJson)
        return out;

    QJsonObject o;
    o[QStringLiteral("ok")]     = true;
    o[QStringLiteral("output")] = out;
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

// ── Lifecycle ─────────────────────────────────────────────────────────────────

WalletPlugin::WalletPlugin(QObject* parent)
    : QObject(parent)
{
    m_bornMs = QDateTime::currentMSecsSinceEpoch();
    m_zoneCompat = QStringLiteral("unknown");
    migrateLegacyNaming();
}

// One-time data migration from the legacy "logos-wallet" naming to "medusa-wallet". Idempotent
// (acts only when the new target is absent), so it is safe to run on every construction: the
// QSettings keys are COPIED (the old ones are left as a backup) and the wallet home dir is MOVED.
void WalletPlugin::migrateLegacyNaming()
{
    const QString oldHome = QDir::homePath() + QStringLiteral("/.local/share/logos-wallet-home");
    const QString newHome = QDir::homePath() + QStringLiteral("/.local/share/medusa-wallet-home");
    if (QDir(oldHome).exists() && !QDir(newHome).exists() && QDir().rename(oldHome, newHome))
        appendLog(QStringLiteral("migrated wallet home: logos-wallet-home -> medusa-wallet-home"));

    QSettings s;
    const QStringList keys = s.allKeys();
    for (const QString& k : keys) {
        if (k.startsWith(QStringLiteral("logos-wallet/"))) {
            const QString nk = QStringLiteral("medusa-wallet/") + k.mid(13);   // 13 == len("logos-wallet/")
            if (!s.contains(nk)) s.setValue(nk, s.value(k));
        }
    }
}

void WalletPlugin::initLogos(LogosAPI* api)
{
    logosAPI = api;
    appendLog(QStringLiteral("medusa_core: initLogos called"));
    // Defer the sequencer/Tor bring-up to the next event-loop tick so module LOAD never
    // blocks on launching Tor/the forward (that was hanging the host's module loading).
    QTimer::singleShot(0, this, [this]() {
        applySequencer();   // point wallet_config.json at the active zone + bring up Tor/forward
    });
}

// ── Status / Config ───────────────────────────────────────────────────────────

QString WalletPlugin::getStatus() const
{
    // This used to shell out to `which wallet` whenever cliPath() returned a bare name, which was
    // both a $PATH-resolved launch of its own (F1) and a question about a state that no longer
    // exists: cliPath() is always an absolute path now (see resolveBin), so the answer is a
    // filesystem fact and needs no process at all. `which` is gone from this module.
    const QString bin = cliPath();
    const bool found = !bin.isEmpty() && QFileInfo(bin).isExecutable();

    QJsonObject o;
    o[QStringLiteral("cliFound")] = found;
    o[QStringLiteral("cliPath")]  = bin;
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

QString WalletPlugin::getConfig() const
{
    QSettings s;
    const QString stored = s.value(QLatin1String(kCliPathKey)).toString();
    QJsonObject o;
    // The key is read here and NOWHERE else. An install poisoned before the override was removed
    // still shows what was planted, which is the only visible trace it was ever there; it is
    // never executed, whatever it points at.
    o[QStringLiteral("cliPath")]    = stored;
    o[QStringLiteral("cliPathEff")] = cliPath();
    o[QStringLiteral("cliPathIgnored")]      = !stored.trimmed().isEmpty();
    o[QStringLiteral("cliPathConfigurable")] = false;
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

QString WalletPlugin::setCliPath(const QString& path, const QString& password)
{
    // Always refused. Storing a CLI path was code execution plus password capture that outlived
    // both a reboot and the module that planted it, and no gate on THIS function could protect
    // it: the setting is an ordinary file that a co-resident module writes directly (see
    // cliPath()). Nothing is stored, nothing is validated, and the caller is told where the
    // remaining override lives so the message is not another dead end.
    Q_UNUSED(path);
    Q_UNUSED(password);
    return errorJson(QStringLiteral("the wallet CLI path is no longer configurable - medusa runs "
                                    "the binary bundled with the module; set MEDUSA_WALLET_CLI "
                                    "before launching to use a different build"),
                     QStringLiteral("not-supported"));
}

// ── Sequencer (local auto-launch / hosted) ──────────────────────────────────────

// The sequencer binary this module SPAWNS (ensureSequencer -> QProcess::start). Whoever controls
// this string gets arbitrary code execution as the user.
//
// It used to read medusa-wallet/seqPath out of QSettings and return it VERBATIM, with no check on
// the read side and no gate anywhere: a co-resident module wrote the key into the shared,
// user-writable INI (no IPC, no password, nothing to authorise) and then called the ungated
// setActiveZone("devnet"), and the wallet spawned the attacker's binary. That is the exact
// primitive that was removed from cliPath() one round earlier and left standing here - persistent
// across reboots and across uninstalling the module that planted it.
//
// It is now resolved exactly like cliPath(): a launcher-owned env var, else the module's own
// bundle. The setting is still REPORTED by getSequencerConfig (as seqPath/seqPathIgnored) so a
// poisoned install shows what was planted, and it is never executed.
QString WalletPlugin::seqPath() const
{
    // diaphani/Tor mode talks to a REAL shared Bedrock L1, so it needs the non-standalone
    // build (sequencer_service_l1); devnet/testnet use the L1-free standalone build.
    const QString binName = (netId() == QStringLiteral("diaphani"))
        ? QStringLiteral("sequencer_service_l1") : QStringLiteral("sequencer_service");
    return resolveBin(binName, "MEDUSA_SEQ_PATH");
}

// ── Zones ───────────────────────────────────────────────────────────────────────
// A "zone" is a LEZ chain. Built-ins: "devnet" (local standalone sandbox) and
// "diaphani" (local sequencer on the shared Bedrock L1 over Tor). User zones are
// REMOTE - the wallet is a thin client of someone else's sequencer (clearnet URL or a
// Tor .onion). Accounts/keys are shared across zones; balances/tokens are per-zone.

QString WalletPlugin::netId() const   // the active zone id
{
    QSettings s;
    // Default for a FRESH install: the hosted "Paradox Computer · clearnet" zone, which now
    // always has an endpoint (clearnetUrl() falls back to the baked default). An existing
    // install keeps whatever zone the user last selected, since that is stored in QSettings.
    const QString def = clearnetUrl().isEmpty() ? QStringLiteral("devnet")
                                                : QStringLiteral("paradox-clearnet");
    return s.value(QLatin1String(kNetworkKey), def).toString();
}

QJsonArray WalletPlugin::userZones() const
{
    QSettings s;
    return QJsonDocument::fromJson(
        s.value(QLatin1String(kZonesKey)).toString().toUtf8()).array();
}

QJsonObject WalletPlugin::zoneObj(const QString& id) const
{
    // Built-in remote zone: the Paradox Computer clearnet sequencer (not stored in userZones).
    if (id == QStringLiteral("paradox-clearnet")) {
        QJsonObject o;
        o[QStringLiteral("id")]    = id;
        o[QStringLiteral("name")]  = QStringLiteral("Paradox Computer");
        o[QStringLiteral("url")]   = clearnetUrl();
        o[QStringLiteral("onion")] = QString();
        o[QStringLiteral("tor")]   = false;
        o[QStringLiteral("tokenFaucet")] = (faucetZoneRow(id) != nullptr);
        return o;
    }
    // Built-in remote zone: the official Logos public testnet (logos-co, LEZ v0.2.0).
    if (id == QStringLiteral("logos-testnet")) {
        QJsonObject o;
        o[QStringLiteral("id")]    = id;
        o[QStringLiteral("name")]  = QStringLiteral("Logos public testnet");
        o[QStringLiteral("url")]   = logosTestnetUrl();
        o[QStringLiteral("onion")] = QString();
        o[QStringLiteral("tor")]   = false;
        o[QStringLiteral("tokenFaucet")] = (faucetZoneRow(id) != nullptr);
        return o;
    }
    const QJsonArray arr = userZones();
    for (const auto& v : arr) {
        QJsonObject o = v.toObject();
        if (o.value(QStringLiteral("id")).toString() == id) {
            // OVERWRITTEN, never read: the stored record is a user-writable INI value, so a
            // planted "tokenFaucet":true would otherwise hand a foreign sequencer the token
            // half. The capability is a fact about kFaucetZones and is recomputed here.
            o[QStringLiteral("tokenFaucet")] = (faucetZoneRow(id) != nullptr);
            return o;
        }
    }
    return {};
}

// The per-zone capability, in one predicate. Everything that asks "does this zone have the
// on-chain token faucet" goes through here, so the answer cannot differ between the zone list
// the UI renders, the preflight the status call reports, and the claim that actually runs.
bool WalletPlugin::zoneHasTokenFaucet(const QString& zoneId) const
{
    return faucetZoneRow(zoneId) != nullptr;
}

// True only for user-added zones. zoneObj() also returns a record for the built-in clearnet zone,
// so editZone/removeZone must gate on THIS (membership in the stored user list), not zoneObj.
bool WalletPlugin::isUserZone(const QString& id) const
{
    for (const auto& v : userZones())
        if (v.toObject().value(QStringLiteral("id")).toString() == id) return true;
    return false;
}

QString WalletPlugin::zoneKind(const QString& id) const
{
    if (id == QStringLiteral("devnet"))   return QStringLiteral("local-standalone");
    if (id == QStringLiteral("diaphani")) return QStringLiteral("local-l1-tor");
    return QStringLiteral("remote");   // user-added: thin client
}

int WalletPlugin::netPort() const
{
    const QString id = netId();
    if (id == QStringLiteral("devnet"))   return 3071;
    if (id == QStringLiteral("diaphani")) return 3077;   // sequencer bound to the Tor-fronted L1
    return 3080;                                         // remote+tor: local diaphani-forward listen port
}

// Bumped whenever the LEZ engine changes in a way that makes an existing chain db unusable.
// v0.2.0 rebuilt every risc0 program, so the ImageIDs, the derived faucet/bridge system account
// ids, and the genesis commitment tree all differ from rc5. SequencerCore::start_from_config
// reuses an existing rocksdb with NO version check, and writeSeqConfig() below deliberately keeps
// the old home across runs, so without this suffix a v0.2.0 binary would silently start on an
// rc5 chain whose accounts are owned by program ids it no longer knows.
// EMPTY for the rc5 engine so existing homes keep their current path and their chain state.
// Set this to "v020" in the SAME commit that flips wallet/build.sh to LEZ_BASE_REV=v0.2.0 +
// PATCH_DIR=patches-v020; the two must move together or the epoch stops describing the engine.
static constexpr const char* kEngineEpoch = "v020";

QString WalletPlugin::seqHome() const
{
    // Per-zone, per-engine-epoch home (only local zones keep chain state here). A new epoch
    // starts from a fresh genesis instead of inheriting a chain the new engine cannot read.
    const QString epoch = QLatin1String(kEngineEpoch);
    return walletHome() + QStringLiteral("/sequencer-") + netId()
         + (epoch.isEmpty() ? QString() : QStringLiteral("-") + epoch);
}

// curl, resolved to an absolute path in a root-owned system directory - never the bare name it
// used to be launched with. "" when curl is not installed: the probe then reports "not healthy"
// (see the callers), which is the honest answer and is not a reason to run something else.
QString WalletPlugin::curlPath()
{
    return resolveSystemBin(QStringLiteral("curl"), "MEDUSA_CURL_BIN");
}

bool WalletPlugin::seqHealthyUrl(const QString& url, int timeoutMs)
{
    // Probe the sequencer's JSON-RPC checkHealth at an arbitrary URL via curl (keeps the
    // dependency surface at zero - the rest of this file shells out the same way).
    if (url.trimmed().isEmpty()) return false;
    const QString curl = curlPath();
    if (curl.isEmpty()) {
        appendLog(QStringLiteral("curl is not installed in any trusted system directory - the "
                                 "sequencer health probe cannot run"), QStringLiteral("error"));
        return false;
    }
    QProcess p;
    if (!startChild(p, curl, {
        // curl's own --max-time must track timeoutMs - a hardcoded "1" ignored the argument and
        // timed out on any real over-the-internet HTTPS round-trip (~2s incl. the TLS handshake).
        QStringLiteral("-s"), QStringLiteral("--max-time"), QString::number(qMax(1, timeoutMs / 1000)),
        QStringLiteral("-X"), QStringLiteral("POST"),
        QStringLiteral("-H"), QStringLiteral("content-type: application/json"),
        QStringLiteral("-d"),
        QStringLiteral(R"({"jsonrpc":"2.0","id":1,"method":"checkHealth","params":[]})"),
        url
    }))
        return false;
    if (!p.waitForFinished(timeoutMs + 800)) { p.kill(); p.waitForFinished(300); return false; }
    return p.exitCode() == 0 &&
           QString::fromUtf8(p.readAllStandardOutput()).contains(QStringLiteral("\"result\""));
}

bool WalletPlugin::seqHealthy(int port, int timeoutMs)
{
    return seqHealthyUrl(QStringLiteral("http://127.0.0.1:%1/").arg(port), timeoutMs);
}

void WalletPlugin::writeSeqConfig(const QString& cfgPath) const
{
    if (QFile::exists(cfgPath))
        return;   // keep the existing chain db/config across runs
    QDir().mkpath(seqHome());
    QString cfg = QString::fromUtf8(kSeqConfigTemplate)
                      .replace(QStringLiteral("__SEQ_HOME__"), seqHome());
    if (netId() == QStringLiteral("diaphani")) {
        // Point the sequencer's Bedrock L1 connection at the local diaphani-forward
        // (which tunnels to the node's .onion over Tor) instead of the dead/mocked url.
        cfg.replace(QStringLiteral("\"node_url\":\"http://127.0.0.1:1\""),
                    QStringLiteral("\"node_url\":\"http://127.0.0.1:8081/\""));
    }
    QFile f(cfgPath);
    if (f.open(QIODevice::WriteOnly))
        f.write(cfg.toUtf8());
}

// Launch the BUNDLED Tor on a private SOCKS port so users need no external Tor. Idempotent
// and non-blocking - Tor bootstraps in the background; diaphani-forward connects lazily, so
// requests just wait for the first circuit (shown as "Connecting…").
void WalletPlugin::ensureTor()
{
    if (m_torProc && m_torProc->state() != QProcess::NotRunning)
        return;
    // This is a FRESH bring-up attempt: the failure record describes THIS one, not a previous
    // one (mirrors ensureSequencer's m_seqLaunchError/m_seqExited reset).
    m_torLaunchError.clear();
    m_torExited = false;
    m_torExitCode = 0;
    // Reuse any Tor already on our SOCKS port (e.g. one orphaned by a previous hard-kill
    // that still holds the data-dir lock) instead of launching a duplicate that would fail.
    // (This was a `bash -c 'exec 3<>/dev/tcp/...'` spawn - a $PATH-resolved shell to open one
    // socket. tcpPortOpen() does it in libc, so the launch no longer exists to be attacked.)
    // RECORDED, because an adopted Tor is not ours to reap: stopTor() must not kill another
    // wallet window's Tor, and cancelConnect() has to be able to SAY that rather than claim a
    // teardown it did not perform.
    if (tcpPortOpen(quint16(torSocksPort()), 700)) {
        appendLog(QStringLiteral("reusing Tor already on 127.0.0.1:%1").arg(torSocksPort()));
        m_torAdopted = true;
        return;
    }
    // Prefer the bundled binary (MEDUSA_TOR_BIN overrides, same rule as every other binary here);
    // fall back to a system tor at an absolute, root-owned path if present.
    QString torBin = resolveBin(QStringLiteral("medusa-tor"), "MEDUSA_TOR_BIN");
    if (!QFileInfo::exists(torBin)) {
        const QString sysTor = resolveSystemBin(QStringLiteral("tor"));
        if (!sysTor.isEmpty()) torBin = sysTor;
    }
    if (!QFileInfo::exists(torBin)) {
        m_torLaunchError = QStringLiteral("no Tor binary found (looked for ") + torBin
                         + QStringLiteral(" and a system tor)");
        appendLog(QStringLiteral("bundled Tor not found (~/.local/bin/medusa-tor)"), QStringLiteral("error"));
        return;
    }
    const QString dataDir = walletHome() + QStringLiteral("/tor");
    QDir().mkpath(dataDir);
    QFile::remove(dataDir + QStringLiteral("/onion-stage.json"));   // clear stale onion stage
    m_torProc = new QProcess(this);
    m_torProc->setProcessChannelMode(QProcess::SeparateChannels);
    appendLog(QStringLiteral("launching bundled Tor (SOCKS 127.0.0.1:%1)").arg(kTorSocksPort));
    startChild(*m_torProc, torBin, {
        QStringLiteral("--SocksPort"),       QStringLiteral("127.0.0.1:%1").arg(kTorSocksPort),
        QStringLiteral("--ControlPort"),     QStringLiteral("127.0.0.1:%1").arg(kTorControlPort),
        QStringLiteral("--CookieAuthentication"), QStringLiteral("1"),
        QStringLiteral("--DataDirectory"),   dataDir,
        QStringLiteral("--ClientOnly"),      QStringLiteral("1"),
        QStringLiteral("--Log"),             QStringLiteral("notice file ") + dataDir + QStringLiteral("/tor.log"),
    });
    QObject::connect(m_torProc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                     this, [this](int code, QProcess::ExitStatus st) {
        // RECORD it, do not merely log it. Without this m_torExited stayed false forever and
        // getSequencerStatus could never emit "tor-exited", so a crashed Tor read as an eternal
        // "Connecting..." - the exact failure this record exists to end. A deliberate stopTor()
        // sets m_torStopping first, so a shutdown is never reported as a crash.
        if (!m_torStopping) {
            m_torExited   = true;
            m_torExitCode = (st == QProcess::CrashExit) ? -1 : code;
            appendLog(QStringLiteral("bundled Tor exited (code %1)").arg(code), QStringLiteral("error"));
        }
    });
    if (!m_torProc->waitForStarted(3000)) {
        m_torLaunchError = QStringLiteral("Tor failed to start: ") + torBin;
        appendLog(m_torLaunchError, QStringLiteral("error"));
        m_torProc->deleteLater();
        m_torProc = nullptr;
        return;
    }
    // Launch the control-port monitor → real onion-connection stages for the progress bar. This
    // one is a SCRIPT handed to python3, so the same rule applies for the same reason: the file
    // is executed, whatever its exec bit says. The INTERPRETER is a system helper and used to be
    // launched as the bare name "python3" - a $PATH lookup, i.e. arbitrary code execution for
    // anyone who can write an earlier PATH entry. It is resolved to an absolute path in a
    // root-owned directory now, and when there is no such python3 the monitor is simply skipped:
    // the onion progress bar loses its per-stage detail and nothing else changes.
    //
    // This is the ONE python child whose interpreter this module names itself, so it gets `-I`
    // (isolated mode) on top of childEnv(): -E -s -P, i.e. no PYTHON* variables, no per-user
    // site directory, and - the part the environment cannot buy on CPython <= 3.10 - no
    // sys.path[0], so nothing planted BESIDE the monitor script is importable either. The
    // monitor imports only the standard library (binascii, json, os, socket, sys, time), so
    // isolated mode costs it nothing. The wallet CLI cannot be launched this way: it is executed
    // through its own shebang, and see childEnv() for why sniffing it would be worse.
    const QString mon = resolveBin(QStringLiteral("medusa-tor-monitor"), "MEDUSA_TOR_MONITOR");
    const QString py  = resolveSystemBin(QStringLiteral("python3"), "MEDUSA_PYTHON_BIN");
    if (QFileInfo::exists(mon) && !py.isEmpty()) {
        if (m_torMonProc) { m_torMonProc->kill(); m_torMonProc->deleteLater(); }
        m_torMonProc = new QProcess(this);
        m_torMonProc->setProcessChannelMode(QProcess::SeparateChannels);
        startChild(*m_torMonProc, py, { QStringLiteral("-I"), mon, dataDir,
                                        QString::number(kTorControlPort) });
    } else if (QFileInfo::exists(mon)) {
        appendLog(QStringLiteral("no python3 in a trusted system directory - the Tor onion-stage "
                                 "monitor will not run (connection progress stays coarse)"));
    }
}

void WalletPlugin::stopTor()
{
    // Mark the teardown BEFORE reaping: waitForFinished() delivers finished() synchronously, and
    // without this flag a deliberate stop would record itself as a crash and the next status call
    // would report "tor-exited" for a Tor the user asked to stop.
    m_torStopping = true;
    auto reap = [](QProcess*& p) {
        if (!p) return;
        if (p->state() != QProcess::NotRunning) {
            p->terminate();
            if (!p->waitForFinished(2000)) p->kill();
            p->waitForFinished(1000);
        }
        p->deleteLater(); p = nullptr;
    };
    reap(m_torMonProc);
    reap(m_torProc);
    m_torStopping = false;
    m_torExited   = false;   // a stopped Tor is not a crashed one
    m_torExitCode = 0;
}

// An ADOPTED Tor (another wallet window's, reused by ensureTor's single-instance guard) is not
// our child, so QProcess tells us nothing about it. The only honest test that covers both cases
// is whether the SOCKS port still answers.
bool WalletPlugin::torRunning()
{
    if (m_torProc && m_torProc->state() != QProcess::NotRunning)
        return true;
    return tcpPortOpen(quint16(torSocksPort()), 400);
}

// THE one predicate for "does this zone reach its sequencer over Tor". Read off the zone RECORD,
// never off "is it built in": a user-added zone is a Tor zone whenever addZone was given an
// .onion, and the built-in diaphani zone is one by kind. getSequencerStatus, applySequencer's
// teardown and cancelConnect all route through here so they cannot drift apart.
bool WalletPlugin::zoneUsesTor(const QString& id) const
{
    if (zoneKind(id) == QStringLiteral("local-l1-tor"))
        return true;
    return zoneObj(id).value(QStringLiteral("tor")).toBool();
}

// The zone a fresh install starts on, and the zone an aborted connect lands on. netId()'s
// default derives from the same rule, so the two cannot disagree.
QString WalletPlugin::defaultZoneId()
{
    return clearnetUrl().isEmpty() ? QStringLiteral("devnet")
                                   : QStringLiteral("paradox-clearnet");
}

// Abort a connect in flight: the escape hatch from a Tor bootstrap that is taking minutes or has
// already failed. Before this existed the wallet was simply stuck on the connect screen.
//
// UNGATED, deliberately. Every other verb that changes what the wallet does is gated, but this
// one only STOPS things: it spends nothing, reveals nothing, and moves the wallet toward the
// safe default zone. Gating it would mean a locked wallet could not escape a hung bootstrap,
// which is precisely when a user most needs to. It is also idempotent, so a double-tap is safe.
//
// Why it does not simply stay on the abandoned zone: a Tor zone with its tunnel torn down is not
// a resting state. Every op would fail at the transport with nothing to explain it, and the UI
// would show a zone that cannot work. Landing on the default clearnet zone leaves the wallet in
// a state where the next thing the user does can succeed.
QString WalletPlugin::cancelConnect()
{
    const QString zone       = netId();
    const bool    wasTorZone = zoneUsesTor(zone);
    // "Connecting" in the sense the UI means it: the transport is up but the zone has not
    // answered a health check yet. Recorded before the teardown, or the reply would always
    // say false.
    const bool    wasConnecting = wasTorZone ? !m_lastSeqOk : false;

    // An adopted Tor belongs to another wallet window: stopping it would break that window, so
    // report it rather than killing it. Ours is a child of this process and is ours to stop.
    const bool ours = (m_torProc && m_torProc->state() != QProcess::NotRunning);
    const bool torForeign = wasTorZone && !ours && torRunning();
    const bool fwdWasUp   = (m_fwdProc && m_fwdProc->state() != QProcess::NotRunning);

    if (ours) stopTor();
    stopForward();

    m_connectAborted = true;
    m_abortedZone    = zone;
    m_lastSeqOk      = false;

    // Only a Tor zone needs relocating: a clearnet or local zone is perfectly usable after a
    // cancel, so moving the user off it would be gratuitous.
    bool switched = false;
    QString landed = zone;
    if (wasTorZone && zone != defaultZoneId()) {
        const QString target = defaultZoneId();
        QSettings s;
        s.setValue(QLatin1String(kNetworkKey), target);
        applySequencer();          // re-point the wallet; this clears m_connectAborted…
        m_connectAborted = true;   // …so re-assert it: the UI must still be able to say WHY.
        m_abortedZone    = zone;
        landed   = target;
        switched = true;
    }

    appendLog(QStringLiteral("connect aborted on zone %1%2")
                  .arg(zone, switched ? QStringLiteral(" - switched to ") + landed : QString()));

    QJsonObject o;
    o[QStringLiteral("ok")]             = true;
    o[QStringLiteral("zone")]           = landed;
    o[QStringLiteral("abortedZone")]    = zone;
    o[QStringLiteral("switched")]       = switched;
    o[QStringLiteral("wasConnecting")]  = wasConnecting;
    o[QStringLiteral("torStopped")]     = ours;
    o[QStringLiteral("torForeign")]     = torForeign;
    o[QStringLiteral("forwardStopped")] = fwdWasUp;
    return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

// Launch diaphani-forward: maps 127.0.0.1:<listenPort> -> a v3 .onion over the bundled Tor.
// Used for the L1 node (Paradox zone) and remote zones' Tor-fronted sequencers, so an
// unmodified client reaches the hidden endpoint with no code change.
void WalletPlugin::ensureForward(int listenPort, const QString& onion)
{
    if (m_fwdProc && m_fwdProc->state() != QProcess::NotRunning)
        return;
    if (onion.trimmed().isEmpty()) {
        appendLog(QStringLiteral("onion not configured for this zone"), QStringLiteral("error"));
        return;
    }
    // Single-instance guard: if another wallet window already runs a forward on this port,
    // ADOPT it instead of launching a duplicate that can't bind. That duplicate was the
    // recurring failure - a second instance's dead forward read as "disconnected from zone"
    // and broke the first. The forward is a shared tunnel to the same per-zone .onion, so
    // reusing it is safe (mirrors the Tor-reuse probe in ensureTor). m_fwdProc stays null;
    // the async health probe drives the connection status either way.
    // (Same libc port probe as ensureTor: the `bash -c 'exec 3<>/dev/tcp/...'` spawn is gone.)
    if (listenPort > 0 && listenPort < 65536 && tcpPortOpen(quint16(listenPort), 700)) {
        appendLog(QStringLiteral("reusing forward already on 127.0.0.1:%1").arg(listenPort));
        return;
    }
    const QString fwd = resolveBin(QStringLiteral("diaphani-forward"), "MEDUSA_FORWARD_BIN");
    const QString listen = QStringLiteral("127.0.0.1:%1").arg(listenPort);
    // The configured onion may carry a port ("host.onion:3077" - the netcup sequencer onion
    // publishes 3077, not 80). diaphani-forward strictly wants a BARE v3 host in --onion and
    // the port in --onion-port (default 80), so split before handing it over.
    QString onionHost = onion.trimmed(), onionPort;
    const int colon = onionHost.lastIndexOf(QLatin1Char(':'));
    if (colon > 0) { onionPort = onionHost.mid(colon + 1).trimmed(); onionHost = onionHost.left(colon); }
    m_fwdProc = new QProcess(this);
    m_fwdProc->setProcessChannelMode(QProcess::SeparateChannels);
    appendLog(QStringLiteral("diaphani-forward: %1 -> %2:%3 over Tor")
                  .arg(listen, onionHost, onionPort.isEmpty() ? QStringLiteral("80") : onionPort));
    QStringList fargs{ QStringLiteral("--onion"), onionHost,
                       QStringLiteral("--listen"), listen,
                       QStringLiteral("--socks"),
                       QStringLiteral("127.0.0.1:%1").arg(kTorSocksPort) };
    if (!onionPort.isEmpty())
        fargs << QStringLiteral("--onion-port") << onionPort;
    startChild(*m_fwdProc, fwd, fargs);
    if (!m_fwdProc->waitForStarted(3000)) {
        appendLog(QStringLiteral("diaphani-forward failed to start: ") + fwd, QStringLiteral("error"));
        m_fwdProc->deleteLater();
        m_fwdProc = nullptr;
    }
}

void WalletPlugin::stopForward()
{
    if (!m_fwdProc)
        return;
    if (m_fwdProc->state() != QProcess::NotRunning) {
        m_fwdProc->terminate();
        if (!m_fwdProc->waitForFinished(2000))
            m_fwdProc->kill();
        m_fwdProc->waitForFinished(1500);
    }
    m_fwdProc->deleteLater();
    m_fwdProc = nullptr;
}

void WalletPlugin::ensureSequencer()
{
    const QString id   = netId();
    const QString kind = zoneKind(id);

    // REMOTE zone: thin client of someone else's sequencer. Never spawn locally; if the
    // zone is Tor-fronted, bring up the bundled Tor + a tunnel to its sequencer .onion.
    if (kind == QStringLiteral("remote")) {
        const QJsonObject z = zoneObj(id);
        if (z.value(QStringLiteral("tor")).toBool()) {
            ensureTor();
            ensureForward(netPort(), z.value(QStringLiteral("onion")).toString().trimmed());
        }
        return;
    }

    // Paradox zone: a THIN CLIENT of the co-located Paradox sequencer (which runs next to
    // the L1 on prod - so no local sequencer, no backfill-over-Tor). Bundled Tor + a tunnel
    // to the sequencer .onion; the wallet talks to that. Onion: ~/.config override else baked.
    if (kind == QStringLiteral("local-l1-tor")) {
        const QString onion = endpointFromConfig("MEDUSA_SEQ_ONION",
                                                  QStringLiteral("medusa-sequencer.onion"));
        if (onion.isEmpty()) {
            appendLog(QStringLiteral("Paradox · Tor zone needs a sequencer .onion - set "
                "MEDUSA_SEQ_ONION or ~/.config/medusa-sequencer.onion"), QStringLiteral("error"));
            return;   // thin client can't tunnel without an onion
        }
        ensureTor();
        ensureForward(netPort(), onion);   // local forward port -> sequencer onion
        return;                            // thin client: never spawn a local sequencer
    }

    const int port = netPort();
    if (seqHealthy(port)) {   // a sequencer (ours from a prior run, or external) is already up
        appendLog(QStringLiteral("sequencer already reachable on :%1 - not spawning").arg(port));
        return;
    }
    if (m_seqProc)
        return;   // idempotency guard

    const QString cfg = seqHome() + QStringLiteral("/sequencer_config.json");
    writeSeqConfig(cfg);

    // Fresh spawn attempt: the failure record describes THIS attempt, not a previous one.
    m_seqLaunchError.clear();
    m_seqExited = false;
    m_seqExitCode = 0;

    m_seqProc = new QProcess(this);
    // Keep the child's merged output in a per-zone log file so a crash or a bad config is
    // diagnosable (and the UI can point the user at it) instead of dying with the process.
    m_seqProc->setProcessChannelMode(QProcess::MergedChannels);
    m_seqProc->setStandardOutputFile(seqHome() + QStringLiteral("/sequencer.log"),
                                     QIODevice::Truncate);
    QProcessEnvironment env = childEnv();   // inherited env with a $PATH nothing at this uid owns
    // Dev-mode (fake/fast proofs, no verification) ONLY for the local "devnet" sandbox; every real
    // zone (diaphani + user-added remote) must verify real proofs. Matches the wallet's prove mode.
    env.insert(QStringLiteral("RISC0_DEV_MODE"),
               netId() == QStringLiteral("devnet") ? QStringLiteral("1") : QStringLiteral("0"));
    m_seqProc->setProcessEnvironment(env);
    QObject::connect(m_seqProc, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        appendLog(QStringLiteral("sequencer process error: ")
                  + (m_seqProc ? m_seqProc->errorString() : QString()), QStringLiteral("error"));
    });
    // Record an unexpected exit (crash, bad config, port clash) so the status surface can say
    // "the sequencer died" - without this the zone just sat in a silent, eternal "Connecting…".
    QObject::connect(m_seqProc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                     this, [this](int code, QProcess::ExitStatus st) {
        if (m_seqStopping) return;   // deliberate stop/kill, not a crash
        m_seqExited = true;
        m_seqExitCode = (st == QProcess::CrashExit) ? -1 : code;
        appendLog(QStringLiteral("sequencer exited unexpectedly (code %1)").arg(m_seqExitCode),
                  QStringLiteral("error"));
    });

    const QString bin = seqPath();
    appendLog(QStringLiteral("spawning sequencer: %1 --port %2").arg(bin).arg(port));
    startChild(*m_seqProc, bin, { QStringLiteral("--port"), QString::number(port), cfg });
    if (!m_seqProc->waitForStarted(3000)) {
        // Distinguish "no binary on disk" from "binary present but won't exec" - the UI
        // gives different advice for each (reinstall the module vs check the log/perms).
        m_seqLaunchError = QFileInfo::exists(bin)
            ? QStringLiteral("failed to launch ") + bin
              + QStringLiteral(" (") + m_seqProc->errorString() + QStringLiteral(")")
            : QStringLiteral("sequencer binary not found: ") + bin;
        appendLog(QStringLiteral("sequencer failed to start: ") + m_seqLaunchError,
                  QStringLiteral("error"));
        m_seqProc->deleteLater();
        m_seqProc = nullptr;
        return;
    }
    m_seqLaunchedMs = QDateTime::currentMSecsSinceEpoch();   // starts the health grace window
}

void WalletPlugin::stopSequencer()
{
    if (m_seqProc) {
        m_seqStopping = true;   // a deliberate stop must not register as a crash
        if (m_seqProc->state() != QProcess::NotRunning) {
            m_seqProc->terminate();                       // graceful (== Ctrl-C)
            if (!m_seqProc->waitForFinished(3000))
                m_seqProc->kill();
            m_seqProc->waitForFinished(2000);
        }
        m_seqProc->deleteLater();
        m_seqProc = nullptr;
        m_seqStopping = false;
    }
    // The stopped process (and any crash it recorded) is history for the next zone.
    m_seqExited = false;
    m_seqExitCode = 0;
    stopForward();   // tear down the Tor tunnel too (no-op if not running)
}

// Recompute the wallet's sequencer_addr from the active zone, write it into
// wallet_config.json, and (re)launch/stop the local sequencer. Returns the effective addr.
QString WalletPlugin::applySequencer()
{
    const QString id   = netId();
    const QString kind = zoneKind(id);

    // Applying a zone ENDS the "the user stopped the last connect" story: whatever happens from
    // here is this apply's doing. Clearing it here is what makes the record mean "the most recent
    // connect ended because the user stopped it, and nothing has been re-applied since", which is
    // what the header promises. cancelConnect() re-asserts it deliberately after the apply it
    // performs itself, so its own reply and the next status call still say "aborted".
    m_connectAborted = false;
    m_abortedZone.clear();
    // A fresh apply also retires the previous zone's Tor failure record: a Tor that failed for a
    // zone we have left must not keep colouring the reason for the zone we are now on.
    m_torLaunchError.clear();
    m_torExited   = false;
    m_torExitCode = 0;

    // Effective sequencer address for this zone:
    //  - local zones  -> the local sequencer on the zone's port
    //  - remote+tor   -> the local diaphani-forward port (tunnels to the sequencer .onion)
    //  - remote+direct-> the zone's clearnet URL
    QString addr;
    bool overTor = (kind == QStringLiteral("local-l1-tor"));
    if (kind == QStringLiteral("remote")) {
        const QJsonObject z = zoneObj(id);
        if (z.value(QStringLiteral("tor")).toBool()) {
            addr = QStringLiteral("http://127.0.0.1:%1/").arg(netPort());
            overTor = true;
        } else {
            addr = z.value(QStringLiteral("url")).toString().trimmed();
            if (!addr.isEmpty() && !addr.endsWith(QLatin1Char('/'))) addr += QLatin1Char('/');
        }
    } else {
        addr = QStringLiteral("http://127.0.0.1:%1/").arg(netPort());
    }

    // Never persist an empty sequencer_addr. wallet-lez parses it with the `url` crate, so a ""
    // makes EVERY later call fail with "Failed to deserialize wallet config ... relative URL
    // without a base" - not just the connect - and it stays broken until the file is repaired.
    // Leave the previous config untouched and report the misconfiguration instead.
    if (addr.isEmpty()) {
        appendLog(QStringLiteral("zone: %1 (%2) -> no sequencer endpoint configured, config left unchanged")
                      .arg(id, kind));
        return QString();
    }

    // read-merge-write so the wrapper's seq_* tuning keys survive
    const QString cfgp = walletHome() + QStringLiteral("/wallet_config.json");
    QJsonObject cfg;
    { QFile f(cfgp); if (f.open(QIODevice::ReadOnly)) cfg = QJsonDocument::fromJson(f.readAll()).object(); }
    if (overTor) {
        // Tor adds latency per round-trip, but with demand-driven production the tx lands in
        // ~3s, so a FEW polls confirm it - 10×30s just burned time + blew past subprocess caps.
        cfg[QStringLiteral("seq_poll_timeout")]          = QStringLiteral("20s");
        cfg[QStringLiteral("seq_tx_poll_max_blocks")]    = 10;
        cfg[QStringLiteral("seq_poll_max_retries")]      = 3;
        cfg[QStringLiteral("seq_block_poll_max_amount")] = 200;
    } else if (!cfg.contains(QStringLiteral("seq_poll_timeout"))
               || (cfg[QStringLiteral("seq_poll_timeout")].toString() == QStringLiteral("12s")
                   && cfg[QStringLiteral("seq_tx_poll_max_blocks")].toInt() == 5)) {
        // Seed - and migrate the OLD stock values (12s/5): that window was tuned for 15s
        // devnet blocks and reports "Transaction not found in preconfigured amount of
        // blocks" on 60s-block zones for txs that land in the very next block.
        cfg[QStringLiteral("seq_poll_timeout")]          = QStringLiteral("20s");
        cfg[QStringLiteral("seq_tx_poll_max_blocks")]    = 8;
        cfg[QStringLiteral("seq_poll_max_retries")]      = 8;
        cfg[QStringLiteral("seq_block_poll_max_amount")] = 200;
    }
    cfg[QStringLiteral("sequencer_addr")] = addr;
    cfg[QStringLiteral("zone")] = id;   // lets the wrapper scope tokens/registry per zone
    QDir().mkpath(walletHome());
    { QFile f(cfgp); if (f.open(QIODevice::WriteOnly)) f.write(QJsonDocument(cfg).toJson(QJsonDocument::Compact)); }

    // Repoint: kill the old local sequencer/tunnel, then bring up whatever this zone needs.
    stopSequencer();
    m_lastSeqOk = false;   // drop the previous zone's cached health → next poll shows "Connecting" until reconfirmed
    m_zoneCompat = QStringLiteral("unknown");   // the build-compat verdict is per-zone - re-probe
    ensureSequencer();
    appendLog(QStringLiteral("zone: %1 (%2) -> %3").arg(id, kind, addr));
    return addr;
}

QString WalletPlugin::getNetwork() const
{
    QJsonObject o; o[QStringLiteral("network")] = netId();
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

QString WalletPlugin::setNetwork(const QString& network)
{
    return setActiveZone(network);   // back-compat alias
}

// ── Zone API ───────────────────────────────────────────────────────────────────
QString WalletPlugin::getZones() const
{
    QJsonArray out;
    // Built-ins first.
    auto builtin = [](const QString& id, const QString& name, const QString& kind) {
        QJsonObject o;
        o[QStringLiteral("id")] = id; o[QStringLiteral("name")] = name;
        o[QStringLiteral("kind")] = kind; o[QStringLiteral("builtin")] = true;
        o[QStringLiteral("tor")] = (kind == QStringLiteral("local-l1-tor"));
        // The token faucet is a property OF THE ZONE, published on the zone record so the UI
        // reads it off the thing it already has instead of keeping a second list of zone ids.
        // Derived from kFaucetZones, which is why "devnet" and "diaphani" come back false here
        // without either being named: they simply have no row.
        o[QStringLiteral("tokenFaucet")] = (faucetZoneRow(id) != nullptr);
        return o;
    };
    out.append(builtin(QStringLiteral("devnet"),   QStringLiteral("Devnet"),                  QStringLiteral("local-standalone")));
    out.append(builtin(QStringLiteral("diaphani"), QStringLiteral("Paradox Computer"),  QStringLiteral("local-l1-tor")));
    // Same prod sequencer as the Tor zone, reached over clearnet (TLS) - a thin remote client.
    {
        QJsonObject o = builtin(QStringLiteral("paradox-clearnet"),
                                QStringLiteral("Paradox Computer"),
                                QStringLiteral("remote"));
        o[QStringLiteral("endpoint")] = clearnetUrl();
        out.append(o);
    }
    // The official Logos public testnet (logos-co). Same LEZ v0.2.0 engine as this build.
    {
        QJsonObject o = builtin(QStringLiteral("logos-testnet"),
                                QStringLiteral("Logos public testnet"),
                                QStringLiteral("remote"));
        o[QStringLiteral("endpoint")] = logosTestnetUrl();
        out.append(o);
    }
    // User zones.
    for (const auto& v : userZones()) {
        QJsonObject z = v.toObject();
        QJsonObject o;
        o[QStringLiteral("id")]   = z.value(QStringLiteral("id")).toString();
        o[QStringLiteral("name")] = z.value(QStringLiteral("name")).toString();
        o[QStringLiteral("kind")] = QStringLiteral("remote");
        o[QStringLiteral("tor")]  = z.value(QStringLiteral("tor")).toBool();
        o[QStringLiteral("endpoint")] = z.value(QStringLiteral("tor")).toBool()
            ? z.value(QStringLiteral("onion")).toString() : z.value(QStringLiteral("url")).toString();
        o[QStringLiteral("builtin")] = false;
        // Computed, not copied from `z`: see zoneObj(). A user zone is somebody else's
        // sequencer, so it has no row in kFaucetZones and this is always false - but it is
        // false because the TABLE says so, not because the stored record was trusted.
        o[QStringLiteral("tokenFaucet")] =
            (faucetZoneRow(z.value(QStringLiteral("id")).toString()) != nullptr);
        out.append(o);
    }
    QJsonObject res;
    res[QStringLiteral("zones")]  = out;
    res[QStringLiteral("active")] = netId();
    return QJsonDocument(res).toJson(QJsonDocument::Compact);
}

// One endpoint argument in, the (url, onion) pair the zone record stores out. Shared by
// addZone and editZone, which had this validation twice and now cannot drift.
bool WalletPlugin::normalizeZoneEndpoint(const QString& endpoint, bool tor, QString* url,
                                         QString* onion, QString* error)
{
    const auto fail = [&](const QString& msg) {
        if (error) *error = msg;
        return false;
    };
    if (url)   url->clear();
    if (onion) onion->clear();
    if (error) error->clear();

    const QString ep = endpoint.trimmed();
    if (tor) {
        if (ep.isEmpty() || !ep.contains(QStringLiteral(".onion")))
            return fail(QStringLiteral("a Tor zone needs a valid .onion address"));
        if (onion) *onion = ep;
        return true;
    }
    // Normalize + validate the clearnet URL so a dead zone can't be created silently.
    if (ep.isEmpty())
        return fail(QStringLiteral("a clearnet zone needs a sequencer URL"));
    QString u = ep;
    if (!u.contains(QStringLiteral("://"))) u = QStringLiteral("http://") + u;
    const QUrl qu(u, QUrl::StrictMode);
    const QString sch = qu.scheme().toLower();
    if (!qu.isValid() || qu.host().isEmpty()
        || (sch != QStringLiteral("http") && sch != QStringLiteral("https")))
        return fail(QStringLiteral("enter a full sequencer URL, e.g. https://host:3072/"));
    if (qu.host().endsWith(QStringLiteral(".onion")))
        return fail(QStringLiteral("a .onion address requires the Tor transport"));
    if (url) *url = u;
    return true;
}

QString WalletPlugin::addZone(const QString& name, const QString& endpoint, bool tor)
{
    const QString nm = name.trimmed();
    if (nm.isEmpty()) return errorJson(QStringLiteral("name is required"));
    QString cleanUrl, cleanOnion, epErr;
    if (!normalizeZoneEndpoint(endpoint, tor, &cleanUrl, &cleanOnion, &epErr))
        return errorJson(epErr);
    QSettings s;
    QJsonArray arr = userZones();
    // id = slug of name + a short disambiguator
    QString base = nm.toLower();
    base.replace(QRegularExpression(QStringLiteral("[^a-z0-9]+")), QStringLiteral("-"));
    base = base.mid(0, 24);
    QString id = QStringLiteral("z-") + base;
    int n = 1; while (!zoneObj(id).isEmpty()) id = QStringLiteral("z-%1-%2").arg(base).arg(++n);
    QJsonObject z;
    z[QStringLiteral("id")] = id; z[QStringLiteral("name")] = nm;
    z[QStringLiteral("url")] = cleanUrl; z[QStringLiteral("onion")] = cleanOnion;
    z[QStringLiteral("tor")] = tor;
    arr.append(z);
    s.setValue(QLatin1String(kZonesKey), QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact)));
    s.sync();
    QJsonObject o; o[QStringLiteral("ok")] = true; o[QStringLiteral("id")] = id;
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

QString WalletPlugin::editZone(const QString& id, const QString& name,
                               const QString& endpoint, bool tor)
{
    if (!isUserZone(id))
        return errorJson(QStringLiteral("only user-added zones can be edited"));
    const QString nm = name.trimmed();
    if (nm.isEmpty()) return errorJson(QStringLiteral("name is required"));
    QString cleanUrl, cleanOnion, epErr;
    if (!normalizeZoneEndpoint(endpoint, tor, &cleanUrl, &cleanOnion, &epErr))
        return errorJson(epErr);
    QSettings s;
    QJsonArray arr = userZones(), out;
    for (const auto& v : arr) {
        QJsonObject o = v.toObject();
        if (o.value(QStringLiteral("id")).toString() == id) {
            o[QStringLiteral("name")]  = nm;
            o[QStringLiteral("url")]   = cleanUrl;
            o[QStringLiteral("onion")] = cleanOnion;
            o[QStringLiteral("tor")]   = tor;
        }
        out.append(o);
    }
    s.setValue(QLatin1String(kZonesKey), QString::fromUtf8(QJsonDocument(out).toJson(QJsonDocument::Compact)));
    s.sync();
    if (netId() == id) applySequencer();   // editing the active zone → repoint live
    return okJson();
}

QString WalletPlugin::removeZone(const QString& id)
{
    if (!isUserZone(id))
        return errorJson(QStringLiteral("not a removable (user) zone"));
    QSettings s;
    QJsonArray arr = userZones(), keep;
    for (const auto& v : arr)
        if (v.toObject().value(QStringLiteral("id")).toString() != id) keep.append(v);
    s.setValue(QLatin1String(kZonesKey), QString::fromUtf8(QJsonDocument(keep).toJson(QJsonDocument::Compact)));
    s.sync();
    if (netId() == id) setActiveZone(QStringLiteral("devnet"));   // fall back if the active one was removed
    return okJson();
}

QString WalletPlugin::setActiveZone(const QString& id)
{
    const QString z = id.trimmed();
    const bool known = (z == QStringLiteral("devnet") || z == QStringLiteral("diaphani"))
                       || !zoneObj(z).isEmpty();
    if (!known) return errorJson(QStringLiteral("unknown zone: ") + z);
    QSettings s; s.setValue(QLatin1String(kNetworkKey), z); s.sync();
    applySequencer();   // repoint + relaunch/stop for the new zone
    return okJson();
}

QString WalletPlugin::getSequencerConfig() const
{
    QSettings s;
    QJsonObject o;
    o[QStringLiteral("mode")]    = s.value(QLatin1String(kSeqModeKey), QStringLiteral("local")).toString();
    o[QStringLiteral("url")]     = s.value(QLatin1String(kSeqUrlKey)).toString();
    o[QStringLiteral("network")] = netId();
    o[QStringLiteral("port")]    = netPort();
    // Same treatment as cliPath in getConfig(): the key is read HERE and nowhere else. An install
    // poisoned before the override was removed still shows what was planted - the only visible
    // trace it was ever there - and it is never executed, whatever it points at.
    const QString storedSeq = s.value(QLatin1String(kSeqPathKey)).toString();
    o[QStringLiteral("seqPath")]           = storedSeq;
    o[QStringLiteral("seqPathEff")]        = seqPath();
    o[QStringLiteral("seqPathIgnored")]    = !storedSeq.trimmed().isEmpty();
    o[QStringLiteral("seqPathConfigurable")] = false;
    QFile f(walletHome() + QStringLiteral("/wallet_config.json"));
    if (f.open(QIODevice::ReadOnly))
        o[QStringLiteral("effectiveAddr")] =
            QJsonDocument::fromJson(f.readAll()).object().value(QStringLiteral("sequencer_addr")).toString();
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

QString WalletPlugin::setSequencerConfig(const QString& mode, const QString& url, int /*port*/)
{
    QString m = mode.trimmed().toLower();
    if (m != QStringLiteral("local") && m != QStringLiteral("hosted"))
        return errorJson(QStringLiteral("mode must be local|hosted"));
    QSettings s;
    s.setValue(QLatin1String(kSeqModeKey), m);
    s.setValue(QLatin1String(kSeqUrlKey), url.trimmed());
    s.sync();
    applySequencer();   // local/hosted is independent of which network is selected
    return okJson();
}

// Parse the bundled Tor's log for the latest "Bootstrapped NN% (tag): description" so the
// UI can show a real connect progress bar. Returns {percent, stage}.
QString WalletPlugin::getTorProgress() const
{
    int pct = 0;
    QString stage;
    QFile f(walletHome() + QStringLiteral("/tor/tor.log"));
    if (f.open(QIODevice::ReadOnly)) {
        const qint64 sz = f.size();
        if (sz > 32768) f.seek(sz - 32768);   // only the tail matters
        const QString txt = QString::fromUtf8(f.readAll());
        QRegularExpression re(QStringLiteral("Bootstrapped (\\d+)%(?: \\(([^)]+)\\))?: ([^\\r\\n]+)"));
        QRegularExpressionMatchIterator it = re.globalMatch(txt);
        QRegularExpressionMatch last;
        while (it.hasNext()) last = it.next();
        if (last.hasMatch()) { pct = last.captured(1).toInt(); stage = last.captured(3).trimmed(); }
    }
    // Onion-connection stage (post-bootstrap), from the control-port monitor.
    QString onionStage; int onionPct = 0;
    { QFile of(walletHome() + QStringLiteral("/tor/onion-stage.json"));
      if (of.open(QIODevice::ReadOnly)) {
          const QJsonObject oo = QJsonDocument::fromJson(of.readAll()).object();
          onionStage = oo.value(QStringLiteral("stage")).toString();
          onionPct   = oo.value(QStringLiteral("pct")).toInt();
      } }
    QJsonObject o;
    o[QStringLiteral("percent")]    = pct;
    o[QStringLiteral("stage")]      = stage;
    o[QStringLiteral("onionStage")] = onionStage;
    o[QStringLiteral("onionPct")]   = onionPct;
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

// Fire-and-forget checkHealth probe (8s budget - fine over Tor). Updates m_lastSeqOk on
// completion; never blocks the caller, so the status dot can't freeze or 1s-time-out.
void WalletPlugin::probeSeqHealthAsync(const QString& url)
{
    if (url.trimmed().isEmpty()) { m_lastSeqOk = false; return; }
    if (m_healthProbe && m_healthProbe->state() != QProcess::NotRunning)
        return;   // one probe at a time
    // THE trigger surface of F1: this runs off a 10-second UI timer from app start, before any
    // password exists, on every zone kind. It used to launch the bare name "curl".
    const QString curl = curlPath();
    if (curl.isEmpty()) { m_lastSeqOk = false; return; }
    if (m_healthProbe) { m_healthProbe->deleteLater(); m_healthProbe = nullptr; }
    m_healthProbe = new QProcess(this);
    QProcess* p = m_healthProbe;
    QObject::connect(p, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
        [this, p](int, QProcess::ExitStatus) {
            m_lastSeqOk = QString::fromUtf8(p->readAllStandardOutput()).contains(QStringLiteral("\"result\""));
            p->deleteLater();
            if (m_healthProbe == p) m_healthProbe = nullptr;
        });
    startChild(*p, curl, {
        QStringLiteral("-s"), QStringLiteral("--max-time"), QStringLiteral("8"),
        QStringLiteral("-X"), QStringLiteral("POST"),
        QStringLiteral("-H"), QStringLiteral("content-type: application/json"),
        QStringLiteral("-d"),
        QStringLiteral(R"({"jsonrpc":"2.0","id":1,"method":"checkHealth","params":[]})"),
        url });
}

// Async zone/build compatibility probe: `wallet check-health` exits 0 when the wallet's
// builtin program ImageIDs match the zone's (getProgramIds), and panics with
// "... is different from remote" on a mismatch. A stale bundled sequencer under a newer
// wallet still ANSWERS checkHealth, so without this probe a fundamentally unusable zone
// reads as connected. Transport / locked-storage failures stay "unknown" - never a false
// "mismatch". One probe at a time; the verdict is cached until the next zone (re)apply.
void WalletPlugin::probeZoneCompatAsync()
{
    if (m_compatProbe && m_compatProbe->state() != QProcess::NotRunning)
        return;   // one probe at a time
    if (m_compatProbe) { m_compatProbe->deleteLater(); m_compatProbe = nullptr; }
    // Never run the CLI before a wallet store exists - some verbs auto-create storage.
    if (!QFile::exists(walletHome() + QStringLiteral("/storage.json")))
        return;
    // A locked wallet can't answer the CLI's password prompt: the verdict would stay
    // "unknown" and the 10s status poll would respawn a check-health (network round-trip
    // + Argon2id) forever behind the lock screen. Probe once the wallet is unlocked.
    if (m_password.isEmpty())
        return;
    QProcess* p = new QProcess(this);
    m_compatProbe = p;
    p->setProcessChannelMode(QProcess::MergedChannels);   // the mismatch panic lands on stderr
    QObject::connect(p, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
        [this, p](int code, QProcess::ExitStatus) {
            const QString out = QString::fromUtf8(p->readAllStandardOutput());
            if (code == 0) {
                m_zoneCompat = QStringLiteral("ok");
            } else if (out.contains(QStringLiteral("different from remote"))
                       || out.contains(QStringLiteral("program ids differ"))) {
                m_zoneCompat = QStringLiteral("mismatch");
                appendLog(QStringLiteral("zone/build mismatch: wallet program ids differ from this zone"),
                          QStringLiteral("error"));
            }   // anything else: keep "unknown" (transport, locked wallet, missing CLI)
            p->deleteLater();
            if (m_compatProbe == p) m_compatProbe = nullptr;
        });
    startChild(*p, cliPath(), { QStringLiteral("check-health") });
    if (!p->waitForStarted(3000)) {
        p->deleteLater();
        if (m_compatProbe == p) m_compatProbe = nullptr;
        return;
    }
    p->write((m_password + QStringLiteral("\n")).toUtf8());
    p->closeWriteChannel();
    // Safety kill: a wedged CLI must not block every future probe (one-at-a-time guard).
    QTimer::singleShot(90000, p, [p]() {
        if (p->state() != QProcess::NotRunning) p->kill();
    });
}

QString WalletPlugin::getSequencerStatus()
{
    const QString id   = netId();
    const QString kind = zoneKind(id);
    const int port = netPort();
    QString state;

    // The endpoint the wallet actually talks to (what we probe in every case).
    QString eff;
    { QFile f(walletHome() + QStringLiteral("/wallet_config.json"));
      if (f.open(QIODevice::ReadOnly))
          eff = QJsonDocument::fromJson(f.readAll()).object().value(QStringLiteral("sequencer_addr")).toString(); }

    const bool torZone = (kind == QStringLiteral("local-l1-tor"))
                      || (kind == QStringLiteral("remote") && zoneObj(id).value(QStringLiteral("tor")).toBool());

    bool healthy = false;
    if (torZone) {
        // Thin client over Tor: a 1s probe would always time out, so probe ASYNC (cached)
        // and report green/gray/red from the cached result + the tunnel state.
        probeSeqHealthAsync(eff);
        const bool fwdUp = m_fwdProc && m_fwdProc->state() != QProcess::NotRunning;
        healthy = m_lastSeqOk;
        state = healthy ? QStringLiteral("running")
              : (fwdUp ? QStringLiteral("starting") : QStringLiteral("unreachable"));
    } else if (kind == QStringLiteral("remote")) {
        // Remote clearnet: probe ASYNC + cached. A sync 1s probe always times out on a real
        // over-the-internet HTTPS round-trip (~2s; the TLS handshake alone is ~1.3s), and a
        // longer SYNC probe would freeze the 10s UI poll. Show "starting" (→ "Connecting…")
        // while the first probe is in flight, then green/red from the cached result.
        probeSeqHealthAsync(eff);
        const bool probing = m_healthProbe && m_healthProbe->state() != QProcess::NotRunning;
        healthy = m_lastSeqOk;
        state = healthy ? QStringLiteral("running")
              : (probing ? QStringLiteral("starting") : QStringLiteral("unreachable"));
    } else {
        // Local zone (devnet): a process we own + its checkHealth on 127.0.0.1:port.
        const bool procUp = m_seqProc && m_seqProc->state() != QProcess::NotRunning;
        healthy = seqHealthy(port);
        state = healthy ? QStringLiteral("running")
              : (procUp ? QStringLiteral("starting") : QStringLiteral("unreachable"));
    }
    // Once the zone answers, confirm the wallet build actually matches it (async, cached).
    if (healthy && m_zoneCompat == QStringLiteral("unknown"))
        probeZoneCompatAsync();

    QJsonObject o;
    o[QStringLiteral("state")] = state;
    o[QStringLiteral("mode")]  = kind;
    o[QStringLiteral("port")]  = port;
    // What the wallet actually dials + the health/compat verdicts, for every zone kind:
    // the UI names the endpoint in its offline modal and must never guess it.
    o[QStringLiteral("endpoint")] = eff;
    o[QStringLiteral("healthy")]  = healthy;
    o[QStringLiteral("compat")]   = m_zoneCompat;   // "unknown" | "ok" | "mismatch"
    // For a local zone (devnet), whether the sequencer binary is actually on disk - if not,
    // it can never spawn, so the UI shows a "you need a local sequencer" disclaimer instead
    // of an endless "Connecting…". (Remote/Tor zones don't run a local sequencer.)
    if (kind == QStringLiteral("local-standalone")) {
        const QFileInfo si(seqPath());
        const bool binMissing = !(si.exists() && si.isFile());
        o[QStringLiteral("binaryAvailable")] = !binMissing;
        o[QStringLiteral("binaryPath")]      = seqPath();
        const bool procUp = m_seqProc && m_seqProc->state() != QProcess::NotRunning;
        o[QStringLiteral("running")]         = procUp;
        o[QStringLiteral("lastLaunchError")] = m_seqLaunchError;
        if (m_seqExited)
            o[QStringLiteral("exitCode")] = m_seqExitCode;
        const QString logp = seqHome() + QStringLiteral("/sequencer.log");
        if (QFile::exists(logp))
            o[QStringLiteral("logPath")] = logp;
        // One machine-readable reason for the UI's banner / offline modal. Empty while the
        // just-spawned sequencer is inside its launch grace window (it needs a few seconds
        // to open its port) or when nothing is wrong. Precedence: a build mismatch beats
        // everything (the zone ANSWERS, so "offline" wording would mislead); then the
        // states that can never self-heal (no binary / spawn failed / crashed).
        QString reason;
        const qint64 sinceMs = QDateTime::currentMSecsSinceEpoch()
                             - qMax(m_seqLaunchedMs, m_bornMs);
        if (m_zoneCompat == QStringLiteral("mismatch")) {
            reason = QStringLiteral("mismatch");
        } else if (!healthy) {
            if (binMissing)                       reason = QStringLiteral("binary-missing");
            else if (!m_seqLaunchError.isEmpty()) reason = QStringLiteral("launch-failed");
            else if (m_seqExited)                 reason = QStringLiteral("exited");
            else if (sinceMs < kSeqLaunchGraceMs) reason.clear();        // still starting
            else                                  reason = QStringLiteral("unhealthy");
        }
        o[QStringLiteral("reason")] = reason;
    }
    // Tor/onion zone: whether a usable Tor binary (bundled medusa-tor OR a system tor) exists -
    // else the wallet can't reach the zone, so the UI shows a "no Tor found" disclaimer.
    if (torZone) {
        const QString tb = resolveTorBin();
        o[QStringLiteral("needsTor")]     = true;
        o[QStringLiteral("torAvailable")] = !tb.isEmpty();
        o[QStringLiteral("torPath")]      = tb;
        // The transport, reported rather than inferred: the forward is a SEPARATE process that
        // outlives Tor and dials lazily, so "the forward is up" alone kept reading as connected.
        const bool fwdUp = (m_fwdProc && m_fwdProc->state() != QProcess::NotRunning);
        o[QStringLiteral("torRunning")]     = torRunning();
        o[QStringLiteral("forwardRunning")] = fwdUp;
        // A Tor zone gets the same machine-readable `reason` a local zone does. Without it a
        // crashed or never-started Tor reported state "starting" with nothing to explain it, and
        // sat there forever: the eternal silent "Connecting..." this record exists to end.
        //
        // Deliberately NO time-based "unhealthy" here: a bootstrap legitimately takes minutes,
        // and calling a slow connect a failure is how this UI learned to lie.
        QString treason;
        if (m_zoneCompat == QStringLiteral("mismatch"))  treason = QStringLiteral("mismatch");
        else if (m_connectAborted && m_abortedZone == id) treason = QStringLiteral("aborted");
        else if (tb.isEmpty())                           treason = QStringLiteral("tor-missing");
        else if (!m_torLaunchError.isEmpty())            treason = QStringLiteral("tor-failed");
        else if (m_torExited)                            treason = QStringLiteral("tor-exited");
        o[QStringLiteral("reason")] = treason;
    }
    // Carried on EVERY reply, not just a Tor one: after a cancel the wallet may have landed on a
    // clearnet zone, and the caller still has to be able to tell "the user stopped it" from "it
    // went quiet". Cleared by the next applySequencer().
    o[QStringLiteral("connectAborted")] = m_connectAborted;
    if (m_connectAborted)
        o[QStringLiteral("abortedZone")] = m_abortedZone;
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

// ── Account management ────────────────────────────────────────────────────────

// Background `account list -l` (fetches balances from the chain over Tor). Updates
// m_balanceCacheJson on success. Never blocks the caller - listAccounts stays instant.
void WalletPlugin::fetchBalancesAsync()
{
    if (m_acctFetchProc && m_acctFetchProc->state() != QProcess::NotRunning)
        return;   // one fetch at a time
    if (m_acctFetchProc) { m_acctFetchProc->deleteLater(); m_acctFetchProc = nullptr; }
    QProcess* p = new QProcess(this);
    m_acctFetchProc = p;
    p->setProcessChannelMode(QProcess::SeparateChannels);
    QObject::connect(p, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
        [this, p](int, QProcess::ExitStatus) {
            const QByteArray out = p->readAllStandardOutput();
            const QJsonDocument d = QJsonDocument::fromJson(out);
            if (d.isArray() && !d.array().isEmpty())   // only cache a real, non-empty result
                m_balanceCacheJson = QString::fromUtf8(out);
            if (m_acctFetchProc == p) m_acctFetchProc = nullptr;
            p->deleteLater();
        });
    startChild(*p, cliPath(), { QStringLiteral("account"), QStringLiteral("list"),
                                QStringLiteral("-l") });
    if (p->waitForStarted(2000)) {
        p->write((m_password + QStringLiteral("\n")).toUtf8());
        p->closeWriteChannel();
    } else {
        if (m_acctFetchProc == p) m_acctFetchProc = nullptr;
        p->deleteLater();
    }
}

QString WalletPlugin::listAccounts()
{
    // NON-BLOCKING: a LOCAL list (no -l) never reaches the chain, so it can't freeze the UI
    // over Tor. Balances are merged from m_balanceCacheJson (refreshed in the background) and
    // a fresh fetch is kicked off. This is what stops the "blocking read over Tor → crash".
    const QString raw = runWalletCommand({ QStringLiteral("account"), QStringLiteral("list") });
    const QJsonDocument doc = QJsonDocument::fromJson(raw.toUtf8());
    if (!doc.isArray())
        return raw;   // an error object (e.g. wrong password) - pass through untouched

    // Build a balance/init map from the cached `-l` result.
    QHash<QString, QJsonObject> byId;
    for (const auto& v : QJsonDocument::fromJson(m_balanceCacheJson.toUtf8()).array()) {
        const QJsonObject o = v.toObject();
        byId.insert(o.value(QStringLiteral("id")).toString(), o);
    }
    QSettings s;
    const QJsonObject names = QJsonDocument::fromJson(
        s.value(QStringLiteral("medusa-wallet/accountNames")).toString().toUtf8()).object();
    QJsonArray out;
    for (const auto& v : doc.array()) {
        QJsonObject o = v.toObject();
        const QString id = o.value(QStringLiteral("id")).toString();
        if (byId.contains(id)) {
            const QJsonObject c = byId.value(id);
            o[QStringLiteral("balance")]     = c.value(QStringLiteral("balance"));
            o[QStringLiteral("initialized")] = c.value(QStringLiteral("initialized"));
        } else {
            o[QStringLiteral("balance")] = QStringLiteral("…");   // not fetched yet
        }
        const QString user = names.value(id).toString();
        o[QStringLiteral("name")] = !user.isEmpty() ? user : o.value(QStringLiteral("label")).toString();
        out.append(o);
    }
    fetchBalancesAsync();   // refresh balances in the background (cache updates for next call)
    return QJsonDocument(out).toJson(QJsonDocument::Compact);
}

QString WalletPlugin::setAccountName(const QString& accountId, const QString& name)
{
    if (accountId.trimmed().isEmpty())
        return errorJson(QStringLiteral("account id is required"));
    QSettings s;
    QJsonObject names = QJsonDocument::fromJson(
        s.value(QStringLiteral("medusa-wallet/accountNames")).toString().toUtf8()).object();
    const QString nm = name.trimmed();
    if (nm.isEmpty()) names.remove(accountId);
    else             names[accountId] = nm;
    s.setValue(QStringLiteral("medusa-wallet/accountNames"),
               QString::fromUtf8(QJsonDocument(names).toJson(QJsonDocument::Compact)));
    s.sync();
    return okJson();
}

QString WalletPlugin::getTokens(const QString& accountId)
{
    if (accountId.trimmed().isEmpty())
        return QStringLiteral("[]");
    // Wrapper verb: probes every registered token definition with `ata list` and
    // returns [{definitionId, ticker, balance}] for the holdings this account has.
    return runWalletCommand({ QStringLiteral("tokens"), accountId.trimmed() }, 45000);
}

QString WalletPlugin::getDirectHoldings()
{
    // Scans every owned public account on-chain (one account-get each) - allow for a slow
    // zone. The wrapper returns [] on any failure, so callers can treat this as best-effort.
    return runWalletCommand({ QStringLiteral("direct-holdings") }, 120000);
}

QString WalletPlugin::getTokenRegistry()
{
    return runWalletCommand({ QStringLiteral("token-registry") }, 20000);
}

QString WalletPlugin::consolidateToken(const QString& accountId, const QString& definitionId,
                                       const QString& password)
{
    // WALLET-UI ONLY on-chain write (sweeps the user's own token ATAs into their vault).
    // NOT part of the Connect op surface (requestAction only dispatches send/shield/deshield/
    // private, each user-approved) - do NOT add it there without an approval sheet. The old
    // "is the wallet unlocked" test was the file's only gate; it is now the full password gate,
    // because unlocked-ness is not proof that this caller is the user.
    if (!authorize(password))
        return authRefusal();
    if (accountId.trimmed().isEmpty() || definitionId.trimmed().isEmpty())
        return errorJson(QStringLiteral("account and definitionId are required"));
    // Worst-case wrapper path on a slow zone: ATA read + ata send confirm + landing poll.
    return runWalletCommand({ QStringLiteral("consolidate"), accountId.trimmed(),
                              definitionId.trimmed() }, 600000);
}

QString WalletPlugin::addToken(const QString& definitionId)
{
    if (definitionId.trimmed().isEmpty())
        return errorJson(QStringLiteral("definitionId is required"));
    return runWalletCommand({
        QStringLiteral("token-registry"), QStringLiteral("add"), definitionId.trimmed()
    });
}

// The curated tokens of the ACTIVE zone: [{name,def}], empty on a zone with no token faucet.
//
// This used to shell out to the wrapper's `whitelist` verb, which read
// ~/.local/share/medusa-treasury/faucet_tokens-<zone>.json. That file is operator state that
// lives outside the install: deleting the treasury directory (or resetting the wallet, or
// installing on a second machine) emptied the list, and with it the Add-token picker, with no
// error anywhere. The definitions are a fixed property of the zone, so they are read from
// kFaucetZones now and the file is gone from the path entirely.
QString WalletPlugin::getWhitelist()
{
    QJsonArray out;
    if (const FaucetZoneRow* row = faucetZoneRow(netId())) {
        for (const FaucetToken& t : row->tokens) {
            QJsonObject o;
            o[QStringLiteral("name")] = QString::fromLatin1(t.ticker);
            o[QStringLiteral("def")]  = QString::fromLatin1(t.definition);
            out.append(o);
        }
    }
    return QJsonDocument(out).toJson(QJsonDocument::Compact);
}

QString WalletPlugin::getBalance(const QString& accountId)
{
    if (accountId.trimmed().isEmpty())
        return errorJson(QStringLiteral("accountId is required"));

    return runWalletCommand({
        QStringLiteral("account"),
        QStringLiteral("get"),
        QStringLiteral("--account-id"),
        accountId.trimmed()
    });
}

// Defined below (after createAccount); forward-declared so createAccount can parse
// the new account id out of the CLI/wrapper output.
static void enrichFromOutput(const QString& result, QJsonObject& into);

QString WalletPlugin::createAccount()
{
    const QString result = runWalletCommand({
        QStringLiteral("account"), QStringLiteral("new"), QStringLiteral("public")
    });
    const QJsonObject o = QJsonDocument::fromJson(result.toUtf8()).object();
    if (o.contains(QStringLiteral("error")))
        return result;

    QJsonObject out;
    out[QStringLiteral("ok")] = true;
    // The CLI may return a structured {"id":…} directly, or the wrapper may fold a
    // human "…account_id Public/<id>…" line into {"output":…}. Handle both.
    if (o.contains(QStringLiteral("id")))
        out[QStringLiteral("id")] = o.value(QStringLiteral("id"));
    enrichFromOutput(result, out);   // "account_id Public/<id>" (registers on-chain lazily on first faucet)
    return QJsonDocument(out).toJson(QJsonDocument::Compact);
}

QString WalletPlugin::initAccount(const QString& accountId)
{
    if (accountId.trimmed().isEmpty())
        return errorJson(QStringLiteral("accountId is required"));

    return runWalletCommand({
        QStringLiteral("auth-transfer"),
        QStringLiteral("init"),
        QStringLiteral("--account-id"),
        accountId.trimmed()
    });
}

QString WalletPlugin::ensureInitialized(const QString& accountId)
{
    const QString id = accountId.trimmed();
    if (id.isEmpty())
        return errorJson(QStringLiteral("accountId is required"));

    // A never-registered account reads back from the chain as the default state, which
    // `account get` reports as "Account is Uninitialized". Only register when needed -
    // auth-transfer init is NOT idempotent (re-initialising a live account fails).
    const QString getRes = runWalletCommand({
        QStringLiteral("account"), QStringLiteral("get"),
        QStringLiteral("--account-id"), id
    });
    const QString out = QJsonDocument::fromJson(getRes.toUtf8())
                            .object().value(QStringLiteral("output")).toString();
    if (out.contains(QStringLiteral("Uninitialized"), Qt::CaseInsensitive))
        return initAccount(id);
    return okJson();
}

// ── Private account management ─────────────────────────────────────────────────

// Parse the human-readable fields the wallet CLI prints (and the wrapper folds
// into {"output":…}) into the supplied object: "account_id Private/<id>",
// "npk <hex>", "vpk <hex>", "pk <hex>".
static void enrichFromOutput(const QString& result, QJsonObject& into)
{
    QJsonObject o = QJsonDocument::fromJson(result.toUtf8()).object();
    const QString text = o.value(QStringLiteral("output")).toString();
    if (text.isEmpty())
        return;

    auto grab = [&](const QString& pattern, const QString& field) {
        QRegularExpression re(pattern);
        QRegularExpressionMatch m = re.match(text);
        if (m.hasMatch())
            into[field] = m.captured(1);
    };
    grab(QStringLiteral("account_id\\s+((?:Public|Private)/\\S+)"), QStringLiteral("id"));
    grab(QStringLiteral("\\bnpk\\s+([0-9a-fA-F]+)"),                 QStringLiteral("npk"));
    grab(QStringLiteral("\\bvpk\\s+([0-9a-fA-F]+)"),                 QStringLiteral("vpk"));
    grab(QStringLiteral("\\bpk\\s+([0-9a-fA-F]+)"),                  QStringLiteral("pk"));
}

QString WalletPlugin::createPrivateAccount(const QString& label)
{
    QStringList args{ QStringLiteral("account"), QStringLiteral("new"), QStringLiteral("private") };
    if (!label.trimmed().isEmpty())
        args << QStringLiteral("--label") << label.trimmed();

    QString result = runWalletCommand(args);
    QJsonObject o = QJsonDocument::fromJson(result.toUtf8()).object();
    if (o.contains(QStringLiteral("error")))
        return result;

    QJsonObject out;
    out[QStringLiteral("ok")] = true;
    enrichFromOutput(result, out);   // surfaces id / npk / vpk when present
    return QJsonDocument(out).toJson(QJsonDocument::Compact);
}

QString WalletPlugin::createPrivateReceiveKey()
{
    QString result = runWalletCommand({
        QStringLiteral("account"), QStringLiteral("new"),
        QStringLiteral("private-accounts-key")
    });
    QJsonObject o = QJsonDocument::fromJson(result.toUtf8()).object();
    if (o.contains(QStringLiteral("error")))
        return result;

    QJsonObject out;
    out[QStringLiteral("ok")] = true;
    enrichFromOutput(result, out);   // npk / vpk
    return QJsonDocument(out).toJson(QJsonDocument::Compact);
}

QString WalletPlugin::syncPrivate()
{
    // Block scan + decrypt - a FIRST full sync of a mature chain takes minutes (353s
    // measured on ~2700 blocks), so the window must cover it; prefer startSyncPrivate()
    // for anything user-facing.
    return runWalletCommand({
        QStringLiteral("account"), QStringLiteral("sync-private")
    }, 900000);
}

// Background `account sync-private` - never blocks the caller, so a slow scan over Tor on a
// loaded box can't freeze the UI (which the host watchdog would kill). Poll syncPrivateStatus().
QString WalletPlugin::startSyncPrivate()
{
    if (m_syncRunning)
        return QStringLiteral("{\"ok\":true,\"alreadyRunning\":true}");
    if (m_syncProc) { m_syncProc->deleteLater(); m_syncProc = nullptr; }
    m_syncErr.clear();
    QProcess* p = new QProcess(this);
    m_syncProc = p;
    m_syncRunning = true;
    p->setProcessChannelMode(QProcess::SeparateChannels);
    QObject::connect(p, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
        [this, p](int code, QProcess::ExitStatus) {
            const QByteArray out = p->readAllStandardOutput();
            const QByteArray err = p->readAllStandardError();
            const QJsonObject o = QJsonDocument::fromJson(out).object();
            if (o.contains(QStringLiteral("error")))
                m_syncErr = o.value(QStringLiteral("error")).toString();
            else if (code != 0)
                m_syncErr = cleanStderr(QString::fromUtf8(err.isEmpty() ? out : err));
            m_syncRunning = false;
            if (m_syncProc == p) m_syncProc = nullptr;
            p->deleteLater();
        });
    startChild(*p, cliPath(), { QStringLiteral("account"), QStringLiteral("sync-private") });
    if (p->waitForStarted(2000)) {
        p->write((m_password + QStringLiteral("\n")).toUtf8());
        p->closeWriteChannel();
    } else {
        m_syncRunning = false;
        if (m_syncProc == p) m_syncProc = nullptr;
        p->deleteLater();
        return errorJson(QStringLiteral("could not start sync-private"));
    }
    return QStringLiteral("{\"ok\":true}");
}

QString WalletPlugin::syncPrivateStatus()
{
    QJsonObject o;
    o[QStringLiteral("running")] = m_syncRunning;
    o[QStringLiteral("error")]   = m_syncErr;
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

QString WalletPlugin::getAccountKeys(const QString& accountId)
{
    if (accountId.trimmed().isEmpty())
        return errorJson(QStringLiteral("accountId is required"));

    QString result = runWalletCommand({
        QStringLiteral("account"), QStringLiteral("get"),
        QStringLiteral("--account-id"), accountId.trimmed(),
        QStringLiteral("--keys")
    });
    QJsonObject o = QJsonDocument::fromJson(result.toUtf8()).object();
    if (o.contains(QStringLiteral("error")))
        return result;

    QJsonObject out;
    out[QStringLiteral("ok")]        = true;
    out[QStringLiteral("accountId")] = accountId.trimmed();
    enrichFromOutput(result, out);   // pk for public, npk/vpk for private
    return QJsonDocument(out).toJson(QJsonDocument::Compact);
}

// ── Faucet ────────────────────────────────────────────────────────────────────

QString WalletPlugin::claimFaucet(const QString& accountId)
{
    if (accountId.trimmed().isEmpty())
        return errorJson(QStringLiteral("accountId is required"));

    // CLI expects: wallet pinata claim --to Public/ID  (capital P)
    QString id = accountId.trimmed();
    QString toArg = (id.startsWith(QStringLiteral("Public/")) || id.startsWith(QStringLiteral("Private/")))
                  ? id
                  : QStringLiteral("Public/") + id;

    // The faucet program credits the recipient WITHOUT claiming it, so the chain rejects
    // crediting an unregistered (default-owned) account. Register it first if needed.
    ensureInitialized(toArg);

    QString result = runWalletCommand({
        QStringLiteral("pinata"),
        QStringLiteral("claim"),
        QStringLiteral("--to"),
        toArg
    }, 60000);

    QJsonDocument doc = QJsonDocument::fromJson(result.toUtf8());
    if (!doc.isNull() && doc.object().value(QStringLiteral("ok")).toBool()) {
        QJsonObject entry;
        entry[QStringLiteral("type")]     = QStringLiteral("faucet");
        entry[QStringLiteral("asset")]    = QStringLiteral("native");
        entry[QStringLiteral("sender")]   = QStringLiteral("");   // pinata has no sender
        entry[QStringLiteral("receiver")] = accountId.trimmed();
        entry[QStringLiteral("amount")]   = QStringLiteral("150");
        entry[QStringLiteral("txId")]     = doc.object().value(QStringLiteral("txHash")).toString();
        entry[QStringLiteral("ts")]       = QDateTime::currentDateTime().toString(Qt::ISODate);
        saveTx(accountId.trimmed(), entry);
    }

    return result;
}

QString WalletPlugin::startFaucet(const QString& accountId)
{
    if (accountId.trimmed().isEmpty())
        return errorJson(QStringLiteral("accountId is required"));
    QString id = accountId.trimmed();
    const QString toArg =
        (id.startsWith(QStringLiteral("Public/")) || id.startsWith(QStringLiteral("Private/")))
            ? id : QStringLiteral("Public/") + id;
    // Run the claim as a background job - it submits a tx and waits for a block (~15s),
    // which would otherwise blow past Basecamp's synchronous module-call timeout. The
    // wrapper auto-registers the recipient before claiming.
    return startPrivacyJob(QStringLiteral("faucet"), QStringLiteral("native"),
                           { QStringLiteral("pinata"), QStringLiteral("claim"),
                             QStringLiteral("--to"), toArg },
                           toArg, QString(), QStringLiteral("150"));
}

// ── The on-chain faucet (the deployed `medusa_faucet` program) ────────────────────────────────

// Where the guest .bin lives. It is DATA, not an executable, so this deliberately does not go
// through resolveBin(): nothing here is ever launched. It is still read with the same
// suspicion - faucetPreflight() proves its ImageID against kFaucetProgramId before any claim
// uses it, so a planted file is caught rather than trusted because of where it sat.
QString WalletPlugin::faucetGuestBin()
{
    const QString env = qEnvironmentVariable("MEDUSA_FAUCET_BIN").trimmed();
    if (!env.isEmpty())
        return QFileInfo::exists(env) ? QFileInfo(env).absoluteFilePath() : QString();
    // Same shape as resolveBin(): on a PACKAGED install the bundle is the only place looked at,
    // so a file planted in a uid-writable directory is never even a candidate. The ~/.local paths
    // are the unpackaged-dev fallback and are reached only when there is no bundle at all. The
    // ImageID check makes this defence in depth rather than the defence - a planted .bin with a
    // DIFFERENT id is refused, and one with the SAME id is byte-identical to the real program.
    const QString bdir = moduleBinDir();
    if (!bdir.isEmpty() && QDir(bdir).exists()) {
        const QString bundled = bdir + QStringLiteral("/medusa_faucet.bin");
        return QFileInfo::exists(bundled) ? bundled : QString();
    }
    for (const QString& c : { QDir::homePath() + QStringLiteral("/.local/share/medusa/medusa_faucet.bin"),
                              QDir::homePath() + QStringLiteral("/.local/bin/medusa_faucet.bin") })
        if (QFileInfo::exists(c))
            return c;
    return QString();
}

// Everything that has to be true before an on-chain claim can possibly succeed, decided ONCE.
// Ordered install-first, then zone, then wallet: a broken install is answered without touching
// the network, and each failure names the single next thing to fix.
//
// What this deliberately does NOT re-check: whether each treasury is initialized, and whether the
// cooldown has elapsed. medusa-faucet-client already fails fast on both with messages meant for a
// human ("treasury for definition <id> is not initialized - run init-treasury first",
// "cooldown not elapsed: N minutes remaining before the next claim"), and those checks need the
// treasury PDA, whose derivation lives in medusa_faucet_shared. Re-deriving a PDA in C++ is
// exactly the drift the shared crate exists to prevent, so the claim path surfaces the client's
// answer instead of computing a second opinion that could disagree with the chain.
WalletPlugin::FaucetPreflight WalletPlugin::faucetPreflight(const QString& owner)
{
    FaucetPreflight pf;
    pf.zone = netId();

    // 0. THE PER-ZONE CAPABILITY, ASKED FIRST. On a zone with no row in kFaucetZones there is
    //    nothing to install, nothing to verify and nothing to claim, so no probe is spawned and
    //    no install-side message is produced: telling the owner of a user-added zone that a
    //    helper is missing implies a token faucet was expected there, and none ever will be.
    const FaucetZoneRow* row = faucetZoneRow(pf.zone);
    if (!row) {
        pf.reason  = QStringLiteral("unsupported-zone");
        pf.message = QStringLiteral("this zone has no token faucet - the medusa_faucet program "
                                    "and its funded treasuries are deployed on the built-in "
                                    "Paradox Computer and Logos public testnet zones only, so "
                                    "the faucet dispenses native LEZ here");
        return pf;
    }
    for (const FaucetToken& t : row->tokens) {
        pf.definitions << QString::fromLatin1(t.definition);
        pf.tickers     << QString::fromLatin1(t.ticker);
        pf.treasuries  << QString::fromLatin1(t.treasury);
    }

    // 1. The client binary. Absent on every install that does not ship it, which today is all
    //    of them - so this is the message most users would see, and it must not read as a bug.
    const QString client = resolveBin(QStringLiteral("medusa-faucet-client"),
                                      "MEDUSA_FAUCET_CLIENT");
    if (client.isEmpty() || !QFileInfo(client).isExecutable()) {
        pf.reason  = QStringLiteral("client-missing");
        pf.message = QStringLiteral("the on-chain faucet helper (medusa-faucet-client) is not "
                                    "installed with this wallet, so it can only use the standard "
                                    "faucet");
        return pf;
    }
    pf.client = client;

    // 2. The guest binary, which is what the program id is computed from.
    pf.bin = faucetGuestBin();
    if (pf.bin.isEmpty()) {
        pf.reason  = QStringLiteral("bin-missing");
        pf.message = QStringLiteral("the faucet program binary (medusa_faucet.bin) is not "
                                    "installed with this wallet, so the on-chain faucet cannot "
                                    "be addressed - use the standard faucet");
        return pf;
    }

    // 3. THE CONSTANT, doing work. `info` is offline by construction (no unlock, no sequencer),
    //    so this costs one short-lived process and no network. A .bin whose ImageID is not
    //    kFaucetProgramId is a DIFFERENT program, whatever its filename says, and pointing a
    //    signed claim at it is the failure mode worth refusing.
    {
        QProcess probe;
        probe.setProcessChannelMode(QProcess::SeparateChannels);
        if (!startChild(probe, client, { QStringLiteral("info"), QStringLiteral("--bin"), pf.bin })) {
            pf.reason  = QStringLiteral("client-missing");
            pf.message = QStringLiteral("the on-chain faucet helper could not be launched");
            return pf;
        }
        probe.closeWriteChannel();
        if (!probe.waitForFinished(15000)) {
            probe.kill();
            probe.waitForFinished(2000);
        }
        const QJsonObject o = QJsonDocument::fromJson(probe.readAllStandardOutput()).object();
        const QString got = o.value(QStringLiteral("programId")).toString().trimmed().toLower();
        pf.verified = (got == QString::fromLatin1(kFaucetProgramId));
        if (!pf.verified) {
            pf.reason  = QStringLiteral("program-mismatch");
            pf.message = QStringLiteral("the local faucet program binary is not the deployed "
                                        "faucet (its id is ")
                       + (got.isEmpty() ? QStringLiteral("unreadable") : got.left(16) + QStringLiteral("…"))
                       + QStringLiteral(", expected ")
                       + QString::fromLatin1(kFaucetProgramId).left(16)
                       + QStringLiteral("…) - refusing to claim from it");
            return pf;
        }
    }

    // 4. A recipient holding per definition. The claim SIGNS each recipient, so every one must be
    //    an account this wallet owns, and they must be distinct: one account holds exactly one
    //    token definition, so the user's own account is never used here (it would be bound to
    //    one token forever). The wrapper's registry designates one per definition (its
    //    "vaults"); a definition with none yet gets an empty slot, NOT a refusal.
    //
    //    A MISSING HOLDING IS NOT A DEAD END, and treating it as one was half of the reported
    //    bug: on a wallet that has just been reset, no vault exists for any definition, so the
    //    old preflight refused every claim with "use the standard faucet once (it creates one)"
    //    - advice that had stopped being true the moment the client-side treasury drop was
    //    removed. The on-chain program accepts an UNINITIALIZED recipient (the guest asserts
    //    only that it carries the claimant's signature, and the token program claims it on the
    //    first transfer - wallet/faucet/guest/src/main.rs), so the claim path can simply make
    //    one. That provisioning is deliberately NOT done here: faucetStatus() calls this on
    //    every Tokens-tab open and a read-only status call must never create accounts. See
    //    ensureFaucetRecipients(), which the two claim verbs call and nothing else does.
    // Vaults are keyed BY OWNER: vaults[owner][definition] = holding. One holding per token
    // for the WHOLE wallet was the reported bug - the faucet always paid the same account
    // whichever one you claimed from, and every other account displayed tokens it could not
    // spend. With no owner in hand (the status path) there is nothing to look up, so every
    // slot is empty and the claim path mints what it needs.
    QHash<QString, QString> vaults;
    if (!owner.trimmed().isEmpty()) {
        const QJsonObject reg =
            QJsonDocument::fromJson(getTokenRegistry().toUtf8()).object();
        const QJsonObject mine = reg.value(QStringLiteral("vaults")).toObject()
                                    .value(owner.trimmed().section(QLatin1Char('/'), -1)).toObject();
        for (auto it = mine.constBegin(); it != mine.constEnd(); ++it)
            vaults.insert(it.key(), it.value().toString());
    }
    for (const QString& def : std::as_const(pf.definitions))
        pf.recipients << vaults.value(def).trimmed();   // "" = none yet, minted at claim time

    pf.ok = true;
    return pf;
}

// Give every definition a recipient this wallet owns, creating one where the registry has none.
//
// CLAIM PATH ONLY (startTokenFaucet), never the status path: it writes - it mints keys and it
// records them. Returns "" on success, or the sentence to refuse with.
//
// Why a fresh account is a correct recipient: the guest requires `recipient.is_authorized`, i.e.
// the claimant's signature, and nothing else; an account whose on-chain state is still the
// default is claimed by the token program during the chained transfer. That is the difference
// between this and the client-side drop it replaces, where a cross-wallet credit to a pristine
// account IS rejected (the treasury cannot produce the recipient key's co-signature).
//
// Each new account is recorded in the wrapper's registry as that definition's vault before it is
// used. That matters for more than tidiness: the cooldown marker PDA is derived from the FIRST
// recipient, so a claim that invented a new account every time would derive a new marker every
// time and the on-chain 6h cooldown would never bind to anything.
QString WalletPlugin::ensureFaucetRecipients(FaucetPreflight& pf, const QString& owner)
{
    for (int i = 0; i < pf.definitions.size(); ++i) {
        if (!pf.recipients.at(i).isEmpty())
            continue;
        const QString made = createAccount();
        const QJsonObject o = QJsonDocument::fromJson(made.toUtf8()).object();
        if (o.contains(QStringLiteral("error")))
            return QStringLiteral("could not create a holding account for ")
                 + pf.tickers.value(i) + QStringLiteral(": ")
                 + o.value(QStringLiteral("error")).toString();
        // createAccount returns {"id":"Public/<bare>"} (structured CLI) or folds the CLI's
        // "account_id Public/<bare>" line into the same key. Either way we want the bare id:
        // the faucet client accepts both, but the registry stores bare.
        const QString bare = o.value(QStringLiteral("id")).toString().trimmed()
                                 .section(QLatin1Char('/'), -1).trimmed();
        if (bare.isEmpty())
            return QStringLiteral("could not read back the holding account created for ")
                 + pf.tickers.value(i);
        // Record it BEFORE the claim: if the claim then fails, the next attempt reuses this
        // account instead of minting another, and the cooldown marker stays put.
        // Record it against the CLAIMING account: vaults are per-owner, so a holding minted
        // for account A must be A's, not a wallet-wide one every other account then displays.
        const QString ownerBare = owner.trimmed().section(QLatin1Char('/'), -1);
        QStringList regArgs{ QStringLiteral("token-registry"), QStringLiteral("vault"),
                             pf.definitions.at(i), bare };
        if (!ownerBare.isEmpty())
            regArgs << ownerBare;
        const QString rec = runWalletCommand(regArgs, 20000);
        if (QJsonDocument::fromJson(rec.toUtf8()).object().contains(QStringLiteral("error")))
            appendLog(QStringLiteral("faucet: could not record the vault for %1 (%2) - the claim "
                                     "still runs, but the next one will create another account")
                          .arg(pf.tickers.value(i), bare), QStringLiteral("error"));
        pf.recipients[i] = bare;
        pf.provisioned << bare;
    }
    // The client refuses duplicates (a holding account holds exactly one definition), and so does
    // the chain. Catch it here where the message can say which registry entry is wrong.
    QSet<QString> seen;
    for (const QString& r : std::as_const(pf.recipients)) {
        if (seen.contains(r))
            return QStringLiteral("two faucet tokens are pointed at the same holding account (")
                 + r.left(8) + QStringLiteral("…) - an account can hold only one token "
                                              "definition, so the token registry needs fixing");
        seen.insert(r);
    }
    return QString();
}

QString WalletPlugin::faucetStatus()
{
    const FaucetPreflight pf = faucetPreflight();

    QJsonObject o;
    o[QStringLiteral("programId")]   = QString::fromLatin1(kFaucetProgramId);
    o[QStringLiteral("available")]   = pf.ok;
    o[QStringLiteral("reason")]      = pf.reason;
    o[QStringLiteral("message")]     = pf.message;
    o[QStringLiteral("client")]      = pf.client;
    o[QStringLiteral("clientFound")] = !pf.client.isEmpty();
    o[QStringLiteral("bin")]         = pf.bin;
    o[QStringLiteral("binFound")]    = !pf.bin.isEmpty();
    o[QStringLiteral("verified")]    = pf.verified;
    o[QStringLiteral("definitions")] = QJsonArray::fromStringList(pf.definitions);
    o[QStringLiteral("recipients")]  = QJsonArray::fromStringList(pf.recipients);
    // The treasury PDAs from the zone table: the accounts an operator funds, and the accounts
    // whose emptiness is the one failure the wallet cannot pre-check (see below). Reported so
    // "which account is empty" is answerable without re-deriving a PDA anywhere.
    o[QStringLiteral("treasuries")]  = QJsonArray::fromStringList(pf.treasuries);
    // The per-zone capability, on the status object as well as on the zone record, so a caller
    // that already has the status does not have to go and fetch the zone list to interpret it.
    o[QStringLiteral("zone")]           = pf.zone;
    o[QStringLiteral("tokenFaucetZone")] = zoneHasTokenFaucet(pf.zone);
    QJsonObject tick;
    for (int i = 0; i < pf.definitions.size() && i < pf.tickers.size(); ++i)
        tick[pf.definitions.at(i)] = pf.tickers.at(i);
    o[QStringLiteral("tickers")] = tick;
    // Honest, not decorative. Treasury balances live behind a PDA whose derivation is owned by
    // medusa_faucet_shared, and this module does not re-derive it (see faucetPreflight), so the
    // wallet cannot assert "funded" from here. What it CAN do is never present an empty treasury
    // as an unexplained failure: onJobFinished translates the sequencer's silent rejection of a
    // preflighted claim into the funding message.
    o[QStringLiteral("funded")] = QStringLiteral("unknown");
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

QString WalletPlugin::startTokenFaucet(const QString& accountId, const QString& password)
{
    // The gate comes FIRST, before any probe, any CLI call and any resolution: a caller that
    // cannot prove who it is learns nothing about this wallet's install or its holdings, and the
    // refusal it gets is identical to every other spend verb's.
    if (!authorize(password))
        return authRefusal();

    if (accountId.trimmed().isEmpty())
        return errorJson(QStringLiteral("accountId is required"));

    FaucetPreflight pf = faucetPreflight(accountId);
    if (!pf.ok)
        return errorJson(pf.message, pf.reason);

    // Every definition needs a recipient this wallet owns; mint the missing ones. This is the
    // one write the status path never performs, which is why it is here and not in the preflight.
    const QString provErr = ensureFaucetRecipients(pf, accountId);
    if (!provErr.isEmpty())
        return errorJson(provErr, QStringLiteral("no-holding"));
    if (!pf.provisioned.isEmpty())
        appendLog(QStringLiteral("faucet: created %1 token holding(s) for this claim: %2")
                      .arg(pf.provisioned.size())
                      .arg(pf.provisioned.join(QStringLiteral(", "))));

    QString id = accountId.trimmed();
    const QString attribute =
        (id.startsWith(QStringLiteral("Public/")) || id.startsWith(QStringLiteral("Private/")))
            ? id : QStringLiteral("Public/") + id;

    // ── ORDER: PINATA FIRST, TOKENS SECOND ────────────────────────────────────────────────
    // The faucet button fires both halves, and they are two independent jobs (the UI composes
    // them; see the header). They must not run at the same time. `pinata claim` may run
    // `auth-transfer init` for the recipient first - the pinata program credits WITHOUT
    // claiming, so the chain rejects a credit to an account it has no record of - and both
    // halves drive the same wallet store from separate processes. Interleaving them means two
    // writers on one storage.json and a claim racing a registration.
    //
    // So a token claim started while a native claim is in flight is QUEUED behind it rather
    // than run beside it, and started (whatever the first one answered: the two cooldowns are
    // independent, and a LEZ refusal must never suppress the tokens) when it finishes.
    QString waitFor;
    int waitSeq = -1;
    for (auto it = m_jobs.constBegin(); it != m_jobs.constEnd(); ++it) {
        const Job* other = it.value();
        if (other->op != QStringLiteral("faucet") || other->state != QStringLiteral("running"))
            continue;
        const int seq = other->id.mid(4).toInt();   // "job-<n>"; hash order is not insert order
        if (seq > waitSeq) { waitSeq = seq; waitFor = other->id; }
    }

    // No amount: the program dispenses a pseudorandom 10-500 PER DEFINITION, decided on-chain
    // from the block clock, and the client's reply does not carry the figures either. Reporting a
    // made-up number in the history would be worse than reporting none.
    return startPrivacyJob(QStringLiteral("tokenfaucet"), QStringLiteral("token"),
                           { QStringLiteral("claim"),
                             QStringLiteral("--bin"),         pf.bin,
                             QStringLiteral("--account"),     pf.recipients.join(QLatin1Char(',')),
                             QStringLiteral("--definitions"), pf.definitions.join(QLatin1Char(',')) },
                           attribute, QString(), QString(), pf.client, waitFor);
}

// ── Transaction history (local store) ─────────────────────────────────────────

static QString txHistoryKey(const QString& accountId)
{
    // Sanitise accountId so it is safe as a QSettings key segment
    QString safe = accountId;
    safe.replace(QLatin1Char('/'), QLatin1Char('_'));
    return QStringLiteral("medusa-wallet/txHistory/") + safe;
}

void WalletPlugin::saveTx(const QString& accountId, const QJsonObject& entry)
{
    QSettings s;
    QString key = txHistoryKey(accountId);
    QJsonArray arr = QJsonDocument::fromJson(
        s.value(key).toByteArray()).array();
    arr.prepend(entry);                // newest first
    if (arr.size() > 50) arr.removeLast();
    s.setValue(key, QJsonDocument(arr).toJson(QJsonDocument::Compact));
    s.sync();
}

QString WalletPlugin::getTransactions(const QString& accountId)
{
    if (accountId.trimmed().isEmpty())
        return errorJson(QStringLiteral("accountId is required"));
    QSettings s;
    QByteArray raw = s.value(txHistoryKey(accountId.trimmed())).toByteArray();
    QJsonArray arr = QJsonDocument::fromJson(raw).array();
    return QJsonDocument(arr).toJson(QJsonDocument::Compact);
}

// ── Transfer ──────────────────────────────────────────────────────────────────

QString WalletPlugin::sendTransfer(const QString& from,
                                    const QString& to,
                                    const QString& value,
                                    const QString& password)
{
    // The gate goes ahead of argument validation on every spend verb, so an unauthorised caller
    // learns nothing from the shape of the error either.
    if (!authorize(password))
        return authRefusal();
    if (from.trimmed().isEmpty())
        return errorJson(QStringLiteral("from account is required"));
    if (to.trimmed().isEmpty())
        return errorJson(QStringLiteral("to account is required"));
    QString asset, definitionId, amount, specErr;
    if (!parseValueSpec(value, &asset, &definitionId, &amount, &specErr))
        return errorJson(specErr);
    if (asset != QStringLiteral("native"))
        return errorJson(QStringLiteral("this verb sends native LEZ - use startSendToken "
                                        "for a token"));

    appendLog(QStringLiteral("transfer: %1 → %2 (%3 tok)").arg(from, to, amount));

    QString result = runWalletCommand({
        QStringLiteral("auth-transfer"),
        QStringLiteral("send"),
        QStringLiteral("--from"),
        from.trimmed(),
        QStringLiteral("--to"),
        to.trimmed(),
        QStringLiteral("--amount"),
        amount.trimmed()
    }, 60000);

    // On success, persist to local tx history for both accounts
    QJsonDocument doc = QJsonDocument::fromJson(result.toUtf8());
    if (!doc.isNull() && doc.object().value(QStringLiteral("ok")).toBool()) {
        QJsonObject entry;
        entry[QStringLiteral("type")]     = QStringLiteral("send");
        entry[QStringLiteral("asset")]    = QStringLiteral("native");
        entry[QStringLiteral("sender")]   = from.trimmed();
        entry[QStringLiteral("receiver")] = to.trimmed();
        entry[QStringLiteral("amount")]   = amount.trimmed();
        entry[QStringLiteral("txId")]     = doc.object().value(QStringLiteral("txId")).toString();
        entry[QStringLiteral("ts")]       = QDateTime::currentDateTime().toString(Qt::ISODate);
        saveTx(from.trimmed(), entry);
        saveTx(to.trimmed(), entry);
    }

    return result;
}

QString WalletPlugin::startSendToken(const QString& from, const QString& to,
                                     const QString& value, const QString& password)
{
    if (!authorize(password))     return authRefusal();
    if (from.trimmed().isEmpty()) return errorJson(QStringLiteral("from account is required"));
    if (to.trimmed().isEmpty())   return errorJson(QStringLiteral("to account is required"));
    QString asset, definitionId, amount, specErr;
    if (!parseValueSpec(value, &asset, &definitionId, &amount, &specErr))
        return errorJson(specErr);
    if (asset != QStringLiteral("token"))
        return errorJson(QStringLiteral("this verb sends a token - pass "
                                        "{\"asset\":\"token\",\"definitionId\":…,\"amount\":…}, "
                                        "or use startSendTransfer for native LEZ"));
    // The wrapper's token-transfer derives/creates ATAs + token-sends + waits for landing
    // (~30-40s), so run it as a background job like the privacy ops.
    return startPrivacyJob(QStringLiteral("tokensend"), QStringLiteral("token"),
                           { QStringLiteral("token-transfer"), from.trimmed(), to.trimmed(),
                             definitionId, amount },
                           from.trimmed(), to.trimmed(), amount);
}

QString WalletPlugin::startSendTransfer(const QString& from, const QString& to,
                                        const QString& value, const QString& password)
{
    if (!authorize(password))     return authRefusal();
    if (from.trimmed().isEmpty()) return errorJson(QStringLiteral("from account is required"));
    if (to.trimmed().isEmpty())   return errorJson(QStringLiteral("to account is required"));
    QString asset, definitionId, amount, specErr;
    if (!parseValueSpec(value, &asset, &definitionId, &amount, &specErr))
        return errorJson(specErr);
    if (asset != QStringLiteral("native"))
        return errorJson(QStringLiteral("this verb sends native LEZ - use startSendToken "
                                        "for a token"));
    // Background job: the destination can be a Private account (private→private from the main
    // Send screen), which is a multi-minute real proof. The wrapper auto-syncs + uses the proof
    // budget when --from is Private; a plain public send just submits + lands. Never blocks.
    return startPrivacyJob(QStringLiteral("send"), QStringLiteral("native"),
                           { QStringLiteral("auth-transfer"), QStringLiteral("send"),
                             QStringLiteral("--from"), from.trimmed(),
                             QStringLiteral("--to"),   to.trimmed(),
                             QStringLiteral("--amount"), amount },
                           from.trimmed(), to.trimmed(), amount);
}

// ── Privacy transfers (asynchronous) ───────────────────────────────────────────

WalletPlugin::~WalletPlugin()
{
    stopSequencer();   // don't orphan the child sequencer when the module unloads
    stopTor();         // and don't orphan the bundled Tor
    if (m_idleLock)
        m_idleLock->stop();   // no auto-lock callback while the plugin is unwinding
    if (QProcess* probe = m_compatProbe) {   // nor an in-flight `wallet check-health` compat probe
        m_compatProbe = nullptr;   // detach first: waitForFinished() below runs the finished
                                   // handler synchronously, and that handler nulls the member
        if (probe->state() != QProcess::NotRunning) {
            probe->terminate();
            if (!probe->waitForFinished(3000))
                probe->kill();
            probe->waitForFinished(2000);
        }
        probe->deleteLater();
    }
    qDeleteAll(m_jobs);
    m_jobs.clear();
    qDeleteAll(m_sessions);
    m_sessions.clear();
    qDeleteAll(m_requests);
    m_requests.clear();
}

QString WalletPlugin::startShield(const QString& from, const QString& to,
                                  const QString& value, const QString& password)
{
    if (!authorize(password))     return authRefusal();
    if (from.trimmed().isEmpty()) return errorJson(QStringLiteral("from account is required"));
    if (to.trimmed().isEmpty())   return errorJson(QStringLiteral("to account is required"));
    QString asset, definitionId, amount, specErr;
    if (!parseValueSpec(value, &asset, &definitionId, &amount, &specErr))
        return errorJson(specErr);

    bool conflict = false;
    QString fromP = withPrivacyPrefix(from, QStringLiteral("Public"), &conflict);
    if (conflict) return errorJson(QStringLiteral("shield source must be a Public account"));
    QString toP = withPrivacyPrefix(to, QStringLiteral("Private"), &conflict);
    if (conflict) return errorJson(QStringLiteral("shield destination must be a Private account"));
    if (const QString busy = privateDestInFlight(toP); !busy.isEmpty()) return errorJson(busy);

    // Token shield can't use `token send --from <owner>` (guest-panics: the owner account
    // is not a token holding) - route through the wrapper's token-shield verb, which
    // resolves a direct-owned holding of the definition or fails with a clear error.
    // (parseValueSpec has already refused a token value with no definitionId.)
    if (assetProgram(asset) == QStringLiteral("token")) {
        QStringList args{ QStringLiteral("token-shield"), fromP, toP, definitionId, amount };
        return startPrivacyJob(QStringLiteral("shield"), asset, args, fromP, toP, amount);
    }

    QStringList args{ assetProgram(asset), QStringLiteral("send"),
                      QStringLiteral("--from"), fromP,
                      QStringLiteral("--to"),   toP,
                      QStringLiteral("--amount"), amount };
    return startPrivacyJob(QStringLiteral("shield"), asset, args, fromP, toP, amount);
}

QString WalletPlugin::startDeshield(const QString& from, const QString& to,
                                    const QString& value, const QString& password)
{
    if (!authorize(password))     return authRefusal();
    if (from.trimmed().isEmpty()) return errorJson(QStringLiteral("from account is required"));
    if (to.trimmed().isEmpty())   return errorJson(QStringLiteral("to account is required"));
    QString asset, definitionId, amount, specErr;
    if (!parseValueSpec(value, &asset, &definitionId, &amount, &specErr))
        return errorJson(specErr);

    bool conflict = false;
    QString fromP = withPrivacyPrefix(from, QStringLiteral("Private"), &conflict);
    if (conflict) return errorJson(QStringLiteral("deshield source must be a Private account"));
    QString toP = withPrivacyPrefix(to, QStringLiteral("Public"), &conflict);
    if (conflict) return errorJson(QStringLiteral("deshield destination must be a Public account"));

    // Token deshield must land in a token HOLDING, not the owner's auth-transfer account -
    // the wrapper's token-deshield verb derives + creates the recipient owner's ATA.
    // (parseValueSpec has already refused a token value with no definitionId.)
    if (assetProgram(asset) == QStringLiteral("token")) {
        QStringList args{ QStringLiteral("token-deshield"), fromP, toP, definitionId, amount };
        return startPrivacyJob(QStringLiteral("deshield"), asset, args, fromP, toP, amount);
    }

    QStringList args{ assetProgram(asset), QStringLiteral("send"),
                      QStringLiteral("--from"), fromP,
                      QStringLiteral("--to"),   toP,
                      QStringLiteral("--amount"), amount };
    return startPrivacyJob(QStringLiteral("deshield"), asset, args, fromP, toP, amount);
}

QString WalletPlugin::startPrivateTransfer(const QString& from, const QString& to,
                                           const QString& value, const QString& password)
{
    // Ungated, the foreign-recipient form below was the cleanest exfiltration primitive in the
    // module: a real STARK proof paying an attacker-supplied npk/vpk, private and
    // unattributable. Folding it in here did not soften that - the gate is the same one, in
    // the same position, ahead of every argument check.
    if (!authorize(password))     return authRefusal();
    if (from.trimmed().isEmpty()) return errorJson(QStringLiteral("from account is required"));
    QString asset, definitionId, amount, specErr;
    if (!parseValueSpec(value, &asset, &definitionId, &amount, &specErr))
        return errorJson(specErr);

    QString toAccount, toNpk, toVpk, toIdentifier, toErr;
    if (!parseRecipientSpec(to, &toAccount, &toNpk, &toVpk, &toIdentifier, &toErr))
        return errorJson(toErr);

    bool conflict = false;
    QString fromP = withPrivacyPrefix(from, QStringLiteral("Private"), &conflict);
    if (conflict) return errorJson(QStringLiteral("private-transfer source must be a Private account"));

    if (toAccount.isEmpty()) {   // foreign recipient: --to-npk/--to-vpk/--to-identifier
        QStringList args{ assetProgram(asset), QStringLiteral("send"),
                          QStringLiteral("--from"),          fromP,
                          QStringLiteral("--to-npk"),        toNpk,
                          QStringLiteral("--to-vpk"),        toVpk,
                          QStringLiteral("--to-identifier"), toIdentifier,
                          QStringLiteral("--amount"),        amount };
        // Recipient is foreign - no owned "to" account to credit in local history.
        return startPrivacyJob(QStringLiteral("private"), asset, args, fromP, QString(), amount);
    }

    QString toP = withPrivacyPrefix(toAccount, QStringLiteral("Private"), &conflict);
    if (conflict) return errorJson(QStringLiteral("private-transfer destination must be a Private account"));
    if (const QString busy = privateDestInFlight(toP); !busy.isEmpty()) return errorJson(busy);

    QStringList args{ assetProgram(asset), QStringLiteral("send"),
                      QStringLiteral("--from"), fromP,
                      QStringLiteral("--to"),   toP,
                      QStringLiteral("--amount"), amount };
    return startPrivacyJob(QStringLiteral("private"), asset, args, fromP, toP, amount);
}

QString WalletPlugin::privateDestInFlight(const QString& toP) const
{
    // A fresh private account stops being a valid privacy destination the moment ANOTHER
    // in-flight shield/private job targets it (rc5 rejects private output onto non-default
    // accounts) - the on-chain guards can't see that yet, so refuse the double-book here
    // rather than waste a second multi-minute proof.
    for (auto it = m_jobs.constBegin(); it != m_jobs.constEnd(); ++it) {
        const Job* j = it.value();
        if (j->state == QStringLiteral("running") && j->to == toP
            && j->op != QStringLiteral("deshield"))
            return QStringLiteral("a privacy transfer to this account is already in flight - "
                                  "wait for it or pick another fresh private account");
    }
    return QString();
}

int WalletPlugin::proveTimeoutMs()
{
    // Mirror the wrapper: MEDUSA_PROOF_TIMEOUT_S (default 3600s) is the proof budget the
    // wrapper enforces per step; this job-level kill adds 30 min of slack for the wrapper's
    // pre-steps (private-state sync up to 900s, ata create/poll) so the wrapper's budgets -
    // including a user's larger override for slow hardware - always decide first.
    bool ok = false;
    int proofS = qEnvironmentVariable("MEDUSA_PROOF_TIMEOUT_S").toInt(&ok);
    if (!ok || proofS <= 0) proofS = 3600;
    return (proofS + 30 * 60) * 1000;
}

QString WalletPlugin::startPrivacyJob(const QString& op, const QString& asset,
                                      const QStringList& sendArgs,
                                      const QString& from, const QString& to,
                                      const QString& amount,
                                      const QString& binOverride,
                                      const QString& waitForJob)
{
    // Bound the registry - drop the oldest terminal jobs once we hit the cap.
    if (m_jobs.size() >= kMaxJobs) {
        QList<QString> terminal;
        for (auto it = m_jobs.constBegin(); it != m_jobs.constEnd(); ++it)
            if (it.value()->state != QStringLiteral("running"))
                terminal.append(it.key());
        std::sort(terminal.begin(), terminal.end(), [](const QString& a, const QString& b) {
            return a.mid(4).toInt() < b.mid(4).toInt();   // "job-<n>"
        });
        for (const QString& id : terminal) {
            if (m_jobs.size() < kMaxJobs) break;
            delete m_jobs.take(id);
        }
    }

    // Every existing caller runs the wallet CLI; the on-chain faucet runs its own client under
    // the same job machinery (registry, phases, kill budget, history) rather than growing a
    // second, subtly different copy of it.
    const QString bin   = binOverride.isEmpty() ? cliPath() : binOverride;
    const QString jobId = QStringLiteral("job-%1").arg(++m_jobSeq);

    Job* j   = new Job;
    j->id    = jobId;
    j->op    = op;
    j->asset = (asset.trimmed().toLower() == QStringLiteral("token"))
             ? QStringLiteral("token") : QStringLiteral("native");
    j->from  = from;
    j->to    = to;
    j->amount = amount;
    j->state  = QStringLiteral("running");
    j->phase  = QStringLiteral("processing");
    j->timer.start();
    m_jobs.insert(jobId, j);

    // redactedArgs, not a raw join: this is the same log ring the synchronous runner redacts into,
    // and it must not be the one place a future --password/--private-key call site leaks. The
    // program is named from `bin` rather than written as "wallet": a job that runs its own binary
    // would otherwise be logged as a wallet-CLI call that no wallet CLI ever made.
    appendLog(QStringLiteral("%1 (%2): %3 %4")
                  .arg(op, j->asset, QFileInfo(bin).fileName(), redactedArgs(sendArgs)));

    // QUEUED BEHIND ANOTHER JOB (only the faucet's token half does this - see startTokenFaucet).
    // The job exists, is registered and is reported "running" so the caller gets an ordinary
    // jobId to poll; only the child is held back. It is launched by onJobFinished when the job it
    // waits for ends, or by the fallback timer below if that never happens.
    if (!waitForJob.isEmpty()) {
        const Job* ahead = m_jobs.value(waitForJob, nullptr);
        if (ahead && ahead->state == QStringLiteral("running")) {
            j->waitFor     = waitForJob;
            j->pendingBin  = bin;
            j->pendingArgs = sendArgs;
            j->pendingOwnBin = !binOverride.isEmpty();
            j->phase       = QStringLiteral("queued");
            // A queued job with no child would never finish if the job ahead of it somehow never
            // reported, so the wait is capped rather than trusted. Starting late is recoverable;
            // hanging forever is not.
            QTimer::singleShot(kQueuedStartMaxMs, this, [this, jobId]() {
                if (Job* job = m_jobs.value(jobId, nullptr))
                    if (job->state == QStringLiteral("running") && !job->proc
                        && !job->pendingBin.isEmpty())
                        startJobProcess(job, job->pendingBin, job->pendingArgs, job->pendingOwnBin);
            });
            QJsonObject q;
            q[QStringLiteral("jobId")] = jobId;
            q[QStringLiteral("state")] = QStringLiteral("running");
            return QJsonDocument(q).toJson(QJsonDocument::Compact);
        }
    }

    startJobProcess(j, bin, sendArgs, !binOverride.isEmpty());

    QJsonObject o;
    o[QStringLiteral("jobId")] = jobId;
    o[QStringLiteral("state")] = QStringLiteral("running");
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

// Create, wire and launch the child for ONE job. Split out of startPrivacyJob so a queued job
// can be started later through the exact same path: a second copy of this wiring is how a
// follow-on job quietly loses the kill budget, the phase reporting or the password on stdin.
void WalletPlugin::startJobProcess(Job* j, const QString& bin, const QStringList& args,
                                   bool ownBin)
{
    if (!j || j->proc)
        return;
    const QString jobId = j->id;

    QProcess* proc = new QProcess(this);
    proc->setProcessChannelMode(QProcess::SeparateChannels);
    j->proc  = proc;
    j->phase = QStringLiteral("processing");

    QObject::connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                     this, [this, jobId](int code, QProcess::ExitStatus) {
        onJobFinished(jobId, code);
    });
    QObject::connect(proc, &QProcess::errorOccurred, this,
                     [this, jobId, bin, ownBin](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart) return;   // other errors arrive via finished()
        Job* job = m_jobs.value(jobId, nullptr);
        if (!job || job->state != QStringLiteral("running")) return;
        job->state  = QStringLiteral("error");
        // NOT "configure the path in settings": there is no such setting any more (see
        // cliPath()). This is the async twin of the message in runWalletCommandInput, and it
        // serves shield, deshield, private transfer, token send, plain send and the faucet - so
        // it was the message most users would actually have seen. A job running its OWN binary
        // must not blame the wallet CLI: naming the wrong missing file is how a five-minute fix
        // becomes an hour.
        job->result = ownBin
            ? errorJson(QStringLiteral("on-chain faucet helper could not be launched: ") + bin)
            : errorJson(QStringLiteral("wallet CLI not found: ") + bin
                        + QStringLiteral(" - reinstall the medusa_core module, or set "
                                         "MEDUSA_WALLET_CLI before launching"));
        if (job->proc) { job->proc->deleteLater(); job->proc = nullptr; }
        // This job is terminal and onJobFinished will NEVER run for it: QProcess does not emit
        // finished() after FailedToStart. Anything sequenced behind it has to be released here
        // too, or a missing wallet CLI would leave the queued half stuck until the cap expires.
        startQueuedBehind(jobId);
    });

    // Stream stdout so the UI can show a real "sent to L2" phase the moment the CLI
    // prints its tx hash (proof done -> submitted to the sequencer), instead of sitting
    // on "processing" the whole run. Also buffers stdout for onJobFinished to consume.
    QObject::connect(proc, &QProcess::readyReadStandardOutput, this, [this, jobId]() {
        Job* job = m_jobs.value(jobId, nullptr);
        if (!job || !job->proc) return;
        job->outBuf += QString::fromUtf8(job->proc->readAllStandardOutput());
        if (job->phase == QStringLiteral("processing")
            && (job->outBuf.contains(QStringLiteral("Transaction hash is"))
                || job->outBuf.contains(QStringLiteral("txHash"))))
            job->phase = QStringLiteral("sent");
    });

    // Safety net: a runaway proof is killed after the proving timeout. We flag
    // the job first so onJobFinished can report the timeout reason rather than a
    // bare crash code. (The timer is parented to proc, so it is cancelled if proc
    // finishes/destructs first.)
    QTimer::singleShot(proveTimeoutMs(), proc, [this, jobId, proc]() {
        if (proc->state() != QProcess::NotRunning) {
            if (Job* job = m_jobs.value(jobId, nullptr))
                job->killedByTimeout = true;
            proc->kill();
        }
    });

    // Proof mode MUST match the active zone's sequencer. Every real zone - "diaphani" (Paradox ·
    // Tor) and any user-added remote sequencer - runs real proofs (RISC0_DEV_MODE=0) for valid,
    // secure receipts, so the wallet must prove for real too - slow on CPU (minutes), but
    // legitimate. ONLY the local "devnet" sandbox stays dev-mode (fast) for iteration. Explicit
    // so it can't depend on inherited env.
    {
        const bool realProofs = (netId() != QStringLiteral("devnet"));
        QProcessEnvironment penv = childEnv();   // sanitised $PATH, then the proof mode on top
        penv.insert(QStringLiteral("RISC0_DEV_MODE"), realProofs ? QStringLiteral("0") : QStringLiteral("1"));
        // The wallet WRAPPER defaults LEE_WALLET_HOME_DIR for itself, so it and the module always
        // agree on one home even when nothing exported it. A non-wrapper child does not: the
        // faucet client would fall back to lee's own default and operate on a DIFFERENT wallet -
        // wrong keys, wrong sequencer, and a claim that looks like it silently did nothing.
        // walletHome() returns the inherited value whenever there is one, so this pins the same
        // home the module itself reads and changes nothing for anyone who already set it.
        if (ownBin)
            penv.insert(QStringLiteral("LEE_WALLET_HOME_DIR"), walletHome());
        proc->setProcessEnvironment(penv);
    }

    startChild(*proc, bin, args);
    // Feed the session password to the proof process's stdin (empty for plaintext
    // wallets), then close the channel so the CLI proceeds.
    if (proc->waitForStarted(3000)) {
        proc->write((m_password + QStringLiteral("\n")).toUtf8());
        proc->closeWriteChannel();
    }
}

void WalletPlugin::onJobFinished(const QString& jobId, int exitCode)
{
    Job* j = m_jobs.value(jobId, nullptr);
    if (!j || !j->proc)
        return;

    QProcess* proc = j->proc;
    const QString out = (j->outBuf + QString::fromUtf8(proc->readAllStandardOutput())).trimmed();
    const QString err = cleanStderr(QString::fromUtf8(proc->readAllStandardError()));
    const int effectiveCode = (proc->exitStatus() == QProcess::CrashExit)
                            ? (exitCode != 0 ? exitCode : 137)
                            : exitCode;

    QString normalized;
    if (j->killedByTimeout) {
        normalized = errorJson(QStringLiteral(
            "privacy transfer exceeded the %1-minute job limit and was cancelled "
            "- use a GPU/Bonsai prover or RISC0_DEV_MODE=1 for faster proofs")
            .arg(proveTimeoutMs() / 60000));
    } else {
        // On failure the CLI's message is on stderr; on success the result is stdout.
        const QString effective = (effectiveCode != 0 && out.isEmpty()) ? err : out;
        normalized = normalizeCliOutput(effective, effectiveCode);
    }
    QJsonObject no = QJsonDocument::fromJson(normalized.toUtf8()).object();
    const bool success = !j->killedByTimeout && (effectiveCode == 0)
                       && !no.contains(QStringLiteral("error"));

    // THE UNFUNDED TREASURY, NAMED. A tokenfaucet claim only reaches the chain after
    // faucetPreflight() passed and after medusa-faucet-client's own fail-fast checks (treasury
    // initialized, cooldown elapsed) passed, and the client reports both of those in plain
    // words. What is left when the sequencer then drops the tx is overwhelmingly a treasury with
    // no supply in it: the program is deployed and initialized but nobody has funded it. The raw
    // message for that is "transaction … was not included within N blocks", which tells the user
    // nothing they can act on, so it is replaced here rather than shown.
    if (!success && j->op == QStringLiteral("tokenfaucet")) {
        const QString raw = no.value(QStringLiteral("error")).toString();
        if (raw.contains(QStringLiteral("was not included"))
            || raw.contains(QStringLiteral("rejected it silently"))) {
            // Name the treasuries, so "fund it" is an instruction rather than a wish. They come
            // from this zone's row in kFaucetZones, which is the only place they exist.
            QStringList tre;
            if (const FaucetZoneRow* row = faucetZoneRow(netId()))
                for (const FaucetToken& t : row->tokens)
                    tre << QString::fromLatin1(t.ticker) + QStringLiteral(" ")
                         + QString::fromLatin1(t.treasury);
            no[QStringLiteral("error")] =
                QStringLiteral("the faucet treasuries on this zone have no token supply left - the "
                               "faucet program is deployed and initialized but its treasuries are "
                               "empty, so there is nothing to dispense")
                + (tre.isEmpty() ? QString()
                                 : QStringLiteral(" (") + tre.join(QStringLiteral(", "))
                                     + QStringLiteral(")"));
            no[QStringLiteral("reason")]    = QStringLiteral("not-funded");
            no[QStringLiteral("rawError")]  = raw;
            normalized = QJsonDocument(no).toJson(QJsonDocument::Compact);
        }
    }

    // ── WHICH HALF DID WHAT, AS A CODE AND NOT A SENTENCE ─────────────────────────────────
    // The faucet's two halves have INDEPENDENT cooldowns (pinata's is its own; the token
    // program's is 6h against a marker PDA derived from the first recipient), so they drift
    // apart the moment anyone claims twice, and "one worked, the other is on cooldown" is the
    // NORMAL steady state rather than an edge case. A caller has to be able to tell that apart
    // from a real failure without pattern-matching prose, so every terminal faucet job carries a
    // machine-readable `reason`. The client's own sentence is left untouched in `error`: it
    // carries the figure ("N minutes remaining") that a countdown is rendered from.
    if (!success && (j->op == QStringLiteral("tokenfaucet") || j->op == QStringLiteral("faucet"))
        && !no.contains(QStringLiteral("reason"))) {
        const QString raw = no.value(QStringLiteral("error")).toString();
        const QString low = raw.toLower();
        QString reason;
        if (low.contains(QStringLiteral("cooldown")) || low.contains(QStringLiteral("too soon"))
            || low.contains(QStringLiteral("already claimed")))
            reason = QStringLiteral("cooldown");
        else if (low.contains(QStringLiteral("is not initialized")))
            reason = QStringLiteral("not-initialized");
        else if (low.contains(QStringLiteral("not owned by this wallet")))
            reason = QStringLiteral("no-holding");
        else if (low.contains(QStringLiteral("sequencer unreachable"))
                 || low.contains(QStringLiteral("timed out")))
            reason = QStringLiteral("unreachable");
        if (!reason.isEmpty()) {
            no[QStringLiteral("reason")] = reason;
            normalized = QJsonDocument(no).toJson(QJsonDocument::Compact);
        }
    }

    j->result = normalized;
    j->state  = success ? QStringLiteral("done") : QStringLiteral("error");

    if (success) {
        const QString txId = extractTxHash(normalized);
        QJsonObject entry;
        entry[QStringLiteral("type")]     = j->op;          // shield | deshield | private
        entry[QStringLiteral("asset")]    = j->asset;       // native | token
        entry[QStringLiteral("sender")]   = j->from;
        entry[QStringLiteral("receiver")] = j->to.isEmpty() ? QStringLiteral("(foreign)") : j->to;
        entry[QStringLiteral("amount")]   = j->amount;
        entry[QStringLiteral("txId")]     = txId;
        entry[QStringLiteral("ts")]       = QDateTime::currentDateTime().toString(Qt::ISODate);
        saveTx(j->from, entry);
        if (!j->to.isEmpty())
            saveTx(j->to, entry);
    }

    appendLog(QStringLiteral("%1 %2 (%3)").arg(j->op, j->state, jobId),
              success ? QStringLiteral("info") : QStringLiteral("error"));

    // Best-effort notification for any QML listener; polling getJob() is authoritative.
    emit eventResponse(j->op, QVariantList{ jobId, j->state });

    proc->deleteLater();
    j->proc = nullptr;

    startQueuedBehind(jobId);
}

// Launch anything sequenced behind `jobId`, WHATEVER that job answered. The faucet's two halves
// are sequenced (see startTokenFaucet) so they never drive the wallet store at the same time,
// but they are not conditional on each other: their cooldowns are independent, so a native claim
// that was refused - or that never started - must not suppress a token claim that would have
// worked. Called from both places a job can become terminal.
//
// Collected first, then started: startJobProcess does not touch m_jobs, but a launch that fails
// instantly re-enters through errorOccurred, and iterating a container across that is how a rare
// crash gets written.
void WalletPlugin::startQueuedBehind(const QString& jobId)
{
    QStringList waiting;
    for (auto it = m_jobs.constBegin(); it != m_jobs.constEnd(); ++it) {
        const Job* q = it.value();
        if (q->waitFor == jobId && q->state == QStringLiteral("running") && !q->proc
            && !q->pendingBin.isEmpty())
            waiting << q->id;
    }
    for (const QString& qid : std::as_const(waiting))
        if (Job* q = m_jobs.value(qid, nullptr))
            startJobProcess(q, q->pendingBin, q->pendingArgs, q->pendingOwnBin);
}

QString WalletPlugin::getJob(const QString& jobId)
{
    Job* j = m_jobs.value(jobId.trimmed(), nullptr);
    if (!j)
        return errorJson(QStringLiteral("unknown jobId: ") + jobId.trimmed());

    // SELF-HEAL A QUEUED JOB. A job held behind another is started by startQueuedBehind() from
    // the ahead job's onJobFinished, with a timer as a backstop. Both are one-shot pushes, and
    // if either misses the job sits in "queued" forever with no child: observed live, a token
    // faucet half still queued 13 minutes after its native half had finished and a later
    // transfer had come and gone, so the module was demonstrably healthy the whole time.
    //
    // The push is not the right shape for this. The UI already polls THIS function once a
    // second for exactly these jobs, so the condition is re-checked here instead: if whatever
    // it waits for is no longer running (finished, errored, or gone from m_jobs entirely), the
    // wait is over and the child starts on the very next poll. A pull cannot be missed the way
    // a single push can, and it needs no new machinery.
    if (!j->waitFor.isEmpty() && !j->pendingBin.isEmpty() && !j->proc
        && j->state == QStringLiteral("running")) {
        const Job* ahead = m_jobs.value(j->waitFor, nullptr);
        if (!ahead || ahead->state != QStringLiteral("running")) {
            appendLog(QStringLiteral("job %1 was queued behind %2, which is no longer running: "
                                     "starting it now").arg(j->id, j->waitFor));
            startJobProcess(j, j->pendingBin, j->pendingArgs, j->pendingOwnBin);
        }
    }

    QJsonObject o;
    o[QStringLiteral("jobId")]     = j->id;
    o[QStringLiteral("op")]        = j->op;
    o[QStringLiteral("asset")]     = j->asset;
    o[QStringLiteral("from")]      = j->from;
    o[QStringLiteral("to")]        = j->to;
    o[QStringLiteral("amount")]    = j->amount;
    o[QStringLiteral("state")]     = j->state;
    o[QStringLiteral("phase")]     = j->phase;
    o[QStringLiteral("elapsedMs")] = static_cast<double>(j->timer.elapsed());

    if (j->state != QStringLiteral("running")) {
        QJsonObject r = QJsonDocument::fromJson(j->result.toUtf8()).object();
        o[QStringLiteral("result")] = r;
        if (j->state == QStringLiteral("done")) {
            o[QStringLiteral("txId")] = extractTxHash(j->result);
        } else {
            o[QStringLiteral("error")] = r.value(QStringLiteral("error")).toString(
                QStringLiteral("privacy transfer failed"));
            // A machine-readable code beside the sentence, using the module's existing `reason`
            // convention, so the UI can act on a failure class instead of matching on prose.
            // Only present when the failure has one (today: the faucet's "not-funded"), and the
            // raw message travels with it so an operator can still see what the chain said.
            if (r.contains(QStringLiteral("reason")))
                o[QStringLiteral("reason")] = r.value(QStringLiteral("reason"));
            if (r.contains(QStringLiteral("rawError")))
                o[QStringLiteral("rawError")] = r.value(QStringLiteral("rawError"));
        }
    }
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

// ── Medusa-Connect (sessions + per-action approval) ─────────────────────────────
// The wire contract is docs/MEDUSA_CONNECT_CONTRACT.md; this is the C++ half. The
// dApp (via the @paradoxcomputer/medusa-connect SDK) calls these over the bridge;
// the wallet QML approval sheets call approveConnect/approveAction/rejectConnect.

QString WalletPlugin::newSessionId()
{
    // 8 random bytes, hex → "ses-<hex16>". Opaque to JS.
    quint32 a = QRandomGenerator::global()->generate();
    quint32 b = QRandomGenerator::global()->generate();
    return QStringLiteral("ses-%1%2")
        .arg(a, 8, 16, QLatin1Char('0'))
        .arg(b, 8, 16, QLatin1Char('0'));
}

QString WalletPlugin::permForOp(const QString& op)
{
    // op → required permission. send→send, shield→shield, deshield→deshield, private→private.
    const QString o = op.trimmed().toLower();
    if (o == QStringLiteral("shield"))   return QStringLiteral("shield");
    if (o == QStringLiteral("deshield")) return QStringLiteral("deshield");
    if (o == QStringLiteral("private"))  return QStringLiteral("private");
    return QStringLiteral("send");
}

void WalletPlugin::evictOldConnRequests()
{
    if (m_requests.size() < kMaxConnRequests) return;
    // Drop oldest terminal (approved/rejected) requests first; never drop a pending one.
    QList<ConnectRequest*> terminal;
    for (auto it = m_requests.constBegin(); it != m_requests.constEnd(); ++it)
        if (it.value()->state != QStringLiteral("pending"))
            terminal.append(it.value());
    std::sort(terminal.begin(), terminal.end(),
              [](const ConnectRequest* a, const ConnectRequest* b) { return a->seq < b->seq; });
    for (ConnectRequest* r : terminal) {
        if (m_requests.size() < kMaxConnRequests) break;
        m_requests.remove(r->id);
        delete r;
    }
}

QJsonObject WalletPlugin::pendingRequestJson(const ConnectRequest* r) const
{
    QJsonObject o;
    o[QStringLiteral("requestId")] = r->id;
    o[QStringLiteral("kind")]      = r->kind;
    if (r->kind == QStringLiteral("connect")) {
        QJsonObject app;
        app[QStringLiteral("appName")] = r->appName;
        app[QStringLiteral("icon")]    = r->appIcon;
        app[QStringLiteral("origin")]  = r->origin;
        o[QStringLiteral("app")]   = app;
        o[QStringLiteral("perms")] = QJsonArray::fromStringList(r->perms);
    } else if (r->kind == QStringLiteral("zone")) {
        // The QML approval sheet renders "<appName> wants to switch to <label> (<sequencer>)".
        const ConnectSession* zs = m_sessions.value(r->sessionId, nullptr);
        o[QStringLiteral("sessionId")] = r->sessionId;
        o[QStringLiteral("appName")]   = zs ? zs->appName : QString();
        o[QStringLiteral("sequencer")] = r->zoneSeq;
        o[QStringLiteral("label")]     = r->zoneLabel;
        o[QStringLiteral("tor")]       = r->zoneTor;
    } else {
        o[QStringLiteral("sessionId")]    = r->sessionId;
        o[QStringLiteral("op")]           = r->op;
        o[QStringLiteral("asset")]        = r->asset;
        o[QStringLiteral("definitionId")] = r->definitionId;
        o[QStringLiteral("from")]         = r->from;
        o[QStringLiteral("to")]           = r->to;
        o[QStringLiteral("amount")]       = r->amount;
        o[QStringLiteral("toNpk")]        = r->toNpk;
        o[QStringLiteral("toVpk")]        = r->toVpk;
        o[QStringLiteral("toIdentifier")] = r->toIdentifier;
    }
    o[QStringLiteral("ts")] = r->createdTs;
    return o;
}

QString WalletPlugin::connectRequest(const QString& appJson, const QString& permsJson)
{
    const QJsonObject app = QJsonDocument::fromJson(appJson.toUtf8()).object();
    const QString appName = app.value(QStringLiteral("appName")).toString().trimmed();
    if (appName.isEmpty())
        return errorJson(QStringLiteral("appName is required"));

    // Filter the requested perms to the known literals; drop unknowns silently.
    static const QStringList kKnownPerms{
        QStringLiteral("accounts"), QStringLiteral("send"), QStringLiteral("shield"),
        QStringLiteral("deshield"), QStringLiteral("private"), QStringLiteral("zone") };
    const QJsonArray inPerms = QJsonDocument::fromJson(permsJson.toUtf8()).array();
    QStringList perms;
    for (const auto& v : inPerms) {
        const QString p = v.toString();
        if (kKnownPerms.contains(p) && !perms.contains(p))
            perms.append(p);
    }
    if (perms.isEmpty())
        return errorJson(QStringLiteral("at least one permission is required"));

    evictOldConnRequests();

    ConnectRequest* r = new ConnectRequest;
    r->id        = QStringLiteral("req-%1").arg(++m_connReqSeq);
    r->kind      = QStringLiteral("connect");
    r->state     = QStringLiteral("pending");
    r->appName   = appName;
    r->appIcon   = app.value(QStringLiteral("icon")).toString();
    r->origin    = app.value(QStringLiteral("origin")).toString();
    r->perms     = perms;
    r->createdTs = QDateTime::currentDateTime().toString(Qt::ISODate);
    r->createdMs = QDateTime::currentMSecsSinceEpoch();
    r->seq       = m_connReqSeq;
    m_requests.insert(r->id, r);

    appendLog(QStringLiteral("connect request %1 from %2").arg(r->id, appName));

    QJsonObject o;
    o[QStringLiteral("requestId")] = r->id;
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

QString WalletPlugin::pendingRequests()
{
    // Only state=="pending", newest first.
    QList<ConnectRequest*> pend;
    for (auto it = m_requests.constBegin(); it != m_requests.constEnd(); ++it)
        if (it.value()->state == QStringLiteral("pending"))
            pend.append(it.value());
    std::sort(pend.begin(), pend.end(),
              [](const ConnectRequest* a, const ConnectRequest* b) { return a->seq > b->seq; });

    QJsonArray arr;
    for (const ConnectRequest* r : pend)
        arr.append(pendingRequestJson(r));
    return QJsonDocument(arr).toJson(QJsonDocument::Compact);
}

QString WalletPlugin::approveConnect(const QString& requestId, const QString& accountsJson)
{
    ConnectRequest* r = m_requests.value(requestId.trimmed(), nullptr);
    if (!r || r->state != QStringLiteral("pending"))
        return errorJson(QStringLiteral("unknown or already-handled request"));
    if (r->kind != QStringLiteral("connect"))
        return errorJson(QStringLiteral("not a connect request"));

    QStringList accounts;
    const QJsonArray inAcc = QJsonDocument::fromJson(accountsJson.toUtf8()).array();
    for (const auto& v : inAcc) {
        const QString a = v.toString().trimmed();
        if (!a.isEmpty() && !accounts.contains(a))
            accounts.append(a);
    }

    ConnectSession* s = new ConnectSession;
    s->id        = newSessionId();
    s->appName   = r->appName;
    s->appIcon   = r->appIcon;
    s->origin    = r->origin;
    s->accounts  = accounts;
    s->perms     = r->perms;           // granted == requested (already filtered at connectRequest)
    s->zone      = netId();
    s->createdTs = QDateTime::currentDateTime().toString(Qt::ISODate);
    m_sessions.insert(s->id, s);

    r->state         = QStringLiteral("approved");
    r->sessionMinted = s->id;          // so actionStatus(req) can hand the SDK its sessionId

    appendLog(QStringLiteral("connect %1 approved → %2 (%3 accts)")
                  .arg(r->id, s->id).arg(accounts.size()));

    QJsonObject o;
    o[QStringLiteral("sessionId")] = s->id;
    o[QStringLiteral("accounts")]  = QJsonArray::fromStringList(s->accounts);
    o[QStringLiteral("granted")]   = QJsonArray::fromStringList(s->perms);
    o[QStringLiteral("zone")]      = s->zone;
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

QString WalletPlugin::rejectConnect(const QString& requestId)
{
    // Shared reject verb for BOTH connect- and action-kind pending requests (§1.5).
    ConnectRequest* r = m_requests.value(requestId.trimmed(), nullptr);
    if (!r || r->state != QStringLiteral("pending"))
        return errorJson(QStringLiteral("unknown or already-handled request"));
    r->state = QStringLiteral("rejected");
    r->error = QStringLiteral("user rejected");
    appendLog(QStringLiteral("request %1 rejected").arg(r->id));
    return okJson();
}

QString WalletPlugin::sessionInfo(const QString& sessionId)
{
    ConnectSession* s = m_sessions.value(sessionId.trimmed(), nullptr);
    if (!s)
        return errorJson(QStringLiteral("no such session"));

    QJsonObject app;
    app[QStringLiteral("appName")] = s->appName;
    app[QStringLiteral("icon")]    = s->appIcon;
    app[QStringLiteral("origin")]  = s->origin;

    QJsonObject o;
    o[QStringLiteral("sessionId")] = s->id;
    o[QStringLiteral("app")]       = app;
    // The account list is exposed only if the "accounts" permission was granted.
    o[QStringLiteral("accounts")]  = s->perms.contains(QStringLiteral("accounts"))
                                   ? QJsonArray::fromStringList(s->accounts) : QJsonArray();
    o[QStringLiteral("granted")]       = QJsonArray::fromStringList(s->perms);
    o[QStringLiteral("zone")]          = netId();    // the LIVE active zone (may differ from connect)
    o[QStringLiteral("zoneAtConnect")] = s->zone;
    o[QStringLiteral("active")]        = true;
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

QString WalletPlugin::requestAction(const QString& sessionId, const QString& actionJson)
{
    ConnectSession* s = m_sessions.value(sessionId.trimmed(), nullptr);
    if (!s)
        return errorJson(QStringLiteral("no such session"));

    const QJsonObject a = QJsonDocument::fromJson(actionJson.toUtf8()).object();
    QString op    = a.value(QStringLiteral("op")).toString().trimmed().toLower();
    const QString asset = (a.value(QStringLiteral("asset")).toString().trimmed().toLower()
                           == QStringLiteral("token"))
                        ? QStringLiteral("token") : QStringLiteral("native");
    const QString definitionId = a.value(QStringLiteral("definitionId")).toString().trimmed();
    const QString from   = a.value(QStringLiteral("from")).toString().trimmed();
    const QString to     = a.value(QStringLiteral("to")).toString().trimmed();
    const QString amount = a.value(QStringLiteral("amount")).toString().trimmed();
    const QString toNpk  = a.value(QStringLiteral("toNpk")).toString().trimmed();
    const QString toVpk  = a.value(QStringLiteral("toVpk")).toString().trimmed();
    const QString toId   = a.value(QStringLiteral("toIdentifier")).toString().trimmed();

    // Auto-derive op from prefixes when omitted (mirror of the SDK's send() auto-detect).
    if (op.isEmpty()) {
        const bool fromPriv = from.toLower().startsWith(QStringLiteral("private/"));
        const bool toPriv   = to.toLower().startsWith(QStringLiteral("private/"));
        if (!fromPriv && !toPriv)      op = QStringLiteral("send");
        else if (!fromPriv && toPriv)  op = QStringLiteral("shield");
        else if (fromPriv && !toPriv)  op = QStringLiteral("deshield");
        else                           op = QStringLiteral("private");
    }
    static const QStringList kOps{ QStringLiteral("send"), QStringLiteral("shield"),
                                   QStringLiteral("deshield"), QStringLiteral("private") };
    if (!kOps.contains(op))
        return errorJson(QStringLiteral("unknown op: ") + op);

    // Permission gate.
    const QString needPerm = permForOp(op);
    if (!s->perms.contains(needPerm))
        return errorJson(QStringLiteral("permission not granted: ") + needPerm);

    // The spending account must be one the session exposed.
    if (!s->accounts.contains(from))
        return errorJson(QStringLiteral("account not authorized for this session"));

    // Amount must be a whole non-negative integer (wallet-side re-validation).
    static const QRegularExpression amtRe(QStringLiteral("^[0-9]+$"));
    if (!amtRe.match(amount).hasMatch())
        return errorJson(QStringLiteral("amounts are whole numbers - no decimals"));

    evictOldConnRequests();

    ConnectRequest* r = new ConnectRequest;
    r->id           = QStringLiteral("req-%1").arg(++m_connReqSeq);
    r->kind         = QStringLiteral("action");
    r->state        = QStringLiteral("pending");
    r->sessionId    = s->id;
    r->op           = op;
    r->asset        = asset;
    r->definitionId = definitionId;
    r->from         = from;
    r->to           = to;
    r->amount       = amount;
    r->toNpk        = toNpk;
    r->toVpk        = toVpk;
    r->toIdentifier = toId;
    r->createdTs    = QDateTime::currentDateTime().toString(Qt::ISODate);
    r->createdMs    = QDateTime::currentMSecsSinceEpoch();
    r->seq          = m_connReqSeq;
    m_requests.insert(r->id, r);

    appendLog(QStringLiteral("action request %1 (%2) on %3").arg(r->id, op, s->id));

    QJsonObject o;
    o[QStringLiteral("requestId")] = r->id;
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

QString WalletPlugin::approveAction(const QString& requestId, const QString& password)
{
    // Nothing here can tell the wallet UI from the dApp that raised the request, so without the
    // password a module could run the whole chain against itself - connectRequest,
    // approveConnect, requestAction, approveAction - and move funds with no window ever shown.
    if (!authorize(password))
        return authRefusal();

    ConnectRequest* r = m_requests.value(requestId.trimmed(), nullptr);
    if (!r || r->state != QStringLiteral("pending"))
        return errorJson(QStringLiteral("unknown or already-handled request"));
    if (r->kind != QStringLiteral("action"))
        return errorJson(QStringLiteral("not an action request"));

    // Zone guard: the action must run on the same chain the session connected to. If the user
    // switched zones since connect, reject instead of silently acting on a different chain.
    ConnectSession* actSess = m_sessions.value(r->sessionId, nullptr);
    if (actSess && netId() != actSess->zone) {
        r->state = QStringLiteral("rejected");
        r->error = QStringLiteral("active zone changed since connect - reconnect");
        QJsonObject zo;
        zo[QStringLiteral("requestId")] = r->id;
        zo[QStringLiteral("status")]    = QStringLiteral("rejected");
        zo[QStringLiteral("error")]     = r->error;
        return QJsonDocument(zo).toJson(QJsonDocument::Compact);
    }

    // Dispatch to an EXISTING start* job - no new send/proof code (invariant §1). The gate was
    // already satisfied above, so the dispatch re-presents the established password rather than
    // duplicating each verb into a private ungated twin; if the wallet were locked, authorize()
    // would have refused before reaching here.
    // The request's (asset, definitionId, amount) is a value spec written out longhand, and its
    // (to | toNpk+toVpk+toIdentifier) is a recipient spec - assemble both here so the dispatch
    // still calls the very same verbs the UI calls, in their 4-argument form.
    QString value = r->amount;
    if (r->asset == QStringLiteral("token")) {
        QJsonObject v;
        v[QStringLiteral("asset")]        = QStringLiteral("token");
        v[QStringLiteral("definitionId")] = r->definitionId;
        v[QStringLiteral("amount")]       = r->amount;
        value = QString::fromUtf8(QJsonDocument(v).toJson(QJsonDocument::Compact));
    }

    QString started;
    if (r->op == QStringLiteral("send")) {
        started = (r->asset == QStringLiteral("token"))
                ? startSendToken(r->from, r->to, value, m_password)
                : startSendTransfer(r->from, r->to, value, m_password);
    } else if (r->op == QStringLiteral("shield")) {
        started = startShield(r->from, r->to, value, m_password);
    } else if (r->op == QStringLiteral("deshield")) {
        started = startDeshield(r->from, r->to, value, m_password);
    } else { // private
        QString recipient = r->to;
        if (recipient.isEmpty()) {   // foreign: the three shared keys as one recipient spec
            QJsonObject rc;
            rc[QStringLiteral("npk")]        = r->toNpk;
            rc[QStringLiteral("vpk")]        = r->toVpk;
            rc[QStringLiteral("identifier")] = r->toIdentifier;
            recipient = QString::fromUtf8(QJsonDocument(rc).toJson(QJsonDocument::Compact));
        }
        started = startPrivateTransfer(r->from, recipient, value, m_password);
    }

    const QJsonObject so = QJsonDocument::fromJson(started.toUtf8()).object();
    if (so.contains(QStringLiteral("error"))) {
        const QString msg = so.value(QStringLiteral("error")).toString();
        r->state = QStringLiteral("rejected");
        r->error = msg;
        QJsonObject o;
        o[QStringLiteral("requestId")] = r->id;
        o[QStringLiteral("status")]    = QStringLiteral("rejected");
        o[QStringLiteral("error")]     = msg;
        return QJsonDocument(o).toJson(QJsonDocument::Compact);
    }

    r->jobId = so.value(QStringLiteral("jobId")).toString();
    r->state = QStringLiteral("approved");

    appendLog(QStringLiteral("action %1 approved → %2").arg(r->id, r->jobId));

    QJsonObject o;
    o[QStringLiteral("requestId")] = r->id;
    o[QStringLiteral("status")]    = QStringLiteral("approved");
    o[QStringLiteral("jobId")]     = r->jobId;
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

QString WalletPlugin::actionStatus(const QString& requestId)
{
    // The single polling primitive for BOTH kinds: returns sessionId for an approved
    // connect, jobId for an approved action (contract invariant §5).
    ConnectRequest* r = m_requests.value(requestId.trimmed(), nullptr);
    if (!r)
        return errorJson(QStringLiteral("unknown request"));

    // Expire a pending request that's sat unapproved past the TTL, so a polling dApp gives up
    // (turns into a clean "rejected: approval timed out") instead of hanging forever.
    if (r->state == QStringLiteral("pending") && r->createdMs > 0
        && QDateTime::currentMSecsSinceEpoch() - r->createdMs > kReqTtlMs) {
        r->state = QStringLiteral("rejected");
        r->error = QStringLiteral("approval timed out");
    }

    QJsonObject o;
    o[QStringLiteral("requestId")] = r->id;
    o[QStringLiteral("status")]    = r->state;
    if (r->state == QStringLiteral("approved")) {
        if (r->kind == QStringLiteral("connect"))
            o[QStringLiteral("sessionId")] = r->sessionMinted;
        else if (r->kind == QStringLiteral("zone"))
            o[QStringLiteral("zoneId")] = r->zoneId;
        else
            o[QStringLiteral("jobId")] = r->jobId;
    } else if (r->state == QStringLiteral("rejected")) {
        o[QStringLiteral("error")] = r->error.isEmpty()
                                   ? QStringLiteral("user rejected") : r->error;
    }
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

// ── Connect with Medusa: dApp-requested zone switch (user-approved) ───────────────

QString WalletPlugin::requestZone(const QString& sessionId, const QString& zoneJson)
{
    // Mirror requestAction: the session must exist AND have granted the "zone" permission.
    ConnectSession* s = m_sessions.value(sessionId.trimmed(), nullptr);
    if (!s)
        return errorJson(QStringLiteral("no such session"));
    if (!s->perms.contains(QStringLiteral("zone")))
        return errorJson(QStringLiteral("permission not granted: zone"));

    const QJsonObject z = QJsonDocument::fromJson(zoneJson.toUtf8()).object();
    const QString sequencer = z.value(QStringLiteral("sequencer")).toString().trimmed();
    const bool    tor       = z.value(QStringLiteral("tor")).toBool();
    const QString label     = z.value(QStringLiteral("label")).toString().trimmed();
    if (sequencer.isEmpty())
        return errorJson(QStringLiteral("sequencer is required"));

    evictOldConnRequests();

    ConnectRequest* r = new ConnectRequest;
    r->id        = QStringLiteral("req-%1").arg(++m_connReqSeq);
    r->kind      = QStringLiteral("zone");
    r->state     = QStringLiteral("pending");
    r->sessionId = s->id;
    r->zoneSeq   = sequencer;
    r->zoneTor   = tor;
    r->zoneLabel = label;
    r->createdTs = QDateTime::currentDateTime().toString(Qt::ISODate);
    r->createdMs = QDateTime::currentMSecsSinceEpoch();
    r->seq       = m_connReqSeq;
    m_requests.insert(r->id, r);

    appendLog(QStringLiteral("zone request %1 (%2) on %3").arg(r->id, sequencer, s->id));

    QJsonObject o;
    o[QStringLiteral("requestId")] = r->id;
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

QString WalletPlugin::approveZone(const QString& requestId, const QString& password)
{
    // Gated exactly like approveAction, and for the same reason spelled with a different noun.
    // approveAction was gated because leaving it open reopened every spend verb through
    // connectRequest -> approveConnect -> requestAction -> approveAction; this is the identical
    // chain one function along, ending in setActiveZone(attacker's sequencer) with the wallet
    // LOCKED and nobody at the keyboard. A sequencer sees every public transaction, can censor
    // them, and supplies every balance the UI renders, so a fake incoming balance is a working
    // merchant scam and a missing one makes the user pay twice. The gate goes on the APPROVAL,
    // not on zone selection: addZone/setActiveZone stay open because onboarding and the lock
    // screen legitimately switch zones before there is any password to prove.
    if (!authorize(password))
        return authRefusal();

    ConnectRequest* r = m_requests.value(requestId.trimmed(), nullptr);
    if (!r || r->state != QStringLiteral("pending"))
        return errorJson(QStringLiteral("unknown or already-handled request"));
    if (r->kind != QStringLiteral("zone"))
        return errorJson(QStringLiteral("not a zone request"));

    // Resolve the zone: reuse an existing zone with the SAME transport whose endpoint matches
    // the requested sequencer (getZones exposes each zone's reachable endpoint), else add a
    // new remote zone. The tor flag must match too - a tor request must never silently reuse
    // a clearnet zone the approval sheet didn't show. Compare QUrl-normalized clearnet forms
    // (addZone stores scheme-less as "http://…"; slash/case variants are the same sequencer),
    // so a repeat approval reuses the zone instead of accreting z-<slug>-2/-3 duplicates.
    const auto epKey = [](const QString& ep, bool tor) {
        const QString t = ep.trimmed();
        if (tor) return t;
        const QString u = t.contains(QStringLiteral("://")) ? t : QStringLiteral("http://") + t;
        return QUrl(u).adjusted(QUrl::StripTrailingSlash).toString();
    };
    const QString wantEp = epKey(r->zoneSeq, r->zoneTor);
    QString zoneId;
    const QJsonObject zonesRes = QJsonDocument::fromJson(getZones().toUtf8()).object();
    for (const auto& v : zonesRes.value(QStringLiteral("zones")).toArray()) {
        const QJsonObject zo = v.toObject();
        const QString stored = zo.value(QStringLiteral("endpoint")).toString();
        if (stored.isEmpty() || zo.value(QStringLiteral("tor")).toBool() != r->zoneTor)
            continue;
        if (epKey(stored, r->zoneTor) == wantEp) {
            zoneId = zo.value(QStringLiteral("id")).toString();
            break;
        }
    }

    if (zoneId.isEmpty()) {
        // addZone(name, endpoint, tor): one endpoint, whichever transport it is reached over.
        const QString name = r->zoneLabel.isEmpty() ? r->zoneSeq : r->zoneLabel;
        const QString added = addZone(name, r->zoneSeq, r->zoneTor);
        const QJsonObject ao = QJsonDocument::fromJson(added.toUtf8()).object();
        if (ao.contains(QStringLiteral("error"))) {
            const QString msg = ao.value(QStringLiteral("error")).toString();
            r->state = QStringLiteral("rejected");
            r->error = msg;
            QJsonObject o;
            o[QStringLiteral("requestId")] = r->id;
            o[QStringLiteral("status")]    = QStringLiteral("rejected");
            o[QStringLiteral("error")]     = msg;
            return QJsonDocument(o).toJson(QJsonDocument::Compact);
        }
        zoneId = ao.value(QStringLiteral("id")).toString();
    }

    const QString switched = setActiveZone(zoneId);
    const QJsonObject sw = QJsonDocument::fromJson(switched.toUtf8()).object();
    if (sw.contains(QStringLiteral("error"))) {
        const QString msg = sw.value(QStringLiteral("error")).toString();
        r->state = QStringLiteral("rejected");
        r->error = msg;
        QJsonObject o;
        o[QStringLiteral("requestId")] = r->id;
        o[QStringLiteral("status")]    = QStringLiteral("rejected");
        o[QStringLiteral("error")]     = msg;
        return QJsonDocument(o).toJson(QJsonDocument::Compact);
    }

    r->zoneId = zoneId;
    r->state  = QStringLiteral("approved");

    // Re-pin the requesting session to the new zone, else approveAction's guard ("active zone
    // changed since connect") rejects every action the dApp just switched here to perform.
    // Other sessions keep their connect-time pin and still trip the guard by design.
    if (ConnectSession* zs = m_sessions.value(r->sessionId, nullptr))
        zs->zone = netId();

    // The re-pin must not retroactively legalize actions requested against the OLD chain:
    // reject the session's still-pending action requests - the dApp re-requests post-switch.
    for (auto it = m_requests.constBegin(); it != m_requests.constEnd(); ++it) {
        ConnectRequest* ar = it.value();
        if (ar->kind == QStringLiteral("action") && ar->sessionId == r->sessionId
            && ar->state == QStringLiteral("pending")) {
            ar->state = QStringLiteral("rejected");
            ar->error = QStringLiteral("zone switched before approval - re-request");
        }
    }

    appendLog(QStringLiteral("zone %1 approved → %2").arg(r->id, zoneId));

    QJsonObject o;
    o[QStringLiteral("requestId")] = r->id;
    o[QStringLiteral("status")]    = QStringLiteral("approved");
    o[QStringLiteral("zoneId")]    = zoneId;
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

QString WalletPlugin::rejectZone(const QString& requestId)
{
    ConnectRequest* r = m_requests.value(requestId.trimmed(), nullptr);
    if (!r || r->state != QStringLiteral("pending"))
        return errorJson(QStringLiteral("unknown or already-handled request"));
    if (r->kind != QStringLiteral("zone"))
        return errorJson(QStringLiteral("not a zone request"));
    r->state = QStringLiteral("rejected");
    r->error = QStringLiteral("user rejected");
    appendLog(QStringLiteral("zone request %1 rejected").arg(r->id));

    QJsonObject o;
    o[QStringLiteral("requestId")] = r->id;
    o[QStringLiteral("status")]    = QStringLiteral("rejected");
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

QString WalletPlugin::revokeSession(const QString& sessionId)
{
    // Idempotent - disconnect must never fail.
    ConnectSession* s = m_sessions.take(sessionId.trimmed());
    if (s) {
        // Reject still-pending action AND zone requests, and cancel any approved action whose
        // proving job is still running, so nothing keeps proving - or switching the wallet's
        // zone - for a disconnected session.
        for (auto it = m_requests.constBegin(); it != m_requests.constEnd(); ++it) {
            ConnectRequest* r = it.value();
            if (r->sessionId != s->id
                || (r->kind != QStringLiteral("action") && r->kind != QStringLiteral("zone")))
                continue;
            if (r->state == QStringLiteral("pending")) {
                r->state = QStringLiteral("rejected");
                r->error = QStringLiteral("session revoked");
            } else if (r->state == QStringLiteral("approved") && !r->jobId.isEmpty()) {
                Job* job = m_jobs.value(r->jobId, nullptr);
                if (job && job->state == QStringLiteral("running") && job->proc)
                    job->proc->kill();   // the process-finished handler marks it error
            }
        }
        appendLog(QStringLiteral("session %1 revoked").arg(s->id));
        delete s;
    }
    return okJson();
}

// ── Wallet security: encrypted-storage unlock ───────────────────────────────────

int WalletPlugin::idleLockMs()
{
    bool ok = false;
    const int ms = qEnvironmentVariable("MEDUSA_IDLE_LOCK_MS").toInt(&ok);
    if (!ok || ms < 100)   // a floor, so a typo can't turn the lock into a busy loop
        return kIdleLockMs;
    return ms;
}

void WalletPlugin::touchActivity()
{
    m_lastActivityMs = QDateTime::currentMSecsSinceEpoch();
    if (m_password.isEmpty()) {          // nothing is being held - no timer needs to run
        if (m_idleLock) m_idleLock->stop();
        return;
    }
    if (!m_idleLock) {
        m_idleLock = new QTimer(this);   // parented: destroyed with the plugin
        m_idleLock->setSingleShot(true);
        m_idleLock->setTimerType(Qt::VeryCoarseTimer);   // a lock clock needs no ms precision
        QObject::connect(m_idleLock, &QTimer::timeout, this, [this]() {
            // A real proof runs 20-40 minutes and the user is waiting on it, not away from the
            // machine; locking underneath it would also leave the UI unable to refresh when it
            // lands. An in-flight job therefore counts as activity.
            for (auto it = m_jobs.constBegin(); it != m_jobs.constEnd(); ++it) {
                if (it.value()->state == QStringLiteral("running")) {
                    m_idleLock->start(idleLockMs());
                    return;
                }
            }
            if (m_password.isEmpty())
                return;
            clearSessionPassword();
            appendLog(QStringLiteral("auto-locked after %1 ms idle").arg(idleLockMs()));
        });
    }
    m_idleLock->start(idleLockMs());
}

// ── INVARIANT A: the one writer and the one clear ─────────────────────────────────────────────
//
// establishSession() below is the ONLY function in this file that assigns m_password, and
// clearSessionPassword() is the ONLY one that clears it. Two greps state the whole rule:
//
//     grep -n "m_password = "       WalletPlugin.cpp   -> one line, inside establishSession()
//     grep -n "m_password.clear()"  WalletPlugin.cpp   -> one line, inside clearSessionPassword()
//
// Round 1 deleted setSessionPassword, which was the front door. Round 2's rule ("a value unlock()
// verified, or one the module just SEALED the store with") still had two side doors, because two
// ungated verbs could cause a seal: encryptPlaintextWallet directly, and createEncryptedWallet
// once an ungated resetWallet had removed the store that made it refuse. Both then handed the
// caller a live session. The lesson is that "the store opens with this password" is only evidence
// of who the caller is when the caller did NOT choose what the store was sealed with - so sealing
// grants nothing here, and the two sealing verbs leave the wallet locked.

QString WalletPlugin::establishSession(const QString& candidate)
{
    // No store: `account list` on an empty wallet home makes the CLI CREATE one, sealed with
    // whatever password it was handed - so without this check unlock("anything") on a fresh
    // install created a wallet and returned a live session for it. That is a credential-free mint
    // through unlock() itself, and it is why this check comes before anything else.
    if (!storageExists())
        return errorJson(QStringLiteral("there is no wallet here to unlock - create or restore "
                                        "one first"),
                         QStringLiteral("no-wallet"));
    // A plaintext store cannot verify anything: the CLI opens it with ANY password. Establishing
    // a session against one would install a credential of the caller's choosing and, worse, would
    // let the UI tell the user they are protected. The verbs still work there (see authorize());
    // what is refused is the CLAIM.
    if (storageIsPlaintext())
        return errorJson(QStringLiteral("this wallet's storage is not encrypted - there is no "
                                        "password to unlock; set one on it first"),
                         QStringLiteral("unencrypted"));

    // The backoff lives here rather than in unlock(), because every verb that can ask "does this
    // password open the store?" is an unlock oracle by another name. resetWallet and restoreWallet
    // both ask, and if they asked through their own code path they would be an unthrottled way to
    // grind a password that unlock() throttles.
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (m_unlockRetryAtMs > now) {
        QJsonObject o;
        o[QStringLiteral("error")]  = QStringLiteral("too many failed unlock attempts - wait a moment");
        o[QStringLiteral("reason")] = QStringLiteral("rate-limited");
        o[QStringLiteral("retryAfterMs")] = m_unlockRetryAtMs - now;
        return QJsonDocument(o).toJson(QJsonDocument::Compact);
    }

    // Probe with a LOCAL account list (no `-l` → no chain/balance fetch), so this is fast and
    // works even when the active zone's sequencer is slow/unreachable (e.g. diaphani over Tor).
    // A decryption failure still means the password was wrong. The candidate goes on the CLI's
    // stdin EXPLICITLY: m_password is not touched until the store has opened, so a wrong guess
    // cannot disturb a session that is already working.
    const QString result = runWalletCommandInput({ QStringLiteral("account"), QStringLiteral("list") },
                                                 candidate + QStringLiteral("\n"));
    const QJsonObject o = QJsonDocument::fromJson(result.toUtf8()).object();
    if (o.contains(QStringLiteral("error"))) {
        const QString err = o.value(QStringLiteral("error")).toString();
        if (err.contains(QStringLiteral("decrypt"), Qt::CaseInsensitive)
            || err.contains(QStringLiteral("invalid password"), Qt::CaseInsensitive)) {
            if (++m_unlockFails > kUnlockFreeAttempts) {
                const int shift = qMin(m_unlockFails - kUnlockFreeAttempts - 1, 20);
                const qint64 wait = qMin<qint64>(qint64(kUnlockBackoffBaseMs) << shift,
                                                 kUnlockBackoffMaxMs);
                m_unlockRetryAtMs = QDateTime::currentMSecsSinceEpoch() + wait;
            }
            return errorJson(QStringLiteral("invalid password"), QStringLiteral("unauthorized"));
        }
        return result;   // an unrelated failure (zone down, CLI missing) - not a wrong guess
    }

    m_password = candidate;   // ← THE ONLY ASSIGNMENT TO m_password IN THIS FILE
    m_unlockFails = 0;
    m_unlockRetryAtMs = 0;
    touchActivity();
    return result;            // the account list
}

QString WalletPlugin::clearSessionPassword()
{
    // The lock verb: deliberately ungated, since needing a password to lock would be absurd.
    m_password.clear();   // ← THE ONLY CLEAR OF m_password IN THIS FILE
    if (m_idleLock)
        m_idleLock->stop();
    // The log ring can hold truncated CLI output from before the redaction landed, and it has no
    // reader, so nothing is lost by dropping it. Note what this does NOT do: QString::clear()
    // returns the buffer to the allocator without zeroing it, and runWalletCommand builds a
    // password + "\n" copy plus a toUtf8() copy on every call, so the plaintext can still be
    // recovered from a core dump or a swapped-out page after a lock. Closing that needs a
    // wiping string type end to end, not a clear() here.
    m_log.clear();
    appendLog(QStringLiteral("session locked"));
    return okJson();
}

QString WalletPlugin::getSecurityState() const
{
    QJsonObject o;
    o[QStringLiteral("hasPassword")] = !m_password.isEmpty();
    o[QStringLiteral("autoLockMs")]  = idleLockMs();
    o[QStringLiteral("idleMs")]      = (m_password.isEmpty() || m_lastActivityMs == 0)
        ? 0 : (QDateTime::currentMSecsSinceEpoch() - m_lastActivityMs);
    // Say it out loud, in two fields the UI cannot misread. `unencrypted` is the fact; `protected`
    // is the verdict, and it is FALSE on a plaintext store even when a password is held, because
    // holding one there protects nothing. The gate does not refuse (see authorize()) - refusing
    // would deny the attacker nothing, since the keys are in a file they can read, and would deny
    // the owner their own wallet - so this field is where the honesty lives.
    const bool plain = storageIsPlaintext();
    o[QStringLiteral("unencrypted")] = plain;
    o[QStringLiteral("protected")]   = !plain;
    if (plain)
        o[QStringLiteral("warning")] = QStringLiteral(
            "this wallet's storage is NOT encrypted: every key in it can be read by any program "
            "running as you, and no password can change that. Set a password on it to secure it.");
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

QString WalletPlugin::unlock(const QString& password)
{
    // The whole verb is establishSession(): the no-store, plaintext, backoff and verification
    // rules all live there because resetWallet and restoreWallet need exactly the same ones, and
    // a second copy of them is how a bypass gets built.
    return establishSession(password);
}

// Defined further down with the other storage helpers; both sealing verbs need it to put a store
// they could not make openable out of the user's way.
static QString backupWalletStorage(const QString& storage, bool* failed);

// A displaced-store path that is guaranteed not to exist yet. The aside/backup names are
// second-granular, and the code used to REMOVE a colliding one ("a second call inside the same
// second"). That falsified the guarantee the whole ungated reset/restore design rests on -
// "nothing is deleted, the old store is always renamed aside" - and it was observable: two
// backup-producing operations in the same wall-clock second and the first one's file, which could
// be the user's only copy of an encrypted wallet, was gone. Nothing is removed here; a colliding
// name gets a counter. "" only if a thousand copies already exist in the same second, and the
// callers treat that as a failure rather than deleting anything.
static QString uniqueAsidePath(const QString& base)
{
    if (!QFile::exists(base))
        return base;
    for (int n = 2; n < 1000; ++n) {
        const QString cand = base + QStringLiteral("-%1").arg(n);
        if (!QFile::exists(cand))
            return cand;
    }
    return QString();
}

QString WalletPlugin::createEncryptedWallet(const QString& password)
{
    if (password.trimmed().isEmpty())
        return errorJson(QStringLiteral("a non-empty password is required to encrypt the wallet"));

    // ONBOARDING ONLY. Ungated and with no existence check this was a one-call takeover of a
    // legacy plaintext wallet: the CLI re-serialises the whole store on any write and seals it
    // with whatever password it was handed, so a caller could re-encrypt the victim's own
    // accounts under a password only the caller knew. The owner could no longer open the wallet,
    // the caller could, and there was no way back - a plaintext store never held a mnemonic, so
    // there is no phrase to restore from. Refuse whenever a store already exists. The legitimate
    // case this used to cover, a plaintext user setting a password for the first time, is
    // encryptPlaintextWallet(), which keeps a copy of what it re-seals.
    if (storageIsPlaintext())
        return errorJson(QStringLiteral("this wallet already exists and is not encrypted - "
                                        "encrypt it (keeping its accounts) instead of "
                                        "re-creating it"),
                         QStringLiteral("wallet-not-encrypted"));
    if (storageExists())
        return errorJson(QStringLiteral("a wallet already exists here - unlock it, or reset / "
                                        "restore before creating a new one"),
                         QStringLiteral("wallet-exists"));

    // The password goes to the CLI's stdin EXPLICITLY. It is deliberately not parked in
    // m_password first: that is what made this verb a session mint after an ungated resetWallet
    // had cleared the store out of its way (invariant A).
    // The first command on empty storage creates the (encrypted) wallet; creating a
    // public account is the natural trigger and yields a first usable account.
    const QString result = runWalletCommandInput(
        { QStringLiteral("account"), QStringLiteral("new"), QStringLiteral("public") },
        password + QStringLiteral("\n"), 60000);
    const QJsonObject o = QJsonDocument::fromJson(result.toUtf8()).object();
    if (o.contains(QStringLiteral("error")))
        return result;
    if (!storageExists())
        return errorJson(QStringLiteral("the wallet CLI reported success but wrote no wallet at ")
                             + storagePath(),
                         QStringLiteral("not-created"));
    if (storageIsPlaintext()) {
        // The CLI reported success but wrote an unencrypted store (an engine built without
        // encrypted storage would do exactly this). Never return {ok} here: the user would be
        // told their wallet is password-protected while nothing protects it. The store is LEFT
        // IN PLACE, not deleted - it holds a real key now - and getWalletState will report it as
        // the unprotected wallet it is, which is a state the user can still act on.
        return errorJson(QStringLiteral("the wallet CLI created an UNENCRYPTED store - this build "
                                        "cannot password-protect a wallet"),
                         QStringLiteral("not-encrypted"));
    }

    // Prove the store that was just written actually OPENS with the password the user chose,
    // before telling them it is theirs. Then drop the session again: see below.
    const QString probe = establishSession(password);
    if (QJsonDocument::fromJson(probe.toUtf8()).object().contains(QStringLiteral("error"))) {
        bool failed = false;
        const QString aside = backupWalletStorage(storagePath(), &failed);
        QJsonObject bad;
        bad[QStringLiteral("error")]  = QStringLiteral("the wallet CLI sealed a store that will "
            "not open with the password you chose - nothing was kept");
        bad[QStringLiteral("reason")] = QStringLiteral("not-encrypted");
        if (!aside.isEmpty()) bad[QStringLiteral("backup")] = aside;
        return QJsonDocument(bad).toJson(QJsonDocument::Compact);
    }

    QJsonObject out;
    out[QStringLiteral("ok")] = true;
    enrichFromOutput(result, out);   // parses "account_id Public/<id>" from the text
    // (the account registers on-chain lazily on its first faucet claim - kept fast here)
    // Fetch the recovery phrase cleanly via export rather than scraping create output.
    const QJsonObject mn = QJsonDocument::fromJson(exportMnemonic(password).toUtf8()).object();
    if (mn.value(QStringLiteral("ok")).toBool())
        out[QStringLiteral("mnemonic")] = mn.value(QStringLiteral("mnemonic")).toString();

    // CREATING A WALLET AND HOLDING ITS SESSION ARE SEPARATE CONCERNS (invariant A). The password
    // just verified against this store is one the CALLER chose and the CALLER installed a moment
    // ago, so "it opens" is not evidence about who the caller is - the only session provenance
    // this module accepts is a password proved against a store the caller did not just write. The
    // cost to a genuine first-run user is one unlock with the password they chose seconds ago,
    // which is also a real confirmation that the store opens; the benefit is that no future verb
    // that seals a store can ever be composed into a session mint again, which is exactly how
    // rounds 1 and 2 were defeated. `locked` says so explicitly, so the UI never has to guess.
    clearSessionPassword();
    out[QStringLiteral("locked")] = true;
    out[QStringLiteral("note")]   = QStringLiteral(
        "unlock with the password you just chose to start using this wallet");
    return QJsonDocument(out).toJson(QJsonDocument::Compact);
}

// Encrypt a legacy PLAINTEXT store in place, keeping its accounts. This is the half of the old
// createEncryptedWallet that was legitimate, split out and named for what it is.
//
// It cannot demand a credential, and no version of it ever could: the store is plaintext, so
// every key in it is already readable by anything running at this uid, and a co-resident module
// can call this exactly as the user can. What IS guaranteed is that it is reversible - the
// plaintext store is COPIED aside first (copied, not renamed: the CLI has to find it in place to
// re-seal it), so a migration nobody asked for costs one file copy to undo instead of the wallet.
// That is the same resolution resetWallet and restoreWallet already use for the same reason.
QString WalletPlugin::encryptPlaintextWallet(const QString& password)
{
    if (password.trimmed().isEmpty())
        return errorJson(QStringLiteral("a non-empty password is required to encrypt the wallet"));
    if (!storageExists())
        return errorJson(QStringLiteral("there is no wallet here to encrypt"),
                         QStringLiteral("no-wallet"));
    // A LIVE SESSION means unlock() verified a password against an ENCRYPTED store - it refuses
    // plaintext ones - so this cannot be a genuine plaintext migration whatever the file header
    // currently says. Refusing here removes the last way the coerced-plaintext reading that
    // defeated round 3's authorize() could still reach a verb that REWRITES the store, and it
    // costs the real plaintext user nothing: they never hold a session, because there is nothing
    // for unlock() to verify. (Deciding on the session rather than on the file is the whole point:
    // the file check below stays, but it can no longer be the only thing standing here.)
    if (!m_password.isEmpty())
        return errorJson(QStringLiteral("this wallet is unlocked, so it is already encrypted - "
                                        "lock it first if you meant to work on a different store"),
                         QStringLiteral("already-encrypted"));
    if (!storageIsPlaintext())
        return errorJson(QStringLiteral("this wallet is already encrypted"),
                         QStringLiteral("already-encrypted"));

    const QString storage = storagePath();
    // Never overwrite an existing copy-aside: it may be the ONLY copy of a wallet (see
    // uniqueAsidePath).
    const QString backup = uniqueAsidePath(storage + QStringLiteral(".plain-")
        + QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-hhmmss")));
    if (backup.isEmpty() || !QFile::copy(storage, backup))
        return errorJson(QStringLiteral("could not copy the unencrypted wallet storage aside at ")
                         + storage);

    // Put the plaintext store back exactly as it was. Used on every failure path below: a
    // migration that half-worked must never be the thing that takes the wallet away, and a
    // plaintext store has no recovery phrase to fall back on.
    auto rollback = [&]() {
        if (!QFile::exists(backup)) return;
        QFile::remove(storage);
        QFile::copy(backup, storage);
    };

    // The password goes to the CLI's stdin explicitly - m_password is not touched (invariant A:
    // this verb used to assign it directly, which made one ungated call enough to seize a legacy
    // wallet AND hand the caller a full session on it).
    // The save is the migration: every write path in the CLI calls store_persistent_data(),
    // which re-serialises the whole store and seals it once the password is non-empty. Creating
    // a public account is the same trigger createEncryptedWallet uses; it costs one fresh empty
    // account, which is exactly why this is not folded back into that verb silently.
    const QString result = runWalletCommandInput(
        { QStringLiteral("account"), QStringLiteral("new"), QStringLiteral("public") },
        password + QStringLiteral("\n"), 60000);
    const QJsonObject o = QJsonDocument::fromJson(result.toUtf8()).object();
    if (o.contains(QStringLiteral("error"))) {
        rollback();
        return result;
    }
    if (storageIsPlaintext()) {
        // The CLI ran but the store is still plaintext. Never report success here: the user would
        // be told they are protected while nothing protects them.
        rollback();
        return errorJson(QStringLiteral("the wallet CLI did not encrypt the store - it is still "
                                        "unencrypted at ") + storage,
                         QStringLiteral("not-encrypted"));
    }

    // MIGRATION MUST NOT LOCK THE OWNER OUT. A plaintext store holds no recovery phrase, so if
    // the sealed store does not open with the password that sealed it there is nothing to restore
    // from except the copy taken above - so check, and put the copy back if it does not.
    const QString probe = establishSession(password);
    if (QJsonDocument::fromJson(probe.toUtf8()).object().contains(QStringLiteral("error"))) {
        rollback();
        return errorJson(QStringLiteral("the wallet CLI sealed a store that will not open with "
                                        "the password you chose - your unencrypted wallet has "
                                        "been put back, unchanged"),
                         QStringLiteral("not-encrypted"));
    }
    // Verified, and immediately given up: sealing a store is not proof of who asked for it
    // (invariant A). The owner unlocks with the password they just chose.
    clearSessionPassword();
    appendLog(QStringLiteral("wallet encrypted - unencrypted copy kept at ") + backup);

    QJsonObject out;
    out[QStringLiteral("ok")]       = true;
    out[QStringLiteral("migrated")] = true;
    out[QStringLiteral("backup")]   = backup;
    out[QStringLiteral("locked")]   = true;
    enrichFromOutput(result, out);
    // A plaintext store never carried a mnemonic, so the encrypted one cannot have one either.
    // Say so here rather than letting the user find out when a backup prompt comes up empty.
    out[QStringLiteral("mnemonic")] = QString();
    out[QStringLiteral("note")]     = QStringLiteral(
        "this wallet predates encrypted storage and has no recovery phrase - back it up by "
        "exporting each account's private key. Unlock with the password you just chose.");
    return QJsonDocument(out).toJson(QJsonDocument::Compact);
}

QString WalletPlugin::walletHome()
{
    // rc5 renamed the wallet-home env var NSSA_→LEE_; honor the rc5 name first (matching the
    // wallet-wrapper) and keep the rc4 one as a fallback so old setups don't silently split.
    QString home = qEnvironmentVariable("LEE_WALLET_HOME_DIR");
    if (home.isEmpty())
        home = qEnvironmentVariable("NSSA_WALLET_HOME_DIR");
    if (home.isEmpty())
        home = QDir::homePath() + QStringLiteral("/.local/share/medusa-wallet-home");
    return home;
}

QString WalletPlugin::storagePath()
{
    return walletHome() + QStringLiteral("/storage.json");
}

bool WalletPlugin::storageExists()
{
    return QFile::exists(storagePath());
}

bool WalletPlugin::storageIsPlaintext()
{
    // The encrypted envelope leads with {"v":…,"kdf":…,"ct":…}; plaintext storage leads with
    // {"accounts":…}. The header is enough to tell. A store that exists but cannot be read, or
    // is empty, counts as NOT encrypted: this decides whether a password can protect anything,
    // and "unreadable" is not a reason to assume it can.
    QFile f(storagePath());
    if (!f.exists())
        return false;                    // no store at all - nothing to mis-protect
    if (!f.open(QIODevice::ReadOnly))
        return true;
    const QByteArray head = f.read(256);
    return !(head.contains("\"kdf\"") || head.contains("\"ct\""));
}

QString WalletPlugin::getWalletState() const
{
    QJsonObject o;
    const bool exists = storageExists();
    o[QStringLiteral("exists")]    = exists;
    o[QStringLiteral("encrypted")] = exists && !storageIsPlaintext();
    o[QStringLiteral("unlocked")]  = !m_password.isEmpty();
    // Every store this module has ever moved aside (reset, restore) or copied aside (migration)
    // is still on disk, and until now NOTHING reported that. "My wallet vanished and the reset
    // link buried it deeper" was a real outcome of that silence, so the paths are listed here:
    // displacing a store is reversible only if the user can find out that it happened.
    QDir d(walletHome());
    const QStringList aside = d.entryList({ QStringLiteral("storage.json.bak-*"),
                                            QStringLiteral("storage.json.plain-*") },
                                          QDir::Files, QDir::Name);
    if (!aside.isEmpty()) {
        QJsonArray arr;
        for (const QString& f : aside) arr.append(d.filePath(f));
        o[QStringLiteral("displacedStores")] = arr;
    }
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

// Move the current storage.json aside rather than letting it be deleted or overwritten. Both
// destructive verbs have to stay usable while LOCKED (that is precisely the state the user who
// forgot their password is in), so the only honest way to keep recovery open without leaving a
// one-call wipe primitive is to make the destruction reversible. Returns the backup path, or ""
// when there was nothing to move; *failed is set only when the rename itself failed.
static QString backupWalletStorage(const QString& storage, bool* failed)
{
    if (failed) *failed = false;
    if (!QFile::exists(storage))
        return QString();
    const QString backup = uniqueAsidePath(storage + QStringLiteral(".bak-")
        + QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-hhmmss")));
    // NOTHING IS EVER REMOVED HERE. A colliding same-second name gets a counter instead: this
    // function is the entire reason resetWallet and restoreWallet can stay usable while locked,
    // and that argument is only true while displacement is reversible.
    if (backup.isEmpty() || !QFile::rename(storage, backup)) {
        if (failed) *failed = true;
        return QString();
    }
    return backup;
}

QString WalletPlugin::resetWallet(const QString& password)
{
    // ── The rule, and why it is this rule ────────────────────────────────────────────────────
    // Round 2 wrote `if (sessionIsProvable() && !authorize(password))`, and a hostile caller
    // deleted the condition in one ungated call: clearSessionPassword() empties m_password, so
    // sessionIsProvable() goes false and the gate is skipped. Keying a gate on a fact the caller
    // controls is not a gate. So:
    //
    //  1. If a password is PRESENTED, it must be one the STORE accepts - checked by opening the
    //     store on disk (establishSession), not by comparing against the in-memory session. That
    //     is what makes clearSessionPassword() irrelevant here, and it shares unlock()'s backoff
    //     so it is not a new password oracle.
    //  2. If a SESSION IS LIVE and nothing is presented, refuse. The user has the password (the
    //     UI is holding it); a caller that does not is not the user.
    //  3. If the wallet is LOCKED and nothing is presented, PROCEED. This is the forgot-password
    //     path and it is the entire reason the verb exists.
    //
    // Rule 3 is a deliberate, bounded residual, not an oversight. What it grants a hostile caller
    // is displacing storage.json - and that is not a capability this module holds exclusively: a
    // co-resident module runs at the same uid and can rename or overwrite that file itself, with
    // no plugin, no verb and no gate. Gating it would therefore protect nothing and would cost
    // the forgotten-password user the only door they have. What DOES matter is that displacing a
    // store must not be composable into anything worse, and it no longer is: nothing is deleted
    // (always renamed aside, and getWalletState now lists what was displaced), and creating a
    // wallet in the freed slot grants no session (invariant A), which is what turned this verb
    // into step 2 of a session mint in round 2.
    //
    // Round 4 amendment to rule 1: the decision of WHETHER to enforce is taken on the session,
    // which lives in memory, before any question is asked about the file. It used to read
    // `storeCanProvePassword() && …`, i.e. it asked storage.json whether to run the check at all,
    // and the same coerced-plaintext reading that defeated authorize() (a re-wrapped store whose
    // markers fall past byte 256) also turned this check off. What that bought an attacker was
    // only displacement - which rule 3 hands to any caller anyway, and which rename(2) hands to
    // them without this module - but a check that a file can switch off is not a check, and this
    // one is now switched by the secret.
    if (!m_password.isEmpty()) {
        // A live session: the presented password must equal it. No file can turn this off.
        if (!authorize(password))
            return authRefusal();
    } else if (!password.isEmpty() && storeCanProvePassword()) {
        // No session, but a credential was presented: it must open the STORE (shared backoff, so
        // this is not a second unthrottled oracle). On a store no password can be proved against
        // there is nothing to check it with, and refusing would stand between the plaintext /
        // forgotten-password user and their only escape - which rule 3 already leaves open to
        // exactly the same caller, so nothing is gained by an attacker who forces this branch.
        const QString probe = establishSession(password);
        const QJsonObject po = QJsonDocument::fromJson(probe.toUtf8()).object();
        if (po.contains(QStringLiteral("error")))
            return probe;
    }

    const QString storage = walletHome() + QStringLiteral("/storage.json");
    bool failed = false;
    const QString backup = backupWalletStorage(storage, &failed);
    if (failed)
        return errorJson(QStringLiteral("could not move wallet storage aside at ") + storage);

    clearSessionPassword();   // also drops the log ring and stops the idle timer
    appendLog(backup.isEmpty() ? QStringLiteral("wallet reset - no storage to move")
                               : QStringLiteral("wallet reset - storage moved to ") + backup);
    QJsonObject o;
    o[QStringLiteral("ok")] = true;
    if (!backup.isEmpty())
        o[QStringLiteral("backup")] = backup;
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

// ── Import / export ─────────────────────────────────────────────────────────────

QString WalletPlugin::restoreWallet(const QString& phrase, const QString& password, int depth,
                                    const QString& sessionPassword)
{
    // Same shape as resetWallet, and the same reason. The CLI intercepts restore-keys BEFORE the
    // store is loaded - it never decrypts the old one - so ungated this overwrote a locked
    // victim's storage.json with no credential whatsoever. On the locked path the recovery
    // phrase IS the credential (only the owner has it) and that path must stay open, because a
    // user restoring after forgetting their password has no session password by definition. A
    // live session still costs the password, and either way the outgoing store is moved aside
    // first, so a restore nobody asked for is undoable.
    //
    // What the locked path must NOT do is hand the caller a session: `password` is whatever the
    // caller chose, so sealing the new store with it and then keeping it in m_password made this
    // ungated verb a way to install a session password of one's own choosing - the same mint that
    // removing setSessionPassword closed - which then satisfied every gated verb. Only a caller
    // that PROVED the live session gets the session carried over to the new password. A genuine
    // recovery still works exactly as before: the store is rebuilt from the phrase and the user
    // unlocks with the password they just chose.
    //
    // The proof is the same one resetWallet uses and for the same reason: presented credentials
    // are checked against the STORE, so emptying the in-memory session first no longer turns the
    // checked path into the unchecked one.
    //
    // Round 4 amendment, the same one resetWallet carries: whether a proof is REQUIRED is decided
    // on the in-memory session, not on a predicate about storage.json that a co-resident module
    // can flip.
    bool proved = false;
    if (!m_password.isEmpty()) {
        if (!authorize(sessionPassword))
            return authRefusal();
        proved = true;
    } else if (!sessionPassword.isEmpty() && storeCanProvePassword()) {
        const QString probe = establishSession(sessionPassword);
        if (QJsonDocument::fromJson(probe.toUtf8()).object().contains(QStringLiteral("error")))
            return probe;
        proved = true;
    }
    if (phrase.trimmed().isEmpty())
        return errorJson(QStringLiteral("recovery phrase is required"));
    if (depth <= 0)
        depth = 5;

    bool failed = false;
    const QString backup = backupWalletStorage(walletHome() + QStringLiteral("/storage.json"),
                                               &failed);
    if (failed)
        return errorJson(QStringLiteral("could not move the existing wallet storage aside"));

    appendLog(QStringLiteral("restore wallet (depth %1)").arg(depth));
    const QStringList args{ QStringLiteral("restore-keys"),
                            QStringLiteral("--depth"), QString::number(depth) };
    // restore-keys reads the mnemonic line, then the password line, from stdin.
    const QString stdinData = phrase.trimmed() + QStringLiteral("\n") + password + QStringLiteral("\n");
    const QString result = runWalletCommandInput(args, stdinData, 300000);  // derives + syncs

    const QJsonDocument doc = QJsonDocument::fromJson(result.toUtf8());
    const QJsonObject o = doc.object();
    if (o.contains(QStringLiteral("error"))) {
        // A failed restore (a mistyped phrase is the usual one) must not leave the user with no
        // wallet at all: if the CLI wrote nothing, put the store we moved aside back.
        const QString storage = walletHome() + QStringLiteral("/storage.json");
        if (!backup.isEmpty() && !QFile::exists(storage))
            QFile::rename(backup, storage);
        return result;
    }

    // Whatever was held before certainly does not open the store that was just written, so it is
    // dropped either way and must never be mistaken for a session on the new store.
    clearSessionPassword();
    if (!doc.isObject())
        return result;               // an array/scalar result: nothing to annotate
    QJsonObject annotated = o;
    if (proved) {
        // The caller proved the OLD session, so carrying one over is legitimate - but it is
        // carried the only way this module ever establishes a session: by opening the NEW store
        // with the new password. If that fails the wallet simply stays locked; the store is
        // there, and the user unlocks with the password they chose.
        const QString probe = establishSession(password);
        annotated[QStringLiteral("locked")] =
            QJsonDocument::fromJson(probe.toUtf8()).object().contains(QStringLiteral("error"));
    } else {
        annotated[QStringLiteral("locked")] = true;
    }
    if (!backup.isEmpty())
        annotated[QStringLiteral("backup")] = backup;   // where the replaced store went
    return QJsonDocument(annotated).toJson(QJsonDocument::Compact);
}

QString WalletPlugin::exportMnemonic(const QString& password)
{
    // The 24 words are every account this wallet will ever hold, on every zone, forever - and
    // unlike a balance they are not testnet-scoped. Nothing else in the module is worth as much.
    if (!authorize(password))
        return authRefusal();

    const QString result = runWalletCommand(
        { QStringLiteral("account"), QStringLiteral("export-mnemonic") });
    const QJsonObject o = QJsonDocument::fromJson(result.toUtf8()).object();
    if (o.contains(QStringLiteral("error")))
        return result;

    QJsonObject out;
    out[QStringLiteral("ok")]       = true;
    out[QStringLiteral("mnemonic")] = o.value(QStringLiteral("output")).toString().trimmed();
    return QJsonDocument(out).toJson(QJsonDocument::Compact);
}

QString WalletPlugin::exportKey(const QString& accountId, const QString& password)
{
    if (!authorize(password))
        return authRefusal();
    if (accountId.trimmed().isEmpty())
        return errorJson(QStringLiteral("accountId is required"));

    const QString result = runWalletCommand({
        QStringLiteral("account"), QStringLiteral("export-key"),
        // the patch declares account_id as #[arg(long)] with no short form; "-a" is rejected
        QStringLiteral("--account-id"), accountId.trimmed()
    });
    const QJsonObject o = QJsonDocument::fromJson(result.toUtf8()).object();
    if (o.contains(QStringLiteral("error")))
        return result;

    QJsonObject out;
    out[QStringLiteral("ok")]         = true;
    out[QStringLiteral("accountId")]  = accountId.trimmed();
    out[QStringLiteral("privateKey")] = o.value(QStringLiteral("output")).toString().trimmed();
    return QJsonDocument(out).toJson(QJsonDocument::Compact);
}

QString WalletPlugin::importKey(const QString& privateKey, const QString& label,
                                const QString& password)
{
    // Planting a key is as much a capability as revealing one: an ungated import let any
    // co-resident module add an account it holds the signing key for, labelled however it
    // liked, and the user's own account list then presented it as theirs.
    if (!authorize(password))
        return authRefusal();

    if (privateKey.trimmed().isEmpty())
        return errorJson(QStringLiteral("private key is required"));

    // Upstream has no `import-key`: it is `account import public`, which takes no --label, so a
    // label is applied afterwards with `account label`.
    //
    // The key goes on STDIN, not argv. `--private-key <hex>` put a raw signing key on a
    // world-readable /proc/<pid>/cmdline (two of them, since the wrapper re-execs argv verbatim)
    // for the length of the import. Stdin line 1 is the password, which the CLI reads before
    // dispatching any subcommand, so the key is line 2 - the same two-line convention
    // restore-keys already uses for the recovery phrase.
    //
    // DEPENDENCY: this needs the CLI to accept the stdin form, which is
    // wallet/patches-v020/0003-fix-wallet-take-the-imported-signing-key-on-stdin-no.patch plus
    // the matching module/scripts/wallet-wrapper change. Against an engine built without that
    // patch, clap rejects the missing --private-key and the import fails closed (no silent
    // fallback to argv: that would put the key back on the command line).
    const QString result = runWalletCommandInput(
        { QStringLiteral("account"), QStringLiteral("import"), QStringLiteral("public") },
        m_password + QStringLiteral("\n") + privateKey.trimmed() + QStringLiteral("\n"));
    const QJsonObject o = QJsonDocument::fromJson(result.toUtf8()).object();
    if (o.contains(QStringLiteral("error")))
        return result;

    QJsonObject out;
    out[QStringLiteral("ok")] = true;
    enrichFromOutput(result, out);   // parses the imported "account_id Public/<id>"

    const QString id = out.value(QStringLiteral("accountId")).toString();
    if (!label.trimmed().isEmpty() && !id.isEmpty())
        runWalletCommand({ QStringLiteral("account"), QStringLiteral("label"),
                           QStringLiteral("--account-id"), id,
                           QStringLiteral("--label"), label.trimmed() });
    return QJsonDocument(out).toJson(QJsonDocument::Compact);
}
