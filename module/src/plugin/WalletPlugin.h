#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QProcess>
#include <QProcessEnvironment>
#include <QHash>
#include <QElapsedTimer>
#include <QTimer>

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
    // Must track modules/medusa_core/manifest.json. It was left at 0.2.0 through two ABI-breaking
    // rounds (every gated verb grew a trailing password), so a host or a UI that consulted it was
    // told the old surface was still there.
    QString version() const override { return QStringLiteral("0.4.0"); }

    // ── THE THREE INVARIANTS THIS CLASS ENFORCES ──────────────────────────────────
    // Rounds 1 and 2 each fixed the reviewer's example and each shipped the same capability
    // under a different name, so these are stated as properties of the whole class, and each one
    // is checked by grepping the file rather than by reading one function.
    //
    // A. m_password is ASSIGNED in exactly one function, establishSession(), and only after the
    //    wallet CLI has opened the store ON DISK with that exact value; it is CLEARED in exactly
    //    one function, clearSessionPassword(). `grep -n "m_password =" WalletPlugin.cpp` must
    //    return one line and `grep -n "m_password.clear()"` must return one line. In particular
    //    no verb that CREATES or RE-SEALS a store may leave a session behind: sealing a store
    //    with a password the caller chose proves nothing about who the caller is, and conflating
    //    the two is what made destroy-then-create a session mint.
    //
    // B. No externally-writable SETTING may influence which binary this module executes. Every
    //    QProcess::start() program string comes from exactly one of: a literal, a launcher-owned
    //    environment variable, or resolveBin() (the module's own bundle). QSettings is never
    //    consulted for a path. `grep -n "start(" WalletPlugin.cpp` enumerates the call sites and
    //    every one of them must resolve that way. This does NOT claim immunity from an attacker
    //    who overwrites the bundled binaries themselves - see resolveBin() for what is and is not
    //    defended.
    //
    // C. A legacy PLAINTEXT store is NOT protected and the module never pretends otherwise. Its
    //    keys are readable by anything running at this uid, so no gate here can defend it.
    //    authorize() therefore does NOT special-case it: it consults the secret and nothing else,
    //    because the "is the store plaintext" question is answered by a file an attacker at this
    //    uid can rewrite, and a security decision must never consult attacker-writable state.
    //    Consequence, stated plainly: on a plaintext store unlock() refuses and every gated verb
    //    refuses with reason "unencrypted". The owner is NOT stranded: reads, reset and restore
    //    stay available, and encryptPlaintextWallet() migrates the store in place (it needs no
    //    session, which is safe precisely because those keys are already exposed on disk), after
    //    which a normal unlock restores full function with the accounts intact.

    Q_INVOKABLE void    initLogos(LogosAPI* api);

    // Status - checks if wallet CLI binary is available
    Q_INVOKABLE QString getStatus() const;

    // Config - wallet CLI binary path. {cliPath (the disowned stored value, if any), cliPathEff,
    // cliPathIgnored, cliPathConfigurable:false}.
    Q_INVOKABLE QString getConfig() const;
    // REMOVED, and kept only so an older UI gets an explanation instead of a failed metacall:
    // this ALWAYS refuses now. The stored override won over the bundled binary in cliPath() and
    // was handed the session password on stdin, i.e. arbitrary code execution plus password
    // capture that outlived a reboot. Neither a password gate nor a path/filename filter can
    // hold it: the QSettings backing file is a user-writable INI, so a co-resident module writes
    // medusa-wallet/cliPath directly with no IPC at all, and it runs as the same uid so it can
    // also drop a file named wallet* into ~/.local/bin or the module's own bin dir to satisfy any
    // location rule. cliPath() no longer reads QSettings; MEDUSA_WALLET_CLI (this process's
    // environment, which a co-resident module cannot write) is the one remaining override.
    Q_INVOKABLE QString setCliPath(const QString& path, const QString& password);

    // ── Sequencer (local auto-launch vs hosted URL) ───────────────────────────
    // {mode,url,port,seqPath,seqPathEff,seqPathIgnored,seqPathConfigurable:false,effectiveAddr}.
    // seqPath is the DISOWNED stored value (see seqPath() in the .cpp - the medusa-wallet/seqPath
    // setting is no longer read on the execution path), kept only so a poisoned install shows
    // what was planted. mode = "local" | "hosted".
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
    // GATED (authorize()): an on-chain write, so it takes the session password like every
    // other verb that spends. Still NOT part of the Connect op surface.
    Q_INVOKABLE QString consolidateToken(const QString& accountId, const QString& definitionId,
                                         const QString& password);
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
    // Every verb below is GATED (authorize()): the trailing `password` is the session password
    // the user typed at unlock, re-presented by the caller on each spend.
    Q_INVOKABLE QString sendTransfer(const QString& from,
                                     const QString& to,
                                     const QString& amount,
                                     const QString& password);
    // Async native send (the main Send screen). Runs as a background job because the dest may
    // be a Private account → a multi-minute real proof that must NOT block the module RPC
    // (blocking sendTransfer timed out / froze the UI). Returns {jobId}; track like a privacy op.
    Q_INVOKABLE QString startSendTransfer(const QString& from,
                                          const QString& to,
                                          const QString& amount,
                                          const QString& password);
    // Send a token (asynchronous - derives/creates ATAs + token-send + waits, ~30-40s, so
    // it must not block the module RPC). Returns {jobId,state}; poll getJob(jobId).
    Q_INVOKABLE QString startSendToken(const QString& from, const QString& to,
                                       const QString& definitionId, const QString& amount,
                                       const QString& password);

    // ── Privacy transfers (asynchronous - generate a local STARK, may take minutes) ──
    // Each "start*" returns {jobId,state} immediately; poll getJob(jobId) for progress.
    // asset is "native" (auth-transfer program) or "token" (token program).

    // Public -> Private (shield): from must be Public/…, to must be Private/… (owned).
    // Token asset REQUIRES definitionId: the wrapper resolves a direct-owned holding of
    // that definition as the signing source (an ATA is a PDA and cannot sign - rc5 limit).
    // `password` keeps the trailing position it has on every other spend verb, which forces
    // a default here because definitionId already has one; the default is the empty password,
    // which authorize() always rejects, so the short-arity form fails closed.
    Q_INVOKABLE QString startShield(const QString& asset, const QString& from,
                                    const QString& to, const QString& amount,
                                    const QString& definitionId = QString(),
                                    const QString& password = QString());
    // Private -> Public (deshield): from must be Private/…, to must be Public/…
    // Token asset REQUIRES definitionId: the wrapper lands the tokens in the recipient
    // owner's ATA (created idempotently), the only valid public token destination.
    Q_INVOKABLE QString startDeshield(const QString& asset, const QString& from,
                                      const QString& to, const QString& amount,
                                      const QString& definitionId = QString(),
                                      const QString& password = QString());
    // Private -> Private (PrivOwned transfer, owned recipient): both must be Private/…
    Q_INVOKABLE QString startPrivateTransfer(const QString& asset, const QString& from,
                                             const QString& to, const QString& amount,
                                             const QString& password);
    // Private -> foreign private recipient via shared keys (--to-npk/--to-vpk/--to-identifier).
    Q_INVOKABLE QString startPrivateTransferForeign(const QString& asset, const QString& from,
                                                     const QString& toNpk, const QString& toVpk,
                                                     const QString& toIdentifier,
                                                     const QString& amount,
                                                     const QString& password);
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
    // GATED (authorize()): this is the wallet-side "the user pressed Approve" verb and it
    // dispatches straight into the spend verbs, so leaving it open would reopen every one of
    // them through connectRequest → approveConnect → requestAction → approveAction.
    Q_INVOKABLE QString approveAction(const QString& requestId, const QString& password);
    Q_INVOKABLE QString actionStatus(const QString& requestId);
    Q_INVOKABLE QString revokeSession(const QString& sessionId);
    // ── Connect with Medusa: dApp-requested zone switch (user-approved) ──────────
    // The dApp asks the wallet to switch to a sequencer/zone (requestZone → approveZone);
    // the user gates it in the wallet UI. zoneJson = {sequencer,tor,label}. Mirrors the
    // action surface: requestZone returns {requestId}; the dApp polls actionStatus(requestId)
    // → approved (with "zoneId") | rejected (with "error").
    Q_INVOKABLE QString requestZone(const QString& sessionId, const QString& zoneJson);
    // GATED (authorize()), exactly like its twin approveAction: this is the wallet-side "the user
    // pressed Approve" verb and what it approves is REPOINTING THE WALLET AT A SEQUENCER. Ungated
    // it completed a chain with no user in it at all - connectRequest -> approveConnect ->
    // requestZone -> approveZone, every step callable by a co-resident module - leaving the wallet
    // talking to the attacker's sequencer, which sees every public transaction, can censor them
    // and controls every balance the UI shows. Zone SELECTION (addZone/setActiveZone/setNetwork)
    // stays ungated because onboarding and the lock screen need it while locked; approving a
    // foreign app's zone switch does not.
    Q_INVOKABLE QString approveZone(const QString& requestId,
                                    const QString& password = QString());
    Q_INVOKABLE QString rejectZone(const QString& requestId);

    // ── Wallet security: encrypted-storage unlock ──────────────────────────────
    // The wallet CLI gates an encrypted store on a password (read from stdin).
    // The session password is held in memory only - never persisted.

    // Wallet lifecycle state for onboarding: {exists, encrypted, unlocked}.
    // Read straight from the storage file so the UI can show "create" vs "unlock"
    // vs "ready" WITHOUT running a CLI command (which would auto-create a wallet).
    Q_INVOKABLE QString getWalletState() const;

    // NOTE: there is deliberately no setSessionPassword verb. It was the whole gate's undoing:
    // clearSessionPassword() is ungated by design, so a caller emptied m_password and then set
    // it to a password of its own choosing (the conditional gate could not fire on an empty
    // m_password), after which it could prove that password to every gated verb.
    //
    // Deleting it was not enough, because "has just SEALED the store" was still accepted as a
    // provenance for a session, and TWO ungated verbs could cause a seal: encryptPlaintextWallet
    // directly, and createEncryptedWallet after an ungated resetWallet had removed the store that
    // made it refuse. Both handed the caller a live session on a store the caller itself had just
    // written. The invariant now is narrower and is invariant A above: a session exists only when
    // the CLI has opened the store on disk with that password, and only establishSession() may
    // write it. Sealing a store grants nothing.
    //
    // Forget the session password (lock). This is the UI's Lock button and the auto-lock timer's
    // action. Deliberately UNGATED: locking must never need the password, or a user who fears
    // shoulder-surfing could not lock. It also drops the log ring. It does NOT scrub the freed
    // heap: QString::clear() releases its buffer without zeroing, and the CLI runner makes short
    // lived copies of the password on every call, so a core dump or swap page can still hold it.
    Q_INVOKABLE QString clearSessionPassword();
    // {hasPassword, autoLockMs, idleMs, unencrypted, protected, warning} - whether a session
    // password is currently set, the idle auto-lock budget, how long since the last PRIVILEGED
    // action (a background status poll is not activity) so the UI can warn before the session
    // lapses, and whether the store on disk is legacy PLAINTEXT. `protected` is the honest
    // summary the UI should show: FALSE on a plaintext store no matter what hasPassword says,
    // because nothing there is protected and `warning` says so in words.
    Q_INVOKABLE QString getSecurityState() const;
    // Establish the session: verify `password` by opening the store on disk with the CLI, and
    // keep it only if that worked. Returns the account list on success, or {error,reason}
    // (unauthorized / rate-limited / unencrypted / no-wallet). Refuses when there is NO store:
    // `account list` on an empty wallet home makes the CLI CREATE one sealed with whatever
    // password it was handed, so on a fresh install unlock() itself was a session mint.
    Q_INVOKABLE QString unlock(const QString& password);
    // Create a fresh password-encrypted wallet + first public account, for ONBOARDING only.
    // Returns {ok,id,mnemonic,locked:true}: the recovery phrase the user must back up, and the
    // fact that NO session was established. Refuses when a store already exists (reason
    // "wallet-exists"), or when it exists unencrypted (reason "wallet-not-encrypted" - that is
    // encryptPlaintextWallet's job, and re-sealing someone's wallet here locked its owner out).
    Q_INVOKABLE QString createEncryptedWallet(const QString& password);
    // Migrate a legacy PLAINTEXT store to an encrypted one: the separate, explicitly named verb
    // that createEncryptedWallet used to do by accident. Only acts when the store exists and is
    // unencrypted, always COPIES it aside first, and PUTS THE COPY BACK if the sealed store does
    // not open with the password it was given - so a migration cannot leave its owner with a
    // wallet nobody can open. Returns {ok,migrated,backup,mnemonic:"",note,locked:true}.
    // There is no credential to demand here and there cannot be one: a plaintext store is already
    // readable by anything running as this uid. Reversibility is the guarantee, not secrecy - and
    // it grants NO session, so it cannot be used to mint one either.
    Q_INVOKABLE QString encryptPlaintextWallet(const QString& password);
    // Erase the local wallet storage so the user can start over (e.g. a locked
    // wallet whose password is lost). Clears the session password. {ok,backup}.
    // The store is never unlinked, it is renamed aside. Gating (see the .cpp for the full
    // reasoning): a caller that presents a password must present one the STORE accepts - checked
    // against the store, not against the in-memory session, so clearing the session first no
    // longer converts a checked path into an unchecked one - and a live session must present it.
    // A LOCKED caller presenting nothing is the forgot-password path and stays open.
    Q_INVOKABLE QString resetWallet(const QString& password = QString());

    // ── Import / export ─────────────────────────────────────────────────────────
    // Restore a wallet from a 24-word recovery phrase, deriving `depth` accounts,
    // sealing the new store with `password`. Long-running (re-derives + syncs).
    // `sessionPassword` is the CURRENT session password, not the new one: same rule as
    // resetWallet - required when a session is live, not required while locked (the phrase is
    // the credential there), and the outgoing store is always backed up first. A LOCKED restore
    // no longer establishes a session: `password` is the caller's choice, so keeping it would
    // have turned this ungated path into "mint any session password you like".
    Q_INVOKABLE QString restoreWallet(const QString& phrase, const QString& password, int depth,
                                      const QString& sessionPassword = QString());
    // Reveal the recovery phrase (encrypted wallets only). GATED. {ok,mnemonic}.
    Q_INVOKABLE QString exportMnemonic(const QString& password);
    // Reveal a public account's private signing key (hex). GATED. {ok,privateKey}.
    Q_INVOKABLE QString exportKey(const QString& accountId, const QString& password);
    // Import a public account from a raw private signing key. {ok,output}.
    Q_INVOKABLE QString importKey(const QString& privateKey, const QString& label);

    // Transaction history (locally stored)
    Q_INVOKABLE QString getTransactions(const QString& accountId);

