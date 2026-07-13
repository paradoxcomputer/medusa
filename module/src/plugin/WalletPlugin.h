#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QProcess>
#include <QHash>
#include <QElapsedTimer>

#include "interface.h"

class WalletPlugin : public QObject, public PluginInterface
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.logos.WalletModuleInterface" FILE "metadata.json")
    Q_INTERFACES(PluginInterface)

public:
    explicit WalletPlugin(QObject* parent = nullptr);
    ~WalletPlugin() override;

    QString name()    const override { return QStringLiteral("medusa_core"); }
    QString version() const override { return QStringLiteral("0.2.0"); }

    Q_INVOKABLE void    initLogos(LogosAPI* api);

    // Status - checks if wallet CLI binary is available
    Q_INVOKABLE QString getStatus() const;

    // Config - wallet CLI binary path
    Q_INVOKABLE QString getConfig() const;
    Q_INVOKABLE QString setCliPath(const QString& path);

    // ── Sequencer (local auto-launch vs hosted URL) ───────────────────────────
    // {mode,url,port,seqPath,seqPathEff,effectiveAddr}. mode = "local" | "hosted".
    Q_INVOKABLE QString getSequencerConfig() const;
    // Persist mode/url/port; project sequencer_addr into wallet_config.json; in local
    // mode (re)launch the bundled standalone sequencer, in hosted mode stop it. {ok}.
    Q_INVOKABLE QString setSequencerConfig(const QString& mode, const QString& url, int port);
    // {state,mode,port} - state = "running" | "starting" | "unreachable".
    Q_INVOKABLE QString getSequencerStatus();
    // Bundled-Tor bootstrap progress (for the connect progress bar on Tor zones):
    // {percent, stage}. Parsed from the bundled Tor's log.
    Q_INVOKABLE QString getTorProgress() const;
    // The active zone id (back-compat alias of getZones' active): {network}.
    Q_INVOKABLE QString getNetwork() const;
    // Switch the active zone (alias of setActiveZone). {ok}.
    Q_INVOKABLE QString setNetwork(const QString& network);

    // ── Zones (LEZ chains the wallet can switch between, token-agnostic) ───────
    // {zones:[{id,name,kind,endpoint,tor,builtin}], active}. kind = local-standalone
    // (devnet) | local-l1-tor (diaphani) | remote (a thin client of someone's sequencer).
    Q_INVOKABLE QString getZones() const;
    // Add a REMOTE zone (thin client). endpoint is a clearnet URL, or set onion+tor=true
    // to reach a Tor-fronted sequencer. {ok,id}.
    Q_INVOKABLE QString addZone(const QString& name, const QString& url,
                                const QString& onion, bool tor);
    // Edit a user zone's name/endpoint/transport (built-ins can't be edited). {ok}.
    Q_INVOKABLE QString editZone(const QString& id, const QString& name, const QString& url,
                                 const QString& onion, bool tor);
    // Remove a user zone (built-ins can't be removed). {ok}.
    Q_INVOKABLE QString removeZone(const QString& id);
    // Switch the active zone - repoints the wallet (local sequencer, or thin client over
    // Tor/clearnet) and reloads. {ok}.
    Q_INVOKABLE QString setActiveZone(const QString& id);

    // Account management
    Q_INVOKABLE QString listAccounts();
    // Token holdings for an account: [{definitionId, ticker, balance, ataBalance,
    // vaultBalance}] across every known definition - balance = owner ATA + the wallet's
    // designated vault; only vaultBalance can shield on rc5 (the wrapper keeps the registry).
    Q_INVOKABLE QString getTokens(const QString& accountId);
    // Register a token definition id the user received but didn't mint. {ok}.
    Q_INVOKABLE QString addToken(const QString& definitionId);
    // DIRECT-owned token holdings (keytree-signable - the only valid token-shield sources
    // on rc5): [{definitionId,ticker,balance,account}]. Feeds the shield asset picker.
    Q_INVOKABLE QString getDirectHoldings();
    // Known token definitions ({definitions:[…], names:{def:ticker}}) - deshield picker.
    Q_INVOKABLE QString getTokenRegistry();
    // Move an owner's ATA balance of a token into the wallet's direct vault holding
    // (making it shieldable). Two chained on-chain steps - SLOW on 60s-block zones;
    // meant for dApp/async callers, the UI's token-shield auto-top-up covers the same.
    // ⚠ UNGATED on-chain write: callable by any module without user approval (it only
    // moves funds between the user's OWN accounts). Gate behind a permission before
    // advertising it as a Connect verb.
    Q_INVOKABLE QString consolidateToken(const QString& accountId, const QString& definitionId);
    // The curated whitelist of tokens the treasury offers: [{name, def}].
    Q_INVOKABLE QString getWhitelist();
    Q_INVOKABLE QString getBalance(const QString& accountId);
    Q_INVOKABLE QString createAccount();
    Q_INVOKABLE QString initAccount(const QString& accountId);
    // Set/clear a user-friendly name for an account (stored module-side, merged into
    // listAccounts as "name"). Empty name clears it. {ok}.
    Q_INVOKABLE QString setAccountName(const QString& accountId, const QString& name);

    // Private account management
    // Create an owned private account (account new private). Returns {ok,id,npk,vpk}.
    Q_INVOKABLE QString createPrivateAccount(const QString& label);
    // Create a receive-only private key node (account new private-accounts-key). Returns {ok,npk,vpk}.
    Q_INVOKABLE QString createPrivateReceiveKey();
    // Sync owned private accounts so their balances become visible (account sync-private).
    Q_INVOKABLE QString syncPrivate();
    // Async variant: kick the sync in the background (never blocks the UI / crashes under load).
    // startSyncPrivate() returns {ok|error}; poll syncPrivateStatus() → {running,error}.
    Q_INVOKABLE QString startSyncPrivate();
    Q_INVOKABLE QString syncPrivateStatus();
    // Reveal an account's keys (pk for public, npk/vpk for private) for sharing. Returns {ok,pk|npk,vpk}.
    Q_INVOKABLE QString getAccountKeys(const QString& accountId);

    // Faucet - asynchronous: returns {jobId,state}, poll getJob(jobId). The claim
    // submits a tx and waits for a block (~15s), so it must not block the module RPC.
    Q_INVOKABLE QString startFaucet(const QString& accountId);
    // Faucet (synchronous, legacy/unused - superseded by startFaucet).
    Q_INVOKABLE QString claimFaucet(const QString& accountId);

    // Transfer
    Q_INVOKABLE QString sendTransfer(const QString& from,
                                     const QString& to,
                                     const QString& amount);
    // Async native send (the main Send screen). Runs as a background job because the dest may
    // be a Private account → a multi-minute real proof that must NOT block the module RPC
    // (blocking sendTransfer timed out / froze the UI). Returns {jobId}; track like a privacy op.
    Q_INVOKABLE QString startSendTransfer(const QString& from,
                                          const QString& to,
                                          const QString& amount);
    // Send a token (asynchronous - derives/creates ATAs + token-send + waits, ~30-40s, so
    // it must not block the module RPC). Returns {jobId,state}; poll getJob(jobId).
    Q_INVOKABLE QString startSendToken(const QString& from, const QString& to,
                                       const QString& definitionId, const QString& amount);

    // ── Privacy transfers (asynchronous - generate a local STARK, may take minutes) ──
    // Each "start*" returns {jobId,state} immediately; poll getJob(jobId) for progress.
    // asset is "native" (auth-transfer program) or "token" (token program).

    // Public -> Private (shield): from must be Public/…, to must be Private/… (owned).
    // Token asset REQUIRES definitionId: the wrapper resolves a direct-owned holding of
    // that definition as the signing source (an ATA is a PDA and cannot sign - rc5 limit).
    Q_INVOKABLE QString startShield(const QString& asset, const QString& from,
                                    const QString& to, const QString& amount,
                                    const QString& definitionId = QString());
    // Private -> Public (deshield): from must be Private/…, to must be Public/…
    // Token asset REQUIRES definitionId: the wrapper lands the tokens in the recipient
    // owner's ATA (created idempotently), the only valid public token destination.
    Q_INVOKABLE QString startDeshield(const QString& asset, const QString& from,
                                      const QString& to, const QString& amount,
                                      const QString& definitionId = QString());
    // Private -> Private (PrivOwned transfer, owned recipient): both must be Private/…
    Q_INVOKABLE QString startPrivateTransfer(const QString& asset, const QString& from,
                                             const QString& to, const QString& amount);
    // Private -> foreign private recipient via shared keys (--to-npk/--to-vpk/--to-identifier).
    Q_INVOKABLE QString startPrivateTransferForeign(const QString& asset, const QString& from,
                                                     const QString& toNpk, const QString& toVpk,
                                                     const QString& toIdentifier,
                                                     const QString& amount);
    // Poll the state of a privacy job. Returns {jobId,op,asset,from,to,amount,state,elapsedMs,result?,txId?,error?}.
    Q_INVOKABLE QString getJob(const QString& jobId);

    // ── Medusa-Connect (sessions + per-action approval) ──────────────────────────
    // The dApp-facing connect surface (contract: docs/MEDUSA_CONNECT_CONTRACT.md). A
    // foreign module asks for a session (connectRequest → approveConnect) then asks for
    // each write (requestAction → approveAction); the user gates both in the wallet UI.
    // approveAction NEVER reimplements send/proof logic - it dispatches to the existing
    // start* jobs and surfaces their jobId (tracked via the unchanged getJob).
    Q_INVOKABLE QString connectRequest(const QString& appJson, const QString& permsJson);
    Q_INVOKABLE QString pendingRequests();
    Q_INVOKABLE QString approveConnect(const QString& requestId, const QString& accountsJson);
    Q_INVOKABLE QString rejectConnect(const QString& requestId);
    Q_INVOKABLE QString sessionInfo(const QString& sessionId);
    Q_INVOKABLE QString requestAction(const QString& sessionId, const QString& actionJson);
    Q_INVOKABLE QString approveAction(const QString& requestId);
    Q_INVOKABLE QString actionStatus(const QString& requestId);
    Q_INVOKABLE QString revokeSession(const QString& sessionId);
    // ── Connect with Medusa: dApp-requested zone switch (user-approved) ──────────
    // The dApp asks the wallet to switch to a sequencer/zone (requestZone → approveZone);
    // the user gates it in the wallet UI. zoneJson = {sequencer,tor,label}. Mirrors the
    // action surface: requestZone returns {requestId}; the dApp polls actionStatus(requestId)
    // → approved (with "zoneId") | rejected (with "error").
    Q_INVOKABLE QString requestZone(const QString& sessionId, const QString& zoneJson);
    Q_INVOKABLE QString approveZone(const QString& requestId);
    Q_INVOKABLE QString rejectZone(const QString& requestId);

    // ── Wallet security: encrypted-storage unlock ──────────────────────────────
    // The wallet CLI gates an encrypted store on a password (read from stdin).
    // The session password is held in memory only - never persisted.

    // Wallet lifecycle state for onboarding: {exists, encrypted, unlocked}.
    // Read straight from the storage file so the UI can show "create" vs "unlock"
    // vs "ready" WITHOUT running a CLI command (which would auto-create a wallet).
    Q_INVOKABLE QString getWalletState() const;

    // Set the in-memory session password used for every subsequent CLI call.
    Q_INVOKABLE QString setSessionPassword(const QString& password);
    // Forget the session password (lock).
    Q_INVOKABLE QString clearSessionPassword();
    // {hasPassword: bool} - whether a session password is currently set.
    Q_INVOKABLE QString getSecurityState() const;
    // Set the password and verify it by listing accounts. Returns the account list
    // on success, or {error} (e.g. wrong password) - in which case the password is
    // cleared again.
    Q_INVOKABLE QString unlock(const QString& password);
    // Create a fresh password-encrypted wallet + first public account. Returns
    // {ok,id,mnemonic} (the recovery phrase the user must back up).
    Q_INVOKABLE QString createEncryptedWallet(const QString& password);
    // Erase the local wallet storage so the user can start over (e.g. a locked
    // wallet whose password is lost). Clears the session password. {ok}.
    Q_INVOKABLE QString resetWallet();

    // ── Import / export ─────────────────────────────────────────────────────────
    // Restore a wallet from a 24-word recovery phrase, deriving `depth` accounts,
    // sealing the new store with `password`. Long-running (re-derives + syncs).
    Q_INVOKABLE QString restoreWallet(const QString& phrase, const QString& password, int depth);
    // Reveal the recovery phrase (encrypted wallets only). {ok,mnemonic}.
    Q_INVOKABLE QString exportMnemonic();
    // Reveal a public account's private signing key (hex). {ok,privateKey}.
    Q_INVOKABLE QString exportKey(const QString& accountId);
    // Import a public account from a raw private signing key. {ok,output}.
    Q_INVOKABLE QString importKey(const QString& privateKey, const QString& label);

    // Transaction history (locally stored)
    Q_INVOKABLE QString getTransactions(const QString& accountId);

