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

#include <algorithm>
#include <dlfcn.h>   // dladdr: locate the module's own install dir (bundled binaries)

static constexpr const char* kCliPathKey = "medusa-wallet/cliPath";
static constexpr const char* kNetworkKey = "medusa-wallet/network";   // active zone id
static constexpr const char* kZonesKey   = "medusa-wallet/zones";     // user-added remote zones (JSON)

// Operator endpoints for the built-in "Paradox Computer" zones are NOT baked into the public
// build - they are supplied at runtime so no production address ever ships in source:
//   • "Paradox Computer · Tor"      sequencer .onion: env MEDUSA_SEQ_ONION     | ~/.config/medusa-sequencer.onion
//   • "Paradox Computer · clearnet" sequencer URL:    env MEDUSA_CLEARNET_URL  | ~/.config/medusa-clearnet.url
// When unset, those zones are simply unavailable; the self-contained local "devnet" zone is
// the default and needs no external infrastructure.
static QString endpointFromConfig(const char* envVar, const QString& cfgFile)
{
    const QString env = qEnvironmentVariable(envVar).trimmed();
    if (!env.isEmpty()) return env;
    QFile f(QDir::homePath() + QStringLiteral("/.config/") + cfgFile);
    if (f.open(QIODevice::ReadOnly)) return QString::fromUtf8(f.readAll()).trimmed();
    return QString();
}
// Bundled-Tor SOCKS port (distinct from a system Tor on 9050, so the two never clash).
static constexpr int kTorSocksPort = 9250;
// Bundled-Tor control port (for the onion-connection-stage monitor).
static constexpr int kTorControlPort = 9251;
static constexpr const char* kSeqModeKey = "medusa-wallet/seqMode";   // "local" | "hosted"
static constexpr const char* kSeqUrlKey  = "medusa-wallet/seqUrl";    // hosted sequencer URL
static constexpr const char* kSeqPortKey = "medusa-wallet/seqPort";   // local port (default 3071)
static constexpr const char* kSeqPathKey = "medusa-wallet/seqPath";   // sequencer binary

// The bundled standalone-sequencer config - the rc5-shaped genesis with a templated home
// and a dead bedrock node_url (standalone mocks bedrock, so no L1 is ever contacted).
// __SEQ_HOME__ is replaced with the per-wallet sequencer home at first launch; the rc5
// sequencer builds its base state from the hardcoded testnet_initial_state (pinata + debug
// accounts) and applies this `genesis` array on top: fund the system bridge account, plus a
// couple of supply accounts. (The diaphani L1 zone rewrites node_url in writeSeqConfig.)
static const char* kSeqConfigTemplate =
R"SEQ({"home":"__SEQ_HOME__","max_num_tx_in_block":20,"max_block_size":"1 MiB","mempool_max_size":1000,"block_create_timeout":"3s","retry_pending_blocks_timeout":"5s","bedrock_config":{"channel_id":"0202020202020202020202020202020202020202020202020202020202020202","node_url":"http://127.0.0.1:1"},"signing_key":[37,37,37,37,37,37,37,37,37,37,37,37,37,37,37,37,37,37,37,37,37,37,37,37,37,37,37,37,37,37,37,37],"genesis":[{"supply_bridge_account":{"balance":1000000000}},{"supply_account":{"account_id":"CbgR6tj5kWx5oziiFptM7jMvrQeYY3Mzaao6ciuhSr2r","balance":100000000}},{"supply_account":{"account_id":"2RHZhw9h534Zr3eq2RGhQete2Hh667foECzXPmSkGni2","balance":100000000}}]})SEQ";

// ── Helpers ───────────────────────────────────────────────────────────────────