signals:
    void eventResponse(const QString& eventName, const QVariantList& data);

private:
    // ── Proof-of-user gate ─────────────────────────────────────────────────────
    // The module CANNOT identify its caller: ModuleProxy::callRemoteMethod drops the auth token
    // before dispatch, its isAuthorized() is a bearer check that accepts any token this module
    // ever issued, and Basecamp calls logos_core_set_access_policy(nullptr), which logos core
    // documents as fail-open. So a caller allowlist is not implementable, and no token scheme
    // helps either: a hostile co-resident module can request one exactly like the real UI does.
    // The one thing it cannot obtain is the USER'S PASSWORD. Hence every verb that exports a
    // secret, moves funds or changes how the wallet executes takes the session password and
    // verifies it here. Modules are separate processes, so medusa_ui holds the password the user
    // typed at unlock and re-presents it: the user still types it ONCE per session.
    // THE GATE IS A FUNCTION OF THE SECRET ALONE: a session exists, and the presented password
    // equals it in constant time. It consults no file, no setting and no other state a co-resident
    // attacker at this uid can write - a `storageIsPlaintext()` short-circuit used to sit in front
    // of both checks and was defeated by re-wrapping the still-encrypted store so its markers fell
    // past the 256 bytes that predicate reads (see the body for the full account).
    // Fails closed while locked (m_password empty = nothing to prove against).
    //
    // WHO IS SERVED, state by state, so "the gate is strict" can never again quietly mean "the
    // owner has no working button" (that was round 2's failure):
    //   • encrypted + unlocked  -> the gate passes with the password the UI holds. Everything.
    //   • encrypted + locked    -> unlock() (ungated), then as above.
    //   • legacy PLAINTEXT      -> the gate refuses, with reason "unencrypted" and the route out
    //                              in the message. listAccounts/balances/zones/faucet keep working
    //                              (ungated), and encryptPlaintextWallet - which needs no session,
    //                              and is safe to leave ungated precisely because a plaintext
    //                              store's keys are already readable at this uid - migrates the
    //                              wallet, keeping its accounts and copying the old store aside.
    //                              After the migration a normal unlock re-opens everything.
    //   • forgot the password   -> resetWallet / restoreWallet, ungated while locked.
    // Not const: a successful gate is what re-arms the idle auto-lock, since it is the only
    // signal the module has that the USER (rather than the UI's 10s status poll) is doing
    // something.
    bool authorize(const QString& password);
    // The refusal a gated verb returns: the module's usual {"error"} shape plus a machine
    // readable "reason" so the UI can tell "locked" (prompt for unlock) from "unauthorized"
    // (wrong password) without string-matching.
    QString authRefusal() const;
    // Compare two secrets without leaking their contents through timing. Both sides are hashed
    // first and the fixed-width digests are compared with a branch-free accumulator, so neither
    // the length nor the first differing byte is observable. This is the one check standing
    // between a hostile module and the seed phrase, so it must not be operator==.
    static bool constantTimeEquals(const QString& a, const QString& b);
    // Can a password be proved against the store ON DISK at all? A fact about the filesystem,
    // which no caller can flip - unlike "is a session live", which round 2's recovery verbs keyed
    // off and which clearSessionPassword() flips in one ungated call.
    static bool storeCanProvePassword();
    // ── THE ONE WRITER OF m_password (invariant A) ────────────────────────────────
    // Verify `candidate` by opening the store on disk with the wallet CLI, and keep it as the
    // session password only if that succeeded. Returns the CLI's account list on success, or an
    // {error,reason} refusal (no-wallet / unencrypted / rate-limited / unauthorized). Carries
    // unlock()'s backoff counters, so every verb that needs a password checked against the store
    // shares one rate limit instead of becoming a second, unthrottled oracle.
    QString establishSession(const QString& candidate);

    // ── THE ONE LAUNCH POINT (invariant B2) ───────────────────────────────────────
    // Every QProcess in this module is started here, and this REFUSES any program that is not an
    // absolute path. A bare name is resolved by QProcess against the inherited $PATH, which on an
    // ordinary desktop begins with ~/.local/bin - writable by the co-resident module this whole
    // design exists to defend against. `curl`, `bash`, `python3` and `which` were all launched
    // that way, and the 10-second status poll fired the first one with no user present.
    // Returns false (and logs) without starting anything when the program is not absolute.
    bool startChild(QProcess& p, const QString& program, const QStringList& args);
    // The environment children get: the inherited one with every $PATH entry writable at this uid
    // removed. Children resolve their OWN helpers by name (the wallet CLI is a
    // `#!/usr/bin/env python3` script), so the sanitising has to follow them.
    static QProcessEnvironment childEnv();

    QString runWalletCommand(const QStringList& args, int timeoutMs = 30000);
    // Run the CLI feeding `stdinData` (the session password, plus the mnemonic line
    // for restore, or the signing-key line for import) to its stdin; reads stdout for the
    // result and stderr for errors.
    QString runWalletCommandInput(const QStringList& args, const QString& stdinData,
                                  int timeoutMs = 30000);
    static QString cleanStderr(const QString& raw);
    QString cliPath() const;
    // argv rendered for the log with the value after any known-secret flag replaced. Defence in
    // depth: no secret should reach argv at all, and none does today.
    static QString redactedArgs(const QStringList& args);
    // Does this command's stdout/stderr carry key material (export-mnemonic, export-key,
    // import, restore-keys)? Such output must never be echoed into the log buffer.
    static bool outputIsSecret(const QStringList& args);
    // The wallet home dir the CLI wrapper uses (LEE_WALLET_HOME_DIR, rc4 NSSA_ fallback, else default).
    static QString walletHome();
    // <walletHome>/storage.json - the store the CLI reads and writes.
    static QString storagePath();
    // Is there a wallet store on disk at all?
    static bool storageExists();
    // Is the store on disk a LEGACY PLAINTEXT one (it exists and carries no crypto envelope)?
    // Read from the file header, the same way getWalletState reports `encrypted`. This is the
    // one state in which the proof-of-user gate cannot work at all, so every entry point that
    // would establish or accept a session password consults it.
    static bool storageIsPlaintext();
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
    // ── Local-sequencer failure record (drives getSequencerStatus's reason field) ──
    // A user op against a dead local sequencer must be able to say WHY it is dead
    // (binary missing / spawn failed / crashed) instead of an eternal "Connecting…".
    QString        m_seqLaunchError;          // last spawn failure ("" = launched fine / not tried)
    bool           m_seqExited = false;       // the spawned sequencer exited on its own
    int            m_seqExitCode = 0;         // its exit code (-1 = killed by a signal)
    bool           m_seqStopping = false;     // deliberate stopSequencer() in flight - not a crash
    qint64         m_seqLaunchedMs = 0;       // epoch ms of the last successful spawn (grace period)
    qint64         m_bornMs = 0;              // plugin construction time (grace before first launch)
    // ── Zone/build compatibility ("unknown" | "ok" | "mismatch") ──
    // Probed asynchronously via `wallet check-health` once the zone answers health checks;
    // reset on every zone (re)apply. A stale bundled sequencer under a newer wallet ANSWERS
    // health checks, so without this the UI reads an unusable zone as connected.
    QProcess*      m_compatProbe = nullptr;
    QString        m_zoneCompat;
    void           probeZoneCompatAsync();
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
    // Resolve the sequencer binary the same way cliPath() resolves the wallet CLI: a
    // launcher-owned env var (MEDUSA_SEQ_PATH) else the module's own bundle. NOT QSettings.
    QString        seqPath() const;
    QString        seqHome() const;            // <walletHome>/sequencer (db + config)
    void           ensureSequencer();          // launch iff local mode + not already reachable
    void           stopSequencer();            // terminate/kill/wait the child
    void           writeSeqConfig(const QString& cfgPath) const;   // seed the standalone config
    // Not static any more: the probe runs curl through startChild(), which is the one place a
    // program may be launched from, and logs when curl is missing instead of guessing.
    bool           seqHealthy(int port, int timeoutMs = 1000);     // checkHealth on 127.0.0.1:port
    bool           seqHealthyUrl(const QString& url, int timeoutMs = 1000);  // checkHealth on a full URL
    // curl, resolved to an absolute path in a root-owned system dir ("" when not installed).
    static QString curlPath();
    QString        netId() const;             // active network id ("devnet"|"testnet")
    int            netPort() const;           // local sequencer port for the active network
    QString        applySequencer();          // recompute wallet_config.json addr + (re)launch/stop; returns effective addr

    // In-memory session password ("" = legacy plaintext wallet). Never persisted.
    QString m_password;

    // ── Idle auto-lock ─────────────────────────────────────────────────────────
    // Without this the decrypted session lasts from the user's first unlock until the module
    // process exits - days on a desktop - which is what turns "the wallet must be unlocked" into
    // a cost the attacker pays by waiting. A passed gate, an unlock and a password change restart
    // the timer; a running job counts as activity so a 40-minute proof never locks under itself.
    // A background status poll deliberately does NOT, or the lock would never fire. Armed by
    // authorize(), unlock(), the two wallet-sealing verbs and a proven restoreWallet.
    QTimer* m_idleLock = nullptr;
    qint64  m_lastActivityMs = 0;
    void    touchActivity();      // (re)arm the idle timer - call on real wallet activity
    // 15 minutes: the same budget this file already picked for an unattended approval
    // (kReqTtlMs), long enough to app-switch and come back, short enough that a walk-away
    // laptop is not left holding the seed.
    static constexpr int kIdleLockMs = 15 * 60 * 1000;
    // The effective budget: kIdleLockMs unless MEDUSA_IDLE_LOCK_MS overrides it (same env-knob
    // idiom as proveTimeoutMs). The environment belongs to whoever launched the process, not to
    // a co-resident module, so this is not a way to disable the lock from outside.
    static int idleLockMs();

    // ── unlock() throttle ──────────────────────────────────────────────────────
    // unlock() is otherwise an unrate-limited password oracle. Consecutive failures earn an
    // exponential backoff; a success resets it.
    int    m_unlockFails = 0;
    qint64 m_unlockRetryAtMs = 0;
    static constexpr int kUnlockFreeAttempts = 3;        // no delay for honest typos
    static constexpr int kUnlockBackoffBaseMs = 1000;    // then 1s, 2s, 4s, …
    static constexpr int kUnlockBackoffMaxMs = 60000;    // … capped at a minute

    // Normalise raw CLI output (merged stdout/stderr) + exit code into the module's
    // JSON contract: pass through JSON as-is, otherwise wrap text in {ok,output} /
    // {error}. Shared by the synchronous and asynchronous code paths.
    static QString normalizeCliOutput(const QString& rawOut, int exitCode);
    // `reason` is an optional machine-readable code ("locked", "unauthorized", "rate-limited")
    // added alongside the human message; omitted, the shape is the historical {"error"}.
    static QString errorJson(const QString& msg, const QString& reason = QString());
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
