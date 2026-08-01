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
    QString version() const override { return QStringLiteral("0.3.0"); }

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

    // ══ THE BRIDGE MARSHALS AT MOST 5 ARGUMENTS. NOTHING BELOW MAY EXCEED THAT. ═══════════
    //
    // Every remote call from QML reaches this class through the Logos bridge, and the bridge
    // dispatches with a switch over the argument COUNT - logos-liblogos,
    // cpp/qt_provider_object.cpp:20-34:
    //
    //     switch (args.size()) {
    //       case 0: … case 5: return QMetaObject::invokeMethod(…);
    //       default: qWarning() << "QtProviderObject: Currently supports 0-5 arguments. Got:"
    //                           << args.size();
    //     }
    //
    // A verb with 6 or more parameters is therefore NEVER INVOKED. Nothing throws, nothing the
    // user can see is logged, and LogosQmlBridge.cpp:76 turns the absent result into
    // {"error":"Invalid response"} - indistinguishable from a wallet failure. That is exactly
    // how three verbs shipped dead: startShield(6) and startDeshield(6), which the fifth
    // security round pushed over the edge by appending the session password, and
    // startPrivateTransferForeign(7), which had been over it from the day it was written and
    // was never once exercised through the real bridge.
    //
    // HOUSE RULE: FOUR arguments, not five. 5 is the ceiling itself, and a gated verb is one
    // security round away from needing one more argument - which is the move that killed
    // shield and deshield. Everything below is at 4 or fewer. If you are about to write a
    // fifth, collapse two arguments that are really one concept instead (see the spec strings
    // below). tests/test_wallet_plugin.cpp enforces BOTH numbers off the metaobject
    // (testEveryInvokableFitsTheBridgeCeiling, testEveryInvokableKeepsBridgeHeadroom), so this
    // cannot be rediscovered live a third time.
    //
    // THE TWO SPEC STRINGS that buy the headroom back. Both take the simple form verbatim and
    // the compound form as a JSON object, told apart by a leading '{' (an amount never starts
    // with one, nor does an account id). JSON is already this module's idiom for a compound
    // argument - connectRequest(appJson), requestAction(actionJson), requestZone(zoneJson) -
    // and it stays unambiguous whatever a user types into a field. One parser each,
    // parseValueSpec()/parseRecipientSpec(), so every verb reports the same errors.
    //
    //   value      = HOW MUCH OF WHAT, one concept (an amount is meaningless without its
    //                asset), replacing the old (asset, amount) or (asset, definitionId, amount):
    //                  "12"                                              → 12 native LEZ
    //                  {"amount":"12"}                                   → the same
    //                  {"asset":"token","definitionId":"<id>","amount":"12"}
    //   recipient  = WHO RECEIVES, one concept, replacing the old
    //                (to) or (toNpk, toVpk, toIdentifier):
    //                  "Private/abc…"                                    → an owned account
    //                  {"npk":"…","vpk":"…","identifier":"…"}            → a foreign private
    //                                                                      recipient
    //
    // AND THE PASSWORD STAYS LAST. medusa_ui's callGated() appends the session password
    // positionally as the final argument (Main.qml, "INVARIANT 1"), mirroring the authorize()
    // sites here. Arguments are shed BEFORE it; nothing is ever inserted after it.

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
    // {state,mode,port,endpoint,healthy,compat} - state = "running" | "starting" |
    // "unreachable". "starting" means a connect is genuinely IN FLIGHT, which on a Tor zone
    // needs BOTH the bundled Tor and the forward alive (the forward is a separate process that
    // outlives Tor and dials lazily, so on its own it kept reporting an eternal "Connecting…").
    // A local zone adds binaryAvailable/running/lastLaunchError/exitCode/logPath, a Tor zone
    // adds needsTor/torAvailable/torPath/torRunning/forwardRunning, and BOTH add the one
    // machine-readable `reason` the UI's banner and offline modal render:
    //   local: "" | binary-missing | launch-failed | exited | unhealthy | mismatch
    //   Tor:   "" | aborted | tor-missing | tor-failed | tor-exited | mismatch
    // A Tor zone deliberately has no time-based "unhealthy": a bootstrap legitimately takes
    // minutes, and calling a slow connect a failure is how the UI learned to lie. After a
    // cancelConnect() the reply also carries connectAborted/abortedZone, whatever zone the
    // wallet has landed on, so "it went quiet" and "the user stopped it" are never confused.
    Q_INVOKABLE QString getSequencerStatus();
    // Bundled-Tor bootstrap progress (for the connect progress bar on Tor zones):
    // {percent, stage}. Parsed from the bundled Tor's log.
    Q_INVOKABLE QString getTorProgress() const;
    // ABORT a connect in flight - the escape hatch from a Tor bootstrap that is taking minutes
    // or has failed. Stops the bundled Tor + its onion-stage monitor + the diaphani-forward
    // tunnel, and, when the zone it abandons is Tor-fronted, leaves the wallet on the default
    // clearnet zone instead of half-switched (see the .cpp for why staying put is not a resting
    // state). Idempotent, and UNGATED by design - the reasoning is at the definition.
    // {ok,zone,abortedZone,switched,wasConnecting,torStopped,torForeign,forwardStopped}.
    Q_INVOKABLE QString cancelConnect();
    // The active zone id (back-compat alias of getZones' active): {network}.
    Q_INVOKABLE QString getNetwork() const;
    // Switch the active zone (alias of setActiveZone). {ok}.
    Q_INVOKABLE QString setNetwork(const QString& network);

    // ── Zones (LEZ chains the wallet can switch between, token-agnostic) ───────
    // {zones:[{id,name,kind,endpoint,tor,builtin}], active}. kind = local-standalone
    // (devnet) | local-l1-tor (diaphani) | remote (a thin client of someone's sequencer).
    Q_INVOKABLE QString getZones() const;
    // Add a REMOTE zone (thin client). ONE endpoint argument: a clearnet URL, or the
    // sequencer's .onion address with tor=true. {ok,id}.
    // The old (url, onion, tor) triple was one concept written twice - exactly one of the two
    // strings was ever non-empty, `tor` said which, getZones() has always published the pair
    // back as a single "endpoint" field, and the UI has always had a single endpoint box that
    // it split at the call site. Collapsing it takes editZone off the 5-argument ceiling and
    // removes the "tor=true with a clearnet url" state that had to be validated away.
    Q_INVOKABLE QString addZone(const QString& name, const QString& endpoint, bool tor);
    // Edit a user zone's name/endpoint/transport (built-ins can't be edited). {ok}.
    Q_INVOKABLE QString editZone(const QString& id, const QString& name,
                                 const QString& endpoint, bool tor);
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
    // The curated tokens of the ACTIVE zone: [{name, def}], empty on a zone with no token
    // faucet. Read from the zone table (kFaucetZones), NOT from operator state on disk: the
    // old source was a file under ~/.local/share/medusa-treasury that vanished with the
    // treasury directory, which silently emptied this list and the Add-token picker with it.
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

    // ── THE FAUCET: ONE BUTTON, TWO HALVES, TWO VERBS ───────────────────────────────────────
    //
    // The ⛲ button claims native LEZ AND, on a zone that has one, the whitelist tokens. Those
    // are two different programs with two different trust levels, so they stay two verbs and the
    // UI composes them; a single verb would have to be gated, and gating the LEZ half would take
    // the faucet away from exactly the wallets that most need it (locked, or legacy plaintext -
    // see invariant C).
    //   • startFaucet      = `pinata claim`. Native LEZ, UNGATED. It spends nothing of the
    //                        user's: the pinata program credits an account, it does not move
    //                        value out of one, so there is no signature to authorise. Fully
    //                        on-chain and needing no local state, which is why it kept working
    //                        while the token half was broken.
    //   • startTokenFaucet = the deployed `medusa_faucet` program. Whitelist tokens only, from
    //                        per-definition treasury PDAs the program owns, with an ON-CHAIN 6h
    //                        cooldown. GATED: it signs and broadcasts with the wallet's keys.
    //
    // ORDER. pinata is started first and the token half is QUEUED BEHIND IT (startTokenFaucet
    // does this; the caller does not have to know). `pinata claim` may run `auth-transfer init`
    // for the recipient first, because the pinata program credits WITHOUT claiming and the chain
    // rejects a credit to an account it has no record of, and the two halves drive the same
    // wallet store from separate processes. Sequencing them keeps one writer on storage.json.
    //
    // PARTIAL SUCCESS IS THE NORMAL CASE, not an edge case: the two cooldowns are independent, so
    // "150 LEZ arrived, tokens are on cooldown for 214 minutes" is the steady state after any
    // second press. Each half therefore reports its own terminal job with its own `state`,
    // `error` and machine-readable `reason` ("cooldown" | "not-funded" | "not-initialized" |
    // "no-holding" | "unreachable"), and a failure of one never cancels or suppresses the other.
    //
    // Asynchronous: returns {jobId,state}, poll getJob(jobId). The claim submits a tx and waits
    // for a block, so it must not block the module RPC.
    Q_INVOKABLE QString startFaucet(const QString& accountId);
    // Faucet (synchronous, legacy/unused - superseded by startFaucet).
    Q_INVOKABLE QString claimFaucet(const QString& accountId);

    // ── The ON-CHAIN token faucet (the deployed `medusa_faucet` LEZ program) ─────
    //
    // THE TOKEN HALF IS A PER-ZONE CAPABILITY, and it is a property of the ZONE RECORD, not of
    // the wallet: kFaucetZones in the .cpp lists the zones that have the program plus the
    // definitions and treasury PDAs it dispenses there, getZones()/zoneObj() publish it as
    // "tokenFaucet", and zoneHasTokenFaucet() is the one predicate everything asks. A zone with
    // no row (the local devnet sandbox, the Tor zone, and EVERY user-added zone) is LEZ-only:
    // the program is not deployed on somebody else's sequencer, and these definitions are
    // accounts on our chains that do not exist on theirs. The capability is derived from the
    // table alone, never read back off a stored zone record, so it cannot be planted.
    //
    // Read-only, ungated (it moves nothing and reveals nothing the UI does not already show):
    // {programId, available, reason, message, client, clientFound, bin, binFound, verified,
    //  zone, tokenFaucetZone, definitions[], treasuries[], tickers{}, recipients[], funded}.
    // `reason` is a machine code the UI switches on: "" (available) | "unsupported-zone" |
    // "client-missing" | "bin-missing" | "program-mismatch".
    //
    // NOTE what is no longer a reason. "no-definitions" is gone: the definitions are in the zone
    // table, so a zone either has them or has no token faucet at all. "no-holding" is gone as a
    // STATUS: a wallet with no holding account for a token is not blocked, the claim path mints
    // one (see ensureFaucetRecipients), which is what a freshly reset wallet needs.
    Q_INVOKABLE QString faucetStatus();
    // Claim from the on-chain faucet. GATED (authorize()): it signs and broadcasts a
    // transaction with the wallet's keys, so it takes the session password as its TRAILING
    // argument exactly like every other verb that spends. Asynchronous: returns {jobId,state},
    // poll getJob(jobId). `accountId` is the account the claim is ATTRIBUTED to in local
    // history; the on-chain recipients are the wallet's per-definition holdings, never the
    // account itself (one account can hold exactly one token definition, so spending the
    // user's main account as a recipient would bind it to one token forever).
    Q_INVOKABLE QString startTokenFaucet(const QString& accountId, const QString& password);

    // ── Transfer ────────────────────────────────────────────────────────────────
    // Every verb below is GATED (authorize()): the trailing `password` is the session password
    // the user typed at unlock, re-presented by the caller on each spend.
    //
    // ONE ARGUMENT ORDER for everything that moves value, so no call site can transpose two of
    // them: (from, to, value, password). `value` is a value spec (see the argument-ceiling
    // block at the top of the invokable section); on the privacy verbs `to` is a recipient
    // spec.
    Q_INVOKABLE QString sendTransfer(const QString& from,
                                     const QString& to,
                                     const QString& value,
                                     const QString& password);
    // Async native send (the main Send screen). Runs as a background job because the dest may
    // be a Private account → a multi-minute real proof that must NOT block the module RPC
    // (blocking sendTransfer timed out / froze the UI). Returns {jobId}; track like a privacy op.
    // Native only: a token value spec is refused here and named to startSendToken, rather than
    // being handed to the auth-transfer program which would move LEZ instead of the token.
    Q_INVOKABLE QString startSendTransfer(const QString& from,
                                          const QString& to,
                                          const QString& value,
                                          const QString& password);
    // Send a token (asynchronous - derives/creates ATAs + token-send + waits, ~30-40s, so
    // it must not block the module RPC). Returns {jobId,state}; poll getJob(jobId).
    // `value` must be a TOKEN spec: {"asset":"token","definitionId":"<id>","amount":"<n>"}.
    Q_INVOKABLE QString startSendToken(const QString& from, const QString& to,
                                       const QString& value, const QString& password);

    // ── Privacy transfers (asynchronous - generate a local STARK, may take minutes) ──
    // Each "start*" returns {jobId,state} immediately; poll getJob(jobId) for progress.
    // The asset ("native" = auth-transfer program, "token" = token program) rides in the
    // value spec, because an amount without its asset is not a quantity of anything.

    // Public -> Private (shield): from must be Public/…, to must be Private/… (owned).
    // A token value REQUIRES its definitionId: the wrapper resolves a direct-owned holding of
    // that definition as the signing source (an ATA is a PDA and cannot sign - rc5 limit).
    // The defaulted `definitionId`/`password` parameters this verb used to carry are gone with
    // the argument they were working around: there is no short-arity form to fail closed any
    // more, because there is no sixth argument to push the password off the end.
    Q_INVOKABLE QString startShield(const QString& from, const QString& to,
                                    const QString& value, const QString& password);
    // Private -> Public (deshield): from must be Private/…, to must be Public/…
    // A token value REQUIRES its definitionId: the wrapper lands the tokens in the recipient
    // owner's ATA (created idempotently), the only valid public token destination.
    Q_INVOKABLE QString startDeshield(const QString& from, const QString& to,
                                      const QString& value, const QString& password);
    // Private -> Private. `to` is a RECIPIENT SPEC, which is what folded the old
    // startPrivateTransferForeign(7) back in here rather than leaving it at an arity the
    // bridge cannot dispatch:
    //   "Private/…"                              → an owned private account (both ends owned)
    //   {"npk":…,"vpk":…,"identifier":…}         → a foreign recipient's shared keys, sent as
    //                                              --to-npk/--to-vpk/--to-identifier
    // A compatibility stub for the deleted verb would have been unreachable by construction:
    // at 7 parameters the bridge could never have called it to deliver the explanation.
    Q_INVOKABLE QString startPrivateTransfer(const QString& from, const QString& to,
                                             const QString& value, const QString& password);
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

    // ── On-chain faucet preflight ────────────────────────────────────────────────
    // ONE function answers "can the on-chain faucet do anything here", so faucetStatus()
    // (what the UI renders) and startTokenFaucet() (what actually runs) can never disagree
    // about availability - the shape of bug that ships a button which always fails.
    struct FaucetPreflight {
        bool        ok = false;
        QString     reason;        // machine code, "" when ok
        QString     message;       // the sentence the user reads
        QString     zone;          // the active zone this preflight is about
        QString     client;        // resolved medusa-faucet-client path ("" = not found)
        QString     bin;           // resolved guest .bin path ("" = not found)
        bool        verified = false;   // the .bin's ImageID == kFaucetProgramId
        QStringList definitions;   // this zone's faucet token definitions (from kFaucetZones)
        QStringList treasuries;    // their treasury PDAs, same order (from kFaucetZones)
        QStringList tickers;       // display names, same order as definitions
        QStringList recipients;    // this wallet's holding per definition, same order; an entry
                                   // is "" when there is none YET, which is not a failure
        QStringList provisioned;   // holdings ensureFaucetRecipients() had to create
    };
    FaucetPreflight faucetPreflight();
    // Does this zone have the on-chain token faucet? Decided by kFaucetZones alone (see the
    // table in the .cpp), so the zone list, the status call and the claim can never disagree,
    // and a stored zone record can never grant itself the capability.
    bool zoneHasTokenFaucet(const QString& zoneId) const;
    // CLAIM PATH ONLY. Give every definition a recipient this wallet owns, creating and
    // recording one where the registry has none. Returns "" on success, else the refusal.
    // Never called from faucetStatus(): a read-only status call must not mint accounts.
    QString ensureFaucetRecipients(FaucetPreflight& pf);
    // Resolve the deployed program's guest .bin (MEDUSA_FAUCET_BIN, then the module bundle,
    // then a dev install). "" when no readable file is there.
    static QString faucetGuestBin();

    // ── Local sequencer lifecycle (plugin-owned standalone process) ───────────
    QProcess*      m_seqProc = nullptr;       // the bundled standalone sequencer (local mode)
    QProcess*      m_fwdProc = nullptr;       // diaphani-forward (Tor tunnel) - Tor zones
    void           ensureForward(int listenPort, const QString& onion);  // Tor tunnel to an .onion
    void           stopForward();             // terminate/kill/wait the forward
    QProcess*      m_torProc = nullptr;       // bundled Tor (private SOCKS) - no external Tor needed
    QProcess*      m_torMonProc = nullptr;    // tor-control monitor → real onion-connection stages
    void           ensureTor();               // launch the bundled Tor (idempotent, non-blocking)
    void           stopTor();                 // terminate/kill/wait Tor (+ its monitor)
    // The loopback ports the bundled Tor listens on. kTorSocksPort/kTorControlPort unless the
    // LAUNCHER's environment overrides them - see the definitions in the .cpp for why an env
    // knob here is not a hole in invariant B and what it buys.
    static int     torSocksPort();
    static int     torControlPort();
    // ── Tor bring-up record (drives the Tor half of getSequencerStatus's reason field) ──
    // The local sequencer has had this since "the zone just sat in a silent, eternal
    // Connecting…" was a bug; a Tor zone had nothing at all, so a Tor that never launched, a Tor
    // that died and a Tor that is merely slow were one indistinguishable "starting" forever.
    bool           m_torAdopted = false;      // we REUSED a Tor already on the SOCKS port - not our child
    bool           m_torStopping = false;     // deliberate stopTor() in flight - not a crash
    QString        m_torLaunchError;          // last Tor spawn failure ("" = launched fine / not tried)
    bool           m_torExited = false;       // the bundled Tor exited on its own
    int            m_torExitCode = 0;         // its exit code (-1 = killed by a signal)
    // Is the Tor this wallet's tunnel depends on actually up? An ADOPTED Tor is not our child,
    // so the only honest test for it is whether its SOCKS port still answers.
    bool           torRunning();
    // ── Aborted-connect record (cancelConnect) ──
    // Cleared by the next applySequencer(), i.e. it means "the most recent connect ended because
    // the user stopped it, and nothing has been re-applied since".
    bool           m_connectAborted = false;
    QString        m_abortedZone;             // the zone id that connect was abandoned on
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
    // Does this zone reach its sequencer over Tor? Read off the zone RECORD (kind + tor), never
    // off "is it built in" - a user-added zone is a Tor zone whenever addZone was given an
    // .onion. THE one predicate: the status call, the teardown in applySequencer() and
    // cancelConnect() must never disagree about which zones need the bundled Tor.
    bool           zoneUsesTor(const QString& id) const;
    // The zone a fresh install starts on, and the zone an aborted connect falls back to. One
    // definition so netId()'s default and cancelConnect()'s landing zone cannot drift apart.
    static QString defaultZoneId();
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

    // ── The two spec parsers (the encoding is documented at the top of this header) ──────
    // These exist to keep every value-moving verb at 4 arguments under a bridge that
    // dispatches at most 5. Both are TOTAL: they either fill every out-parameter or return
    // false with a sentence in *error, so a verb never half-parses a spend.
    //
    // "12" | {"amount":"12"} | {"asset":"token","definitionId":"<id>","amount":"12"}
    // → *asset is "native" or "token" and nothing else (an unrecognised asset is an ERROR,
    //   never a silent fall-back to native: "toekn" quietly spending LEZ instead of a token
    //   is a money bug, and assetProgram()'s permissive mapping is only safe downstream of
    //   this check). *amount is digits (optionally with a decimal part - the chain rejects
    //   fractional units, but rejecting them HERE would be a new refusal); that check is also
    //   what makes a stale caller which still passes (asset, from, to, amount) fail loudly on
    //   the misplaced argument instead of forwarding it to the CLI.
    static bool parseValueSpec(const QString& spec, QString* asset, QString* definitionId,
                               QString* amount, QString* error);
    // "Private/abc…" (owned) | {"npk":…,"vpk":…,"identifier":…} (foreign private recipient).
    // Exactly one of *accountId or the (npk,vpk,identifier) triple comes back non-empty.
    static bool parseRecipientSpec(const QString& spec, QString* accountId, QString* npk,
                                   QString* vpk, QString* identifier, QString* error);
    // Validate + split a zone endpoint into the (url, onion) pair the zone record stores.
    // tor=true → the endpoint must be a .onion; tor=false → a http(s) URL, normalised the way
    // addZone has always normalised it (a missing scheme defaults to http://).
    static bool normalizeZoneEndpoint(const QString& endpoint, bool tor, QString* url,
                                      QString* onion, QString* error);
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
        // ── Queued start (only the faucet's token half uses this) ──────────────────────
        // The job is registered and reported "running" from the moment it is created; the
        // CHILD is held back until `waitFor` ends, then launched with these. Empty waitFor =
        // started immediately, which is every other job.
        QString        waitFor;       // jobId this one is sequenced behind
        QString        pendingBin;    // the child to launch when that job ends
        QStringList    pendingArgs;
        bool           pendingOwnBin = false;   // the child is not the wallet CLI
    };

    // Non-empty error message if a running shield/private job already targets this
    // private destination (double-booking a fresh account would waste the second proof).
    QString privateDestInFlight(const QString& toP) const;
    // Build args + spawn a privacy "send" as a background job; returns {jobId,state}.
    // `binOverride` names a DIFFERENT child to run under the same job machinery (the on-chain
    // faucet's medusa-faucet-client); empty = the wallet CLI, which is every other caller.
    // `waitForJob`, when it names a job that is still running, sequences this one behind it:
    // the job is created and polled normally, but its child is not launched until that job
    // finishes (or the queue cap below expires). Used to keep the faucet's two halves off the
    // wallet store at the same time; see startTokenFaucet.
    QString startPrivacyJob(const QString& op, const QString& asset,
                            const QStringList& sendArgs,
                            const QString& from, const QString& to, const QString& amount,
                            const QString& binOverride = QString(),
                            const QString& waitForJob = QString());
    // Create, wire and launch one job's child. The single place a job process is built, so a
    // queued start cannot drift from an immediate one.
    void    startJobProcess(Job* j, const QString& bin, const QStringList& args, bool ownBin);
    // Release any job queued behind `jobId`. Called from BOTH places a job becomes terminal:
    // onJobFinished, and the FailedToStart handler (after which finished() is never emitted).
    void    startQueuedBehind(const QString& jobId);
    void    onJobFinished(const QString& jobId, int exitCode);

    QHash<QString, Job*> m_jobs;
    int  m_jobSeq = 0;
    static constexpr int kMaxJobs        = 24;
    // How long a queued job waits for the one ahead of it before starting anyway. The wait is
    // an ordering convenience, not a dependency, so it is capped: the worst realistic `pinata
    // claim` is the wrapper's own budget (180s recipient init + 240s claim), and starting a
    // little late is recoverable where never starting is not.
    static constexpr int kQueuedStartMaxMs = 8 * 60 * 1000;
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