QString WalletPlugin::errorJson(const QString& msg)
{
    QJsonObject o;
    o[QStringLiteral("error")] = msg;
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

QString WalletPlugin::okJson()
{
    QJsonObject o;
    o[QStringLiteral("ok")] = true;
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

// "token" → the token program; anything else → the native authenticated-transfer
// program (the asset the wallet's balance/faucet/Send already operate on).
QString WalletPlugin::assetProgram(const QString& asset)
{
    return (asset.trimmed().toLower() == QStringLiteral("token"))
         ? QStringLiteral("token")
         : QStringLiteral("auth-transfer");
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

// Resolve a runtime binary: prefer the copy bundled INSIDE the module (self-contained
// install), then the dev-staged ~/.local/bin/<name>, else the bare name (PATH lookup).
QString resolveBin(const QString& name)
{
    const QString bdir = moduleBinDir();
    if (!bdir.isEmpty()) {
        const QString bundled = bdir + QStringLiteral("/") + name;
        if (QFile::exists(bundled)) return bundled;
    }
    const QString local = QDir::homePath() + QStringLiteral("/.local/bin/") + name;
    if (QFile::exists(local)) return local;
    return name;
}

// Resolve a usable Tor binary the way ensureTor() does: the bundled medusa-tor first,
// then a system tor. Returns "" when neither is present (Tor/onion zones can't connect).
QString resolveTorBin()
{
    const QString bundled = resolveBin(QStringLiteral("medusa-tor"));
    if (QFileInfo::exists(bundled)) return bundled;
    for (const QString& c : { QStringLiteral("/usr/bin/tor"), QStringLiteral("/usr/sbin/tor"),
                              QStringLiteral("/usr/local/bin/tor") })
        if (QFileInfo::exists(c)) return c;
    return QString();
}
}  // namespace

QString WalletPlugin::cliPath() const
{
    QSettings s;
    QString stored = s.value(QLatin1String(kCliPathKey)).toString().trimmed();
    if (!stored.isEmpty())
        return stored;

    // Bundled (<module>/bin/wallet) -> ~/.local/bin/wallet -> PATH.
    return resolveBin(QStringLiteral("wallet"));
}

// ── QProcess runner ──────────────────────────────────────────────────────────

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
    return s.trimmed();
}

QString WalletPlugin::runWalletCommandInput(const QStringList& args,
                                            const QString& stdinData, int timeoutMs)
{
    QString bin = cliPath();
    appendLog(QStringLiteral("run: wallet ") + args.join(QLatin1Char(' ')));

    QProcess proc;
    // Keep channels separate: the CLI prompts on stderr and returns results on
    // stdout, so we must not let the "Input password: " prompt pollute stdout.
    proc.setProcessChannelMode(QProcess::SeparateChannels);
    proc.start(bin, args);

    if (!proc.waitForStarted(3000)) {
        appendLog(QStringLiteral("failed to start: ") + proc.errorString(), QStringLiteral("error"));
        return errorJson(QStringLiteral("wallet CLI not found: ") + bin
                         + QStringLiteral(" - configure path in ⚙ settings"));
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

    if (exitCode != 0)
        appendLog(QStringLiteral("exit %1: ").arg(exitCode) + (err.isEmpty() ? out : err).left(120),
                  QStringLiteral("error"));
    else
        appendLog(QStringLiteral("ok: ") + out.left(80));

    // On failure the message is on stderr; on success the result is on stdout.
    const QString effective = (exitCode != 0 && out.isEmpty()) ? err : out;
    return normalizeCliOutput(effective, exitCode);
}

// Turn raw merged CLI output + exit code into the module's JSON contract.
// The wallet wrapper script (~/.local/bin/wallet) already emits JSON for some
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
    QString bin = cliPath();
    bool found = QFile::exists(bin) || (bin == QStringLiteral("wallet")); // PATH lookup: assume present if name only

    // Attempt a real existence check for PATH-style name
    if (bin == QStringLiteral("wallet")) {
        QProcess check;
        check.start(QStringLiteral("which"), {QStringLiteral("wallet")});
        check.waitForFinished(2000);
        found = (check.exitCode() == 0);
    }

    QJsonObject o;
    o[QStringLiteral("cliFound")] = found;
    o[QStringLiteral("cliPath")]  = bin;
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

QString WalletPlugin::getConfig() const
{
    QSettings s;
    QString stored = s.value(QLatin1String(kCliPathKey)).toString();
    QJsonObject o;
    o[QStringLiteral("cliPath")]    = stored;
    o[QStringLiteral("cliPathEff")] = cliPath();
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

QString WalletPlugin::setCliPath(const QString& path)
{
    QString p = path.trimmed();
    if (p.isEmpty())
        return errorJson(QStringLiteral("path is empty"));

    QSettings s;
    s.setValue(QLatin1String(kCliPathKey), p);
    s.sync();
    appendLog(QStringLiteral("cliPath saved: ") + p);
    return okJson();
}

// ── Sequencer (local auto-launch / hosted) ──────────────────────────────────────

QString WalletPlugin::seqPath() const
{
    QSettings s;
    const QString stored = s.value(QLatin1String(kSeqPathKey)).toString().trimmed();
    if (!stored.isEmpty())
        return stored;
    // diaphani/Tor mode talks to a REAL shared Bedrock L1, so it needs the non-standalone
    // build (sequencer_service_l1); devnet/testnet use the L1-free standalone build.
    const QString binName = (netId() == QStringLiteral("diaphani"))
        ? QStringLiteral("sequencer_service_l1") : QStringLiteral("sequencer_service");
    return resolveBin(binName);   // bundled <module>/bin -> ~/.local/bin -> PATH
}

// ── Zones ───────────────────────────────────────────────────────────────────────
// A "zone" is a LEZ chain. Built-ins: "devnet" (local standalone sandbox) and
// "diaphani" (local sequencer on the shared Bedrock L1 over Tor). User zones are
// REMOTE - the wallet is a thin client of someone else's sequencer (clearnet URL or a
// Tor .onion). Accounts/keys are shared across zones; balances/tokens are per-zone.

QString WalletPlugin::netId() const   // the active zone id
{
    QSettings s;
    // Default: the hosted "Paradox Computer · clearnet" zone whenever its operator endpoint
    // is configured (env / ~/.config - see top of file). Only a pristine public build with
    // no endpoint falls back to the self-contained local "devnet" sandbox, which needs no
    // external infrastructure.
    const QString def = endpointFromConfig("MEDUSA_CLEARNET_URL",
                                           QStringLiteral("medusa-clearnet.url")).isEmpty()
                      ? QStringLiteral("devnet")
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
        o[QStringLiteral("name")]  = QStringLiteral("Paradox Computer · clearnet");
        o[QStringLiteral("url")]   = endpointFromConfig("MEDUSA_CLEARNET_URL", QStringLiteral("medusa-clearnet.url"));
        o[QStringLiteral("onion")] = QString();
        o[QStringLiteral("tor")]   = false;
        return o;
    }
    const QJsonArray arr = userZones();
    for (const auto& v : arr) {
        const QJsonObject o = v.toObject();
        if (o.value(QStringLiteral("id")).toString() == id)
            return o;
    }
    return {};
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

QString WalletPlugin::seqHome() const
{
    // Per-zone home (only local zones keep chain state here).
    return walletHome() + QStringLiteral("/sequencer-") + netId();
}

bool WalletPlugin::seqHealthyUrl(const QString& url, int timeoutMs)
{
    // Probe the sequencer's JSON-RPC checkHealth at an arbitrary URL via curl (keeps the
    // dependency surface at zero - the rest of this file shells out the same way).
    if (url.trimmed().isEmpty()) return false;
    QProcess p;
    p.start(QStringLiteral("curl"), {
        // curl's own --max-time must track timeoutMs - a hardcoded "1" ignored the argument and
        // timed out on any real over-the-internet HTTPS round-trip (~2s incl. the TLS handshake).
        QStringLiteral("-s"), QStringLiteral("--max-time"), QString::number(qMax(1, timeoutMs / 1000)),
        QStringLiteral("-X"), QStringLiteral("POST"),
        QStringLiteral("-H"), QStringLiteral("content-type: application/json"),
        QStringLiteral("-d"),
        QStringLiteral(R"({"jsonrpc":"2.0","id":1,"method":"checkHealth","params":[]})"),
        url
    });
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
    // Reuse any Tor already on our SOCKS port (e.g. one orphaned by a previous hard-kill
    // that still holds the data-dir lock) instead of launching a duplicate that would fail.
    {
        QProcess probe;
        probe.start(QStringLiteral("bash"),
                    { QStringLiteral("-c"),
                      QStringLiteral("exec 3<>/dev/tcp/127.0.0.1/%1").arg(kTorSocksPort) });
        if (probe.waitForFinished(700) && probe.exitCode() == 0) {
            appendLog(QStringLiteral("reusing Tor already on 127.0.0.1:%1").arg(kTorSocksPort));
            return;
        }
    }
    // Prefer the bundled binary; fall back to a system tor if present.
    QString torBin = resolveBin(QStringLiteral("medusa-tor"));
    if (!QFileInfo::exists(torBin)) {
        for (const QString& c : { QStringLiteral("/usr/bin/tor"), QStringLiteral("/usr/sbin/tor"),
                                  QStringLiteral("/usr/local/bin/tor") })
            if (QFileInfo::exists(c)) { torBin = c; break; }
    }
    if (!QFileInfo::exists(torBin)) {
        appendLog(QStringLiteral("bundled Tor not found (~/.local/bin/medusa-tor)"), QStringLiteral("error"));
        return;
    }
    const QString dataDir = walletHome() + QStringLiteral("/tor");
    QDir().mkpath(dataDir);
    QFile::remove(dataDir + QStringLiteral("/onion-stage.json"));   // clear stale onion stage
    m_torProc = new QProcess(this);
    m_torProc->setProcessChannelMode(QProcess::SeparateChannels);
    appendLog(QStringLiteral("launching bundled Tor (SOCKS 127.0.0.1:%1)").arg(kTorSocksPort));
    m_torProc->start(torBin, {
        QStringLiteral("--SocksPort"),       QStringLiteral("127.0.0.1:%1").arg(kTorSocksPort),
        QStringLiteral("--ControlPort"),     QStringLiteral("127.0.0.1:%1").arg(kTorControlPort),
        QStringLiteral("--CookieAuthentication"), QStringLiteral("1"),
        QStringLiteral("--DataDirectory"),   dataDir,
        QStringLiteral("--ClientOnly"),      QStringLiteral("1"),
        QStringLiteral("--Log"),             QStringLiteral("notice file ") + dataDir + QStringLiteral("/tor.log"),
    });
    QObject::connect(m_torProc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                     this, [this](int code, QProcess::ExitStatus) {
        appendLog(QStringLiteral("bundled Tor exited (code %1)").arg(code), QStringLiteral("error"));
    });
    if (!m_torProc->waitForStarted(3000)) {
        appendLog(QStringLiteral("Tor failed to start: ") + torBin, QStringLiteral("error"));
        m_torProc->deleteLater();
        m_torProc = nullptr;
        return;
    }
    // Launch the control-port monitor → real onion-connection stages for the progress bar.
    const QString mon = resolveBin(QStringLiteral("medusa-tor-monitor"));
    if (QFileInfo::exists(mon)) {
        if (m_torMonProc) { m_torMonProc->kill(); m_torMonProc->deleteLater(); }
        m_torMonProc = new QProcess(this);
        m_torMonProc->setProcessChannelMode(QProcess::SeparateChannels);
        m_torMonProc->start(QStringLiteral("python3"),
                            { mon, dataDir, QString::number(kTorControlPort) });
    }
}

void WalletPlugin::stopTor()
{
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
    {
        QProcess probe;
        probe.start(QStringLiteral("bash"),
                    { QStringLiteral("-c"),
                      QStringLiteral("exec 3<>/dev/tcp/127.0.0.1/%1").arg(listenPort) });
        if (probe.waitForFinished(700) && probe.exitCode() == 0) {
            appendLog(QStringLiteral("reusing forward already on 127.0.0.1:%1").arg(listenPort));
            return;
        }
    }
    const QString fwd = resolveBin(QStringLiteral("diaphani-forward"));
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
    m_fwdProc->start(fwd, fargs);
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

    m_seqProc = new QProcess(this);
    m_seqProc->setProcessChannelMode(QProcess::SeparateChannels);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    // Dev-mode (fake/fast proofs, no verification) ONLY for the local "devnet" sandbox; every real
    // zone (diaphani + user-added remote) must verify real proofs. Matches the wallet's prove mode.
    env.insert(QStringLiteral("RISC0_DEV_MODE"),
               netId() == QStringLiteral("devnet") ? QStringLiteral("1") : QStringLiteral("0"));
    m_seqProc->setProcessEnvironment(env);
    QObject::connect(m_seqProc, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        appendLog(QStringLiteral("sequencer process error: ")
                  + (m_seqProc ? m_seqProc->errorString() : QString()), QStringLiteral("error"));
    });

    const QString bin = seqPath();
    appendLog(QStringLiteral("spawning sequencer: %1 --port %2").arg(bin).arg(port));
    m_seqProc->start(bin, { QStringLiteral("--port"), QString::number(port), cfg });
    if (!m_seqProc->waitForStarted(3000)) {
        appendLog(QStringLiteral("sequencer failed to start (binary missing?): ") + bin,
                  QStringLiteral("error"));
        m_seqProc->deleteLater();
        m_seqProc = nullptr;
    }
}

void WalletPlugin::stopSequencer()
{
    if (m_seqProc) {
        if (m_seqProc->state() != QProcess::NotRunning) {
            m_seqProc->terminate();                       // graceful (== Ctrl-C)
            if (!m_seqProc->waitForFinished(3000))
                m_seqProc->kill();
            m_seqProc->waitForFinished(2000);
        }
        m_seqProc->deleteLater();
        m_seqProc = nullptr;
    }
    stopForward();   // tear down the Tor tunnel too (no-op if not running)
}

// Recompute the wallet's sequencer_addr from the active zone, write it into
// wallet_config.json, and (re)launch/stop the local sequencer. Returns the effective addr.
QString WalletPlugin::applySequencer()
{
    const QString id   = netId();
    const QString kind = zoneKind(id);

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
        return o;
    };
    out.append(builtin(QStringLiteral("devnet"),   QStringLiteral("Devnet"),                  QStringLiteral("local-standalone")));
    out.append(builtin(QStringLiteral("diaphani"), QStringLiteral("Paradox Computer · Tor"),  QStringLiteral("local-l1-tor")));
    // Same prod sequencer as the Tor zone, reached over clearnet (TLS) - a thin remote client.
    {
        QJsonObject o = builtin(QStringLiteral("paradox-clearnet"),
                                QStringLiteral("Paradox Computer · clearnet"),
                                QStringLiteral("remote"));
        o[QStringLiteral("endpoint")] = endpointFromConfig("MEDUSA_CLEARNET_URL", QStringLiteral("medusa-clearnet.url"));
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
        out.append(o);
    }
    QJsonObject res;
    res[QStringLiteral("zones")]  = out;
    res[QStringLiteral("active")] = netId();
    return QJsonDocument(res).toJson(QJsonDocument::Compact);
}

QString WalletPlugin::addZone(const QString& name, const QString& url,
                              const QString& onion, bool tor)
{
    const QString nm = name.trimmed();
    if (nm.isEmpty()) return errorJson(QStringLiteral("name is required"));
    QString cleanUrl;
    if (tor) {
        if (onion.trimmed().isEmpty() || !onion.contains(QStringLiteral(".onion")))
            return errorJson(QStringLiteral("a Tor zone needs a valid .onion address"));
    } else {
        // Normalize + validate the clearnet URL so a dead zone can't be created silently.
        QString u = url.trimmed();
        if (u.isEmpty()) return errorJson(QStringLiteral("a clearnet zone needs a sequencer URL"));
        if (!u.contains(QStringLiteral("://"))) u = QStringLiteral("http://") + u;
        const QUrl qu(u, QUrl::StrictMode);
        const QString sch = qu.scheme().toLower();
        if (!qu.isValid() || qu.host().isEmpty()
            || (sch != QStringLiteral("http") && sch != QStringLiteral("https")))
            return errorJson(QStringLiteral("enter a full sequencer URL, e.g. https://host:3072/"));
        if (qu.host().endsWith(QStringLiteral(".onion")))
            return errorJson(QStringLiteral("a .onion address requires the Tor transport"));
        cleanUrl = u;
    }
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
    z[QStringLiteral("url")] = cleanUrl; z[QStringLiteral("onion")] = onion.trimmed();
    z[QStringLiteral("tor")] = tor;
    arr.append(z);
    s.setValue(QLatin1String(kZonesKey), QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact)));
    s.sync();
    QJsonObject o; o[QStringLiteral("ok")] = true; o[QStringLiteral("id")] = id;
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