signals:
    void eventResponse(const QString& eventName, const QVariantList& data);

private:
    QString runWalletCommand(const QStringList& args, int timeoutMs = 30000);
    // Run the CLI feeding `stdinData` (the session password, plus the mnemonic line
    // for restore) to its stdin; reads stdout for the result and stderr for errors.
    QString runWalletCommandInput(const QStringList& args, const QString& stdinData,
                                  int timeoutMs = 30000);
    static QString cleanStderr(const QString& raw);
    QString cliPath() const;
    // The wallet home dir the CLI wrapper uses (LEE_WALLET_HOME_DIR, rc4 NSSA_ fallback, else default).
    static QString walletHome();
    // Register an account on-chain (auth-transfer init) iff it is still uninitialized.
    // A fresh account exists only locally; it must be registered before it can claim the
    // faucet or be spent from. init is NOT idempotent, so we check chain state first. {ok}.
    QString ensureInitialized(const QString& accountId);

    // ── Local sequencer lifecycle (plugin-owned standalone process) ───────────
    QProcess*      m_seqProc = nullptr;       // the bundled standalone sequencer (local mode)
    QProcess*      m_fwdProc = nullptr;       // diaphani-forward (Tor tunnel) - Tor zones
    void           ensureForward(int listenPort, const QString& onion);  // Tor tunnel to an .onion
    void           stopForward();             // terminate/kill/wait the forward
    QProcess*      m_torProc = nullptr;       // bundled Tor (private SOCKS) - no external Tor needed
    QProcess*      m_torMonProc = nullptr;    // tor-control monitor → real onion-connection stages
    void           ensureTor();               // launch the bundled Tor (idempotent, non-blocking)
    void           stopTor();                 // terminate/kill/wait Tor (+ its monitor)
    // Async health probe (so a slow Tor round-trip never blocks the UI / 1s-times-out the dot).
    QProcess*      m_healthProbe = nullptr;
    bool           m_lastSeqOk = false;       // cached: did the last async checkHealth succeed?
    void           probeSeqHealthAsync(const QString& url);  // fire-and-forget, updates m_lastSeqOk
    // Async balances: `account list -l` over Tor is slow + would freeze/crash the UI, so the
    // account list is served LOCALLY (no -l, instant) with balances merged from this cache,
    // refreshed by a background fetch.
    QProcess*      m_acctFetchProc = nullptr;
    QString        m_balanceCacheJson;        // last successful `account list -l` JSON
    void           fetchBalancesAsync();      // background `account list -l` → updates the cache
    // Async private-state sync (account sync-private is a slow block-scan/decrypt over Tor;
    // running it blocking froze the UI and crashed the module under load).
    QProcess*      m_syncProc = nullptr;
    bool           m_syncRunning = false;
    QString        m_syncErr;
    // ── Zone helpers ──
    QJsonArray     userZones() const;          // user-added remote zones [{id,name,url,onion,tor}]
    QJsonObject    zoneObj(const QString& id) const;  // a remote zone's record (incl. the built-in clearnet zone)
    QString        zoneKind(const QString& id) const; // local-standalone|local-l1-tor|remote
    bool           isUserZone(const QString& id) const; // true only for user-added zones (not built-ins)
    QString        seqPath() const;            // resolve the sequencer binary (mirror cliPath)
    QString        seqHome() const;            // <walletHome>/sequencer (db + config)
    void           ensureSequencer();          // launch iff local mode + not already reachable
    void           stopSequencer();            // terminate/kill/wait the child
    void           writeSeqConfig(const QString& cfgPath) const;   // seed the standalone config
    static bool    seqHealthy(int port, int timeoutMs = 1000);     // checkHealth on 127.0.0.1:port
    static bool    seqHealthyUrl(const QString& url, int timeoutMs = 1000);  // checkHealth on a full URL
    QString        netId() const;             // active network id ("devnet"|"testnet")
    int            netPort() const;           // local sequencer port for the active network
    QString        applySequencer();          // recompute wallet_config.json addr + (re)launch/stop; returns effective addr

    // In-memory session password ("" = legacy plaintext wallet). Never persisted.
    QString m_password;

    // Normalise raw CLI output (merged stdout/stderr) + exit code into the module's
    // JSON contract: pass through JSON as-is, otherwise wrap text in {ok,output} /
    // {error}. Shared by the synchronous and asynchronous code paths.
    static QString normalizeCliOutput(const QString& rawOut, int exitCode);
    static QString errorJson(const QString& msg);
    static QString okJson();

    // Map an asset name ("native"/"token") to its CLI program subcommand.
    static QString assetProgram(const QString& asset);
    // Ensure an account id carries the required privacy prefix ("Public"/"Private").
    // Returns the prefixed id, or an empty string on an explicit prefix conflict.
    static QString withPrivacyPrefix(const QString& id, const QString& kind, bool* conflict);
    // Best-effort extraction of a tx hash from a normalised CLI JSON result
    // (reads "txId"/"txHash", else parses "Transaction hash is <hash>" from "output").
    static QString extractTxHash(const QString& normalizedJson);

    void appendLog(const QString& line, const QString& level = QStringLiteral("info"));
    void saveTx(const QString& accountId, const QJsonObject& entry);
    // One-time migration of legacy logos-wallet QSettings keys + home dir to the medusa-wallet naming.
    void migrateLegacyNaming();

    // ── Asynchronous privacy jobs ──────────────────────────────────────────────
    struct Job {
        QProcess*      proc = nullptr;
        QString        id;
        QString        op;        // shield | deshield | private
        QString        asset;     // native | token
        QString        from;
        QString        to;        // owned recipient id, or "" for a foreign recipient
        QString        amount;
        QString        state;     // running | done | error
        QString        phase;     // processing | sent  (advisory sub-state while running)
        QString        result;    // normalised CLI JSON once terminal
        QString        outBuf;    // stdout accumulated incrementally (for phase detection)
        bool           killedByTimeout = false;
        QElapsedTimer  timer;
    };

    // Non-empty error message if a running shield/private job already targets this
    // private destination (double-booking a fresh account would waste the second proof).
    QString privateDestInFlight(const QString& toP) const;
    // Build args + spawn a privacy "send" as a background job; returns {jobId,state}.
    QString startPrivacyJob(const QString& op, const QString& asset,
                            const QStringList& sendArgs,
                            const QString& from, const QString& to, const QString& amount);
    void    onJobFinished(const QString& jobId, int exitCode);

    QHash<QString, Job*> m_jobs;
    int  m_jobSeq = 0;
    static constexpr int kMaxJobs        = 24;
    // Job safety-kill budget. Real STARK proofs on a busy machine measured 20-40+ min
    // (native ~20-35, token ~40 on a half-loaded 16-core box); 30 min killed genuine
    // proofs mid-flight. The wrapper's own per-step budgets (MEDUSA_PROOF_TIMEOUT_S,
    // default 3600s proof + up to ~25 min of sync/ata pre-steps) must win - this kill
    // exists only for a truly wedged process, so it is proof budget + 30 min slack.
    static int proveTimeoutMs();

    // ── Medusa-Connect: in-memory sessions + requests ───────────────────────────
    // Like m_jobs, these do NOT persist across module reloads - by design.
    struct ConnectSession {
        QString id;                 // "ses-…"
        QString appName;            // from appJson.appName
        QString appIcon;            // from appJson.icon (data: URI or "")
        QString origin;             // from appJson.origin (module name/id, "" if absent)
        QStringList accounts;       // granted account ids ("Public/…","Private/…")
        QStringList perms;          // granted permissions subset
        QString zone;               // pinned zone: netId() at approveConnect, re-pinned by the
                                    // session's own approved requestZone (approveAction guard)
        QString createdTs;
    };

    struct ConnectRequest {
        QString id;                 // "req-…"
        QString kind;               // "connect" | "action" | "zone"
        QString state;              // "pending" | "approved" | "rejected"
        // connect-kind fields:
        QString appName, appIcon, origin;
        QStringList perms;          // requested perms
        // zone-kind fields:
        QString zoneSeq, zoneLabel; // requested sequencer endpoint + display label
        bool    zoneTor = false;    // reach the sequencer over Tor (.onion)
        QString zoneId;             // "" until approveZone resolves/creates the zone
        // action-kind fields:
        QString sessionId;          // owning session
        QString op;                 // "send" | "shield" | "deshield" | "private"
        QString asset;              // "native" | "token"
        QString definitionId;       // token def id ("" for native)
        QString from, to, amount;   // owned-recipient form
        QString toNpk, toVpk, toIdentifier;  // foreign-recipient form (private only)
        // result fields (filled on approve):
        QString jobId;              // "" until approveAction starts a job
        QString sessionMinted;      // "" until approveConnect mints a session (connect-kind)
        QString error;              // "" unless rejected/failed
        QString createdTs;
        qint64  createdMs = 0;      // epoch ms at creation (for the pending-request TTL)
        qint64  seq = 0;            // monotonic insert order (for newest-first / cap eviction)
    };

    QHash<QString, ConnectSession*> m_sessions;   // sessionId -> session
    QHash<QString, ConnectRequest*> m_requests;   // requestId -> request
    int m_connReqSeq = 0;                          // → "req-<n>"
    static constexpr int kMaxConnRequests = 64;    // bound the map; drop oldest terminal
    static constexpr qint64 kReqTtlMs = 15 * 60 * 1000; // a pending request expires after 15 min
                                                        // (generous: the user may app-switch +
                                                        // unlock before approving in the wallet)

    // Allocate a "ses-<hex16>" id (8 random bytes, hex). Opaque to JS.
    static QString newSessionId();
    // Drop the oldest terminal (approved/rejected) connect requests once at the cap.
    void evictOldConnRequests();
    // Serialise one pending request into its pendingRequests() element.
    QJsonObject pendingRequestJson(const ConnectRequest* r) const;
    // op → required permission ("send"/"shield"/"deshield"/"private").
    static QString permForOp(const QString& op);

    struct LogEntry { QString ts; QString msg; QString level; };
    QList<LogEntry> m_log;
    static constexpr int kMaxLogLines = 200;
};