QString WalletPlugin::editZone(const QString& id, const QString& name, const QString& url,
                               const QString& onion, bool tor)
{
    if (!isUserZone(id))
        return errorJson(QStringLiteral("only user-added zones can be edited"));
    const QString nm = name.trimmed();
    if (nm.isEmpty()) return errorJson(QStringLiteral("name is required"));
    QString cleanUrl;
    if (tor) {
        if (onion.trimmed().isEmpty() || !onion.contains(QStringLiteral(".onion")))
            return errorJson(QStringLiteral("a Tor zone needs a valid .onion address"));
    } else {
        QString u = url.trimmed();
        if (u.isEmpty()) return errorJson(QStringLiteral("a clearnet zone needs a sequencer URL"));
        if (!u.contains(QStringLiteral("://"))) u = QStringLiteral("http://") + u;
        const QUrl qu(u, QUrl::StrictMode);
        const QString sch = qu.scheme().toLower();
        if (!qu.isValid() || qu.host().isEmpty()
            || (sch != QStringLiteral("http") && sch != QStringLiteral("https")))
            return errorJson(QStringLiteral("enter a full sequencer URL, e.g. https://host:3072/"));
        if (qu.host().endsWith(QStringLiteral(".onion")))
            return errorJson(QStringLiteral("a .onion address requires the Tor transport"));
        cleanUrl = u;
    }
    QSettings s;
    QJsonArray arr = userZones(), out;
    for (const auto& v : arr) {
        QJsonObject o = v.toObject();
        if (o.value(QStringLiteral("id")).toString() == id) {
            o[QStringLiteral("name")]  = nm;
            o[QStringLiteral("url")]   = cleanUrl;
            o[QStringLiteral("onion")] = onion.trimmed();
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
    o[QStringLiteral("seqPath")]    = s.value(QLatin1String(kSeqPathKey)).toString();
    o[QStringLiteral("seqPathEff")] = seqPath();
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
    if (m_healthProbe) { m_healthProbe->deleteLater(); m_healthProbe = nullptr; }
    m_healthProbe = new QProcess(this);
    QProcess* p = m_healthProbe;
    QObject::connect(p, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
        [this, p](int, QProcess::ExitStatus) {
            m_lastSeqOk = QString::fromUtf8(p->readAllStandardOutput()).contains(QStringLiteral("\"result\""));
            p->deleteLater();
            if (m_healthProbe == p) m_healthProbe = nullptr;
        });
    p->start(QStringLiteral("curl"), {
        QStringLiteral("-s"), QStringLiteral("--max-time"), QStringLiteral("8"),
        QStringLiteral("-X"), QStringLiteral("POST"),
        QStringLiteral("-H"), QStringLiteral("content-type: application/json"),
        QStringLiteral("-d"),
        QStringLiteral(R"({"jsonrpc":"2.0","id":1,"method":"checkHealth","params":[]})"),
        url });
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

    if (torZone) {
        // Thin client over Tor: a 1s probe would always time out, so probe ASYNC (cached)
        // and report green/gray/red from the cached result + the tunnel state.
        probeSeqHealthAsync(eff);
        const bool fwdUp = m_fwdProc && m_fwdProc->state() != QProcess::NotRunning;
        state = m_lastSeqOk ? QStringLiteral("running")
              : (fwdUp ? QStringLiteral("starting") : QStringLiteral("unreachable"));
    } else if (kind == QStringLiteral("remote")) {
        // Remote clearnet: probe ASYNC + cached. A sync 1s probe always times out on a real
        // over-the-internet HTTPS round-trip (~2s; the TLS handshake alone is ~1.3s), and a
        // longer SYNC probe would freeze the 10s UI poll. Show "starting" (→ "Connecting…")
        // while the first probe is in flight, then green/red from the cached result.
        probeSeqHealthAsync(eff);
        const bool probing = m_healthProbe && m_healthProbe->state() != QProcess::NotRunning;
        state = m_lastSeqOk ? QStringLiteral("running")
              : (probing ? QStringLiteral("starting") : QStringLiteral("unreachable"));
    } else {
        // Local zone (devnet): a process we own + its checkHealth on 127.0.0.1:port.
        const bool procUp = m_seqProc && m_seqProc->state() != QProcess::NotRunning;
        state = seqHealthy(port) ? QStringLiteral("running")
              : (procUp ? QStringLiteral("starting") : QStringLiteral("unreachable"));
    }
    QJsonObject o;
    o[QStringLiteral("state")] = state;
    o[QStringLiteral("mode")]  = kind;
    o[QStringLiteral("port")]  = port;
    // For a local zone (devnet), whether the sequencer binary is actually on disk - if not,
    // it can never spawn, so the UI shows a "you need a local sequencer" disclaimer instead
    // of an endless "Connecting…". (Remote/Tor zones don't run a local sequencer.)
    if (kind == QStringLiteral("local-standalone")) {
        const QFileInfo si(seqPath());
        o[QStringLiteral("binaryAvailable")] = si.exists() && si.isFile();
        o[QStringLiteral("binaryPath")]      = seqPath();
    }
    // Tor/onion zone: whether a usable Tor binary (bundled medusa-tor OR a system tor) exists -
    // else the wallet can't reach the zone, so the UI shows a "no Tor found" disclaimer.
    if (torZone) {
        const QString tb = resolveTorBin();
        o[QStringLiteral("needsTor")]     = true;
        o[QStringLiteral("torAvailable")] = !tb.isEmpty();
        o[QStringLiteral("torPath")]      = tb;
    }
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
    p->start(cliPath(), { QStringLiteral("account"), QStringLiteral("list"), QStringLiteral("-l") });
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

QString WalletPlugin::consolidateToken(const QString& accountId, const QString& definitionId)
{
    // WALLET-UI ONLY on-chain write (sweeps the user's own token ATAs into their vault).
    // NOT part of the Connect op surface (requestAction only dispatches send/shield/deshield/
    // private, each user-approved) - do NOT add it there without an approval sheet. Requiring
    // an unlocked wallet is the precondition gate: a locked wallet performs no writes.
    if (m_password.isEmpty())
        return errorJson(QStringLiteral("wallet is locked - unlock before consolidating"));
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

QString WalletPlugin::getWhitelist()
{
    return runWalletCommand({ QStringLiteral("whitelist") });
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
    p->start(cliPath(), { QStringLiteral("account"), QStringLiteral("sync-private") });
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
                                    const QString& amount)
{
    if (from.trimmed().isEmpty())
        return errorJson(QStringLiteral("from account is required"));
    if (to.trimmed().isEmpty())
        return errorJson(QStringLiteral("to account is required"));
    if (amount.trimmed().isEmpty())
        return errorJson(QStringLiteral("amount is required"));

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
                                     const QString& definitionId, const QString& amount)
{
    if (from.trimmed().isEmpty())         return errorJson(QStringLiteral("from account is required"));
    if (to.trimmed().isEmpty())           return errorJson(QStringLiteral("to account is required"));
    if (definitionId.trimmed().isEmpty()) return errorJson(QStringLiteral("token is required"));
    if (amount.trimmed().isEmpty())       return errorJson(QStringLiteral("amount is required"));
    // The wrapper's token-transfer derives/creates ATAs + token-sends + waits for landing
    // (~30-40s), so run it as a background job like the privacy ops.
    return startPrivacyJob(QStringLiteral("tokensend"), QStringLiteral("token"),
                           { QStringLiteral("token-transfer"), from.trimmed(), to.trimmed(),
                             definitionId.trimmed(), amount.trimmed() },
                           from.trimmed(), to.trimmed(), amount.trimmed());
}

QString WalletPlugin::startSendTransfer(const QString& from, const QString& to,
                                        const QString& amount)
{
    if (from.trimmed().isEmpty())   return errorJson(QStringLiteral("from account is required"));
    if (to.trimmed().isEmpty())     return errorJson(QStringLiteral("to account is required"));
    if (amount.trimmed().isEmpty()) return errorJson(QStringLiteral("amount is required"));
    // Background job: the destination can be a Private account (private→private from the main
    // Send screen), which is a multi-minute real proof. The wrapper auto-syncs + uses the proof
    // budget when --from is Private; a plain public send just submits + lands. Never blocks.
    return startPrivacyJob(QStringLiteral("send"), QStringLiteral("native"),
                           { QStringLiteral("auth-transfer"), QStringLiteral("send"),
                             QStringLiteral("--from"), from.trimmed(),
                             QStringLiteral("--to"),   to.trimmed(),
                             QStringLiteral("--amount"), amount.trimmed() },
                           from.trimmed(), to.trimmed(), amount.trimmed());
}

// ── Privacy transfers (asynchronous) ───────────────────────────────────────────

WalletPlugin::~WalletPlugin()
{
    stopSequencer();   // don't orphan the child sequencer when the module unloads
    stopTor();         // and don't orphan the bundled Tor
    qDeleteAll(m_jobs);
    m_jobs.clear();
    qDeleteAll(m_sessions);
    m_sessions.clear();
    qDeleteAll(m_requests);
    m_requests.clear();
}

QString WalletPlugin::startShield(const QString& asset, const QString& from,
                                  const QString& to, const QString& amount,
                                  const QString& definitionId)
{
    if (from.trimmed().isEmpty())   return errorJson(QStringLiteral("from account is required"));
    if (to.trimmed().isEmpty())     return errorJson(QStringLiteral("to account is required"));
    if (amount.trimmed().isEmpty()) return errorJson(QStringLiteral("amount is required"));

    bool conflict = false;
    QString fromP = withPrivacyPrefix(from, QStringLiteral("Public"), &conflict);
    if (conflict) return errorJson(QStringLiteral("shield source must be a Public account"));
    QString toP = withPrivacyPrefix(to, QStringLiteral("Private"), &conflict);
    if (conflict) return errorJson(QStringLiteral("shield destination must be a Private account"));
    if (const QString busy = privateDestInFlight(toP); !busy.isEmpty()) return errorJson(busy);

    // Token shield can't use `token send --from <owner>` (guest-panics: the owner account
    // is not a token holding) - route through the wrapper's token-shield verb, which
    // resolves a direct-owned holding of the definition or fails with a clear error.
    if (assetProgram(asset) == QStringLiteral("token")) {
        if (definitionId.trimmed().isEmpty())
            return errorJson(QStringLiteral("definitionId is required for a token shield"));
        QStringList args{ QStringLiteral("token-shield"), fromP, toP,
                          definitionId.trimmed(), amount.trimmed() };
        return startPrivacyJob(QStringLiteral("shield"), asset, args, fromP, toP, amount.trimmed());
    }

    QStringList args{ assetProgram(asset), QStringLiteral("send"),
                      QStringLiteral("--from"), fromP,
                      QStringLiteral("--to"),   toP,
                      QStringLiteral("--amount"), amount.trimmed() };
    return startPrivacyJob(QStringLiteral("shield"), asset, args, fromP, toP, amount.trimmed());
}

QString WalletPlugin::startDeshield(const QString& asset, const QString& from,
                                    const QString& to, const QString& amount,
                                    const QString& definitionId)
{
    if (from.trimmed().isEmpty())   return errorJson(QStringLiteral("from account is required"));
    if (to.trimmed().isEmpty())     return errorJson(QStringLiteral("to account is required"));
    if (amount.trimmed().isEmpty()) return errorJson(QStringLiteral("amount is required"));

    bool conflict = false;
    QString fromP = withPrivacyPrefix(from, QStringLiteral("Private"), &conflict);
    if (conflict) return errorJson(QStringLiteral("deshield source must be a Private account"));
    QString toP = withPrivacyPrefix(to, QStringLiteral("Public"), &conflict);
    if (conflict) return errorJson(QStringLiteral("deshield destination must be a Public account"));

    // Token deshield must land in a token HOLDING, not the owner's auth-transfer account -
    // the wrapper's token-deshield verb derives + creates the recipient owner's ATA.
    if (assetProgram(asset) == QStringLiteral("token")) {
        if (definitionId.trimmed().isEmpty())
            return errorJson(QStringLiteral("definitionId is required for a token deshield"));
        QStringList args{ QStringLiteral("token-deshield"), fromP, toP,
                          definitionId.trimmed(), amount.trimmed() };
        return startPrivacyJob(QStringLiteral("deshield"), asset, args, fromP, toP, amount.trimmed());
    }

    QStringList args{ assetProgram(asset), QStringLiteral("send"),
                      QStringLiteral("--from"), fromP,
                      QStringLiteral("--to"),   toP,
                      QStringLiteral("--amount"), amount.trimmed() };
    return startPrivacyJob(QStringLiteral("deshield"), asset, args, fromP, toP, amount.trimmed());
}

QString WalletPlugin::startPrivateTransfer(const QString& asset, const QString& from,
                                           const QString& to, const QString& amount)
{
    if (from.trimmed().isEmpty())   return errorJson(QStringLiteral("from account is required"));
    if (to.trimmed().isEmpty())     return errorJson(QStringLiteral("to account is required"));
    if (amount.trimmed().isEmpty()) return errorJson(QStringLiteral("amount is required"));

    bool conflict = false;
    QString fromP = withPrivacyPrefix(from, QStringLiteral("Private"), &conflict);
    if (conflict) return errorJson(QStringLiteral("private-transfer source must be a Private account"));
    QString toP = withPrivacyPrefix(to, QStringLiteral("Private"), &conflict);
    if (conflict) return errorJson(QStringLiteral("private-transfer destination must be a Private account"));
    if (const QString busy = privateDestInFlight(toP); !busy.isEmpty()) return errorJson(busy);

    QStringList args{ assetProgram(asset), QStringLiteral("send"),
                      QStringLiteral("--from"), fromP,
                      QStringLiteral("--to"),   toP,
                      QStringLiteral("--amount"), amount.trimmed() };
    return startPrivacyJob(QStringLiteral("private"), asset, args, fromP, toP, amount.trimmed());
}

QString WalletPlugin::startPrivateTransferForeign(const QString& asset, const QString& from,
                                                  const QString& toNpk, const QString& toVpk,
                                                  const QString& toIdentifier,
                                                  const QString& amount)
{
    if (from.trimmed().isEmpty())         return errorJson(QStringLiteral("from account is required"));
    if (toNpk.trimmed().isEmpty())        return errorJson(QStringLiteral("recipient npk is required"));
    if (toVpk.trimmed().isEmpty())        return errorJson(QStringLiteral("recipient vpk is required"));
    if (toIdentifier.trimmed().isEmpty()) return errorJson(QStringLiteral("recipient identifier is required"));
    if (amount.trimmed().isEmpty())       return errorJson(QStringLiteral("amount is required"));

    bool conflict = false;
    QString fromP = withPrivacyPrefix(from, QStringLiteral("Private"), &conflict);
    if (conflict) return errorJson(QStringLiteral("private-transfer source must be a Private account"));

    QStringList args{ assetProgram(asset), QStringLiteral("send"),
                      QStringLiteral("--from"),          fromP,
                      QStringLiteral("--to-npk"),        toNpk.trimmed(),
                      QStringLiteral("--to-vpk"),        toVpk.trimmed(),
                      QStringLiteral("--to-identifier"), toIdentifier.trimmed(),
                      QStringLiteral("--amount"),        amount.trimmed() };
    // Recipient is foreign - no owned "to" account to credit in local history.
    return startPrivacyJob(QStringLiteral("private"), asset, args, fromP, QString(), amount.trimmed());
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
                                      const QString& amount)
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

    const QString bin   = cliPath();
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

    QProcess* proc = new QProcess(this);
    proc->setProcessChannelMode(QProcess::SeparateChannels);
    j->proc = proc;
    m_jobs.insert(jobId, j);

    appendLog(QStringLiteral("%1 (%2): wallet %3").arg(op, j->asset, sendArgs.join(QLatin1Char(' '))));

    QObject::connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                     this, [this, jobId](int code, QProcess::ExitStatus) {
        onJobFinished(jobId, code);
    });
    QObject::connect(proc, &QProcess::errorOccurred, this,
                     [this, jobId, bin](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart) return;   // other errors arrive via finished()
        Job* job = m_jobs.value(jobId, nullptr);
        if (!job || job->state != QStringLiteral("running")) return;
        job->state  = QStringLiteral("error");
        job->result = errorJson(QStringLiteral("wallet CLI not found: ") + bin
                                + QStringLiteral(" - configure path in ⚙ settings"));
        if (job->proc) { job->proc->deleteLater(); job->proc = nullptr; }
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
        QProcessEnvironment penv = QProcessEnvironment::systemEnvironment();
        penv.insert(QStringLiteral("RISC0_DEV_MODE"), realProofs ? QStringLiteral("0") : QStringLiteral("1"));
        proc->setProcessEnvironment(penv);
    }

    proc->start(bin, sendArgs);
    // Feed the session password to the proof process's stdin (empty for plaintext
    // wallets), then close the channel so the CLI proceeds.
    if (proc->waitForStarted(3000)) {
        proc->write((m_password + QStringLiteral("\n")).toUtf8());
        proc->closeWriteChannel();
    }

    QJsonObject o;
    o[QStringLiteral("jobId")] = jobId;
    o[QStringLiteral("state")] = QStringLiteral("running");
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
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
}

QString WalletPlugin::getJob(const QString& jobId)
{
    Job* j = m_jobs.value(jobId.trimmed(), nullptr);
    if (!j)
        return errorJson(QStringLiteral("unknown jobId: ") + jobId.trimmed());

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
        if (j->state == QStringLiteral("done"))
            o[QStringLiteral("txId")] = extractTxHash(j->result);
        else
            o[QStringLiteral("error")] = r.value(QStringLiteral("error")).toString(
                QStringLiteral("privacy transfer failed"));
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

QString WalletPlugin::approveAction(const QString& requestId)
{
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

    // Dispatch to an EXISTING start* job - no new send/proof code (invariant §1).
    QString started;
    if (r->op == QStringLiteral("send")) {
        started = (r->asset == QStringLiteral("token"))
                ? startSendToken(r->from, r->to, r->definitionId, r->amount)
                : startSendTransfer(r->from, r->to, r->amount);
    } else if (r->op == QStringLiteral("shield")) {
        started = startShield(r->asset, r->from, r->to, r->amount, r->definitionId);
    } else if (r->op == QStringLiteral("deshield")) {
        started = startDeshield(r->asset, r->from, r->to, r->amount, r->definitionId);
    } else { // private
        started = r->to.isEmpty()
                ? startPrivateTransferForeign(r->asset, r->from, r->toNpk, r->toVpk,
                                              r->toIdentifier, r->amount)
                : startPrivateTransfer(r->asset, r->from, r->to, r->amount);
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

QString WalletPlugin::approveZone(const QString& requestId)
{
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
        // addZone(name, url, onion, tor): clearnet goes in url; a Tor zone goes in onion.
        const QString name = r->zoneLabel.isEmpty() ? r->zoneSeq : r->zoneLabel;
        const QString added = r->zoneTor
            ? addZone(name, QString(), r->zoneSeq, true)
            : addZone(name, r->zoneSeq, QString(), false);
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

QString WalletPlugin::setSessionPassword(const QString& password)
{
    m_password = password;
    appendLog(QStringLiteral("session password set"));
    return okJson();
}

QString WalletPlugin::clearSessionPassword()
{
    m_password.clear();
    appendLog(QStringLiteral("session locked"));
    return okJson();
}

QString WalletPlugin::getSecurityState() const
{
    QJsonObject o;
    o[QStringLiteral("hasPassword")] = !m_password.isEmpty();
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

QString WalletPlugin::unlock(const QString& password)
{
    m_password = password;
    // Probe the password with a LOCAL account list (no `-l` → no chain/balance fetch), so
    // unlock is fast and works even when the active zone's sequencer is slow/unreachable
    // (e.g. diaphani over Tor). A decryption failure still means the password was wrong.
    const QString result = runWalletCommand({ QStringLiteral("account"), QStringLiteral("list") });
    const QJsonObject o = QJsonDocument::fromJson(result.toUtf8()).object();
    if (o.contains(QStringLiteral("error"))) {
        const QString err = o.value(QStringLiteral("error")).toString();
        if (err.contains(QStringLiteral("decrypt"), Qt::CaseInsensitive)
            || err.contains(QStringLiteral("invalid password"), Qt::CaseInsensitive)) {
            m_password.clear();
            return errorJson(QStringLiteral("invalid password"));
        }
    }
    return result;   // the account list (or an unrelated error the UI surfaces)
}

QString WalletPlugin::createEncryptedWallet(const QString& password)
{
    if (password.trimmed().isEmpty())
        return errorJson(QStringLiteral("a non-empty password is required to encrypt the wallet"));

    m_password = password;
    // The first command on empty storage creates the (encrypted) wallet; creating a
    // public account is the natural trigger and yields a first usable account.
    const QString result = runWalletCommand(
        { QStringLiteral("account"), QStringLiteral("new"), QStringLiteral("public") }, 60000);
    const QJsonObject o = QJsonDocument::fromJson(result.toUtf8()).object();
    if (o.contains(QStringLiteral("error"))) {
        m_password.clear();
        return result;
    }

    QJsonObject out;
    out[QStringLiteral("ok")] = true;
    enrichFromOutput(result, out);   // parses "account_id Public/<id>" from the text
    // (the account registers on-chain lazily on its first faucet claim - kept fast here)
    // Fetch the recovery phrase cleanly via export rather than scraping create output.
    const QJsonObject mn = QJsonDocument::fromJson(exportMnemonic().toUtf8()).object();
    if (mn.value(QStringLiteral("ok")).toBool())
        out[QStringLiteral("mnemonic")] = mn.value(QStringLiteral("mnemonic")).toString();
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

QString WalletPlugin::getWalletState() const
{
    const QString storage = walletHome() + QStringLiteral("/storage.json");
    QJsonObject o;
    const bool exists = QFile::exists(storage);
    bool encrypted = false;
    if (exists) {
        QFile f(storage);
        if (f.open(QIODevice::ReadOnly)) {
            // The encrypted envelope leads with {"v":…,"kdf":…,"ct":…}; plaintext
            // storage leads with {"accounts":…}. The header is enough to tell.
            const QByteArray head = f.read(256);
            encrypted = head.contains("\"kdf\"") || head.contains("\"ct\"");
        }
    }
    o[QStringLiteral("exists")]    = exists;
    o[QStringLiteral("encrypted")] = encrypted;
    o[QStringLiteral("unlocked")]  = !m_password.isEmpty();
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

QString WalletPlugin::resetWallet()
{
    // Removing storage.json lets the next setup create a brand-new wallet.
    const QString storage = walletHome() + QStringLiteral("/storage.json");
    if (QFile::exists(storage) && !QFile::remove(storage))
        return errorJson(QStringLiteral("could not remove wallet storage at ") + storage);

    m_password.clear();
    appendLog(QStringLiteral("wallet reset - storage removed"));
    return okJson();
}

// ── Import / export ─────────────────────────────────────────────────────────────

QString WalletPlugin::restoreWallet(const QString& phrase, const QString& password, int depth)
{
    if (phrase.trimmed().isEmpty())
        return errorJson(QStringLiteral("recovery phrase is required"));
    if (depth <= 0)
        depth = 5;

    appendLog(QStringLiteral("restore wallet (depth %1)").arg(depth));
    const QStringList args{ QStringLiteral("restore-keys"),
                            QStringLiteral("--depth"), QString::number(depth) };
    // restore-keys reads the mnemonic line, then the password line, from stdin.
    const QString stdinData = phrase.trimmed() + QStringLiteral("\n") + password + QStringLiteral("\n");
    const QString result = runWalletCommandInput(args, stdinData, 300000);  // derives + syncs

    const QJsonObject o = QJsonDocument::fromJson(result.toUtf8()).object();
    if (!o.contains(QStringLiteral("error")))
        m_password = password;   // the restored store is now sealed with this password
    return result;
}

QString WalletPlugin::exportMnemonic()
{
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

QString WalletPlugin::exportKey(const QString& accountId)
{
    if (accountId.trimmed().isEmpty())
        return errorJson(QStringLiteral("accountId is required"));

    const QString result = runWalletCommand({
        QStringLiteral("account"), QStringLiteral("export-key"),
        QStringLiteral("-a"), accountId.trimmed()
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

QString WalletPlugin::importKey(const QString& privateKey, const QString& label)
{
    if (privateKey.trimmed().isEmpty())
        return errorJson(QStringLiteral("private key is required"));

    QStringList args{ QStringLiteral("account"), QStringLiteral("import-key"), privateKey.trimmed() };
    if (!label.trimmed().isEmpty())
        args << QStringLiteral("--label") << label.trimmed();

    const QString result = runWalletCommand(args);
    const QJsonObject o = QJsonDocument::fromJson(result.toUtf8()).object();
    if (o.contains(QStringLiteral("error")))
        return result;

    QJsonObject out;
    out[QStringLiteral("ok")] = true;
    enrichFromOutput(result, out);   // parses the imported "account_id Public/<id>"
    return QJsonDocument(out).toJson(QJsonDocument::Compact);
}
