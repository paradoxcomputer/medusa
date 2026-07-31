import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQuick.Effects

Rectangle {
    id: root
    color: "#0F0F0F"   // BG_DARK - see design tokens below (root literal must be self-contained)
    anchors.fill: parent

    // Bundled brand fonts (loaded from ../fonts relative to this Main.qml). The face/mono
    // tokens below bind to the loaded family names, falling back to the system family.
    FontLoader { id: interFont; source: "fonts/InterVariable.ttf" }
    FontLoader { id: jbmFont;   source: "fonts/JetBrainsMono-Regular.ttf" }

    // ════════════════════════════════════════════════════════════════════════════
    //  DESIGN SYSTEM - "Silver Sentinel Noir"
    //  Premium privacy-first dark theme: near-black surfaces, a single confident
    //  metallic SILVER accent, restrained semantic colour, purposeful motion.
    //  NOTE: existing property NAMES are preserved so the whole file re-themes for
    //  free; the SILVER design accent is mapped onto `accentOrange` (the file's
    //  primary-CTA token) so every CTA, active border and focus ring becomes silver
    //  without touching any of the ~200 call sites. New tokens are ADDED below.
    // ════════════════════════════════════════════════════════════════════════════
    readonly property string faceFont:     interFont.name || "Inter"     // primary sans (graceful Qt fallback)
    readonly property string monoFont:     jbmFont.name || "JetBrains Mono"  // addresses / seed phrases / keys

    // ── Surfaces (near-black, layered elevation) ──
    readonly property color bgColor:       "#0F0F0F"   // BG_DARK   - near-black canvas
    readonly property color panelColor:    "#1A1A1A"   // BG_SURFACE- elevated card surface
    readonly property color surface2:      "#242424"   // BG_CARD   - higher elevation / hover
    readonly property color surface3:      "#2D2D2D"   // top-most elevation (sheets, pills)
    readonly property color borderColor:   "#2E2E2E"   // hairline border
    readonly property color borderStrong:  "#3A3A3A"   // emphasised hairline
    readonly property color inputBg:       "#2D2D2D"   // BG_INPUT  - recessed field
    readonly property color selectBg:      "#242424"   // selection / chip base

    // ── Text (WCAG-AAA-leaning on near-black) ──
    readonly property color textPrimary:   "#FAFAFA"   // high-contrast body
    readonly property color textSecondary: "#B8B8B8"   // secondary labels (~7:1)
    readonly property color textDisabled:  "#808080"   // tertiary / muted (≥3:1 large)

    // ── Silver accent system (the brand) ──
    readonly property color silver:        "#C4C4C4"   // ACCENT_PRIMARY - chips, rims, native asset
    readonly property color silverHover:   "#D9D9D9"   // ACCENT_HOVER
    readonly property color silverPressed: "#A8A8A8"   // ACCENT_PRESSED
    readonly property color silverDim:     "#666666"   // ACCENT_DISABLED

    // ── Semantic ──
    readonly property color successGreen:  "#3E9E5B"   // confirmed / private-enabled (base)
    readonly property color greenBright:   "#4FD869"   // SUCCESS - status dots / live
    readonly property color connectGray:   "#808080"   // "connecting" - neutral gray
    readonly property color warningAmber:  "#F5C641"   // WARNING - pending / fees
    readonly property color errorRed:      "#FF6B6B"   // ERROR - deshield / failures
    readonly property color infoBlue:      "#64B8FF"   // INFO - network / balance syncs

    // ── Primary CTA / active accent → SILVER (re-mapped, name kept for the rest of file) ──
    readonly property color accentOrange:  "#C4C4C4"   // primary CTA / active border / focus = silver
    readonly property color accentHover:   "#D9D9D9"   // CTA hover
    readonly property color accentPressed: "#A8A8A8"   // CTA pressed
    readonly property color accentDeep:    "#5A5A5A"   // gradient end / dim accent
    // Tint helpers - translucent accent washes for active fills (silver-tinted)
    readonly property color accentTint10:  Qt.rgba(196/255, 196/255, 196/255, 0.10)
    readonly property color accentTint14:  Qt.rgba(196/255, 196/255, 196/255, 0.14)
    readonly property color accentTint22:  Qt.rgba(196/255, 196/255, 196/255, 0.22)
    readonly property color hoverWash:     Qt.rgba(1, 1, 1, 0.05)
    readonly property color errorTint:     Qt.rgba(255/255, 107/255, 107/255, 0.10)

    // ── Dark-green accent (header key + cog buttons) ──
    readonly property color darkGreen:       "#1A6B3C"   // header key/cog border + glyph (idle)
    readonly property color darkGreenBright: "#2E9E5B"   // header key/cog border + glyph (active screen)

    // ── Crimson accent - detail micro-labels + balance accent + primary CTA fills.
    //    Silver (`silver`/`accentOrange`) stays the primary for chips/rims/active borders;
    //    crimson is targeted at the hero-balance accent and the action CTAs only. ──
    readonly property color brandRed:        "#E0314C"   // crimson - eyebrows / pulse / balance / CTA top
    readonly property color brandRedDeep:    "#7A1020"   // deep crimson - CTA gradient bottom
    readonly property color brandRedHover:   "#F0506A"   // CTA hover
    readonly property color brandRedPressed: "#C02038"   // CTA pressed
    // Translucent crimson washes - for CTA "armed but tinted" fills (mirror the accentTint* silver washes).
    readonly property color brandRedTint10:  Qt.rgba(224/255, 49/255, 76/255, 0.12)
    readonly property color brandRedTint14:  Qt.rgba(224/255, 49/255, 76/255, 0.18)
    readonly property color brandRedTint22:  Qt.rgba(224/255, 49/255, 76/255, 0.28)

    // ── Type scale (8-pt modular) ──
    readonly property int   fsXS:    11    // label / metadata
    readonly property int   fsSM:    12    // form labels / dense body
    readonly property int   fsBase:  14    // body / input
    readonly property int   fsMD:    16    // subsection titles
    readonly property int   fsLG:    18    // section headers
    readonly property int   fsXL:    22    // screen titles
    readonly property int   fs2XL:   30    // hero subtotal
    readonly property int   fs3XL:   44    // hero balance / splash

    // ── Radii ──
    readonly property int   rChip:   12
    readonly property int   rInput:  10
    readonly property int   rCard:   14
    readonly property int   rSheet:  16
    readonly property int   rHero:   22
    readonly property real  rPill:   999   // fully rounded

    // ── Spacing (8-pt grid) ──
    readonly property int   sp1:  4
    readonly property int   sp2:  8
    readonly property int   sp3:  12
    readonly property int   sp4:  16
    readonly property int   sp5:  20
    readonly property int   sp6:  24
    readonly property int   sp8:  32

    // ── Motion ──
    readonly property int   motionQuick:      150   // toggles / state swaps
    readonly property int   motionStandard:   250   // transitions / button effects
    readonly property int   motionDeliberate: 400   // significant changes
    readonly property int   motionSlow:       600   // hero / onboarding entrances

    // ── State ─────────────────────────────────────────────────────────────────
    // Navigation: which full screen is showing. "main" = the wallet home.
    property string screen:              "main"      // main | accounts | security | settings | network | addtoken | send | receive | privacy
    // INVARIANT 4 - A DEGRADED WALLET ALWAYS HAS A VISIBLE WAY OUT.
    // The onboarding/lock screen owns the window whenever the wallet is not ready, which used to
    // make the two panels that can rescue such a user unreachable: Security & Backup (unlock,
    // restore from a recovery phrase, erase and start over) was gated on walletState === "ready",
    // and Settings (which names the wallet binary that has to exist for ANY of this to work) was
    // painted underneath. Their icons in the top bar are always live, so deliberately opening one
    // now hands it the window. The backup screen is excluded: nothing may cover an unwritten
    // recovery phrase.
    readonly property bool escapeScreenOpen: (screen === "security" || screen === "settings")
                                             && walletState !== "backup" && walletState !== "loading"
    property string activeTab:           "tokens"    // tokens | activity
    property string network:             "paradox-clearnet"  // active zone id (default: Paradox Computer · clearnet)
    property var    zones:               []          // [{id,name,kind,endpoint,tor,builtin}]
    property bool   addZoneOpen:         false       // add/edit-zone form in the Zones screen
    property string editingZoneId:       ""          // non-empty → the form edits this zone
    property string renamingAcctId:      ""          // non-empty → that account row is being renamed
    property bool   cliFound:            false
    property string seqMode:             "local"     // local-standalone | local-l1-tor | remote ("local" until the first poll)
    property string seqUrl:              ""          // hosted URL
    property int    seqPort:             3071
    property string seqStatus:           "unknown"   // running | starting | unreachable
    property bool   seqBinaryMissing:    false       // devnet selected but no sequencer binary on disk
    property string seqBinaryPath:       ""          // where the wallet looked for it
    property bool   torBinaryMissing:    false       // Tor/onion zone but no bundled/system Tor found
    property int    torPercent:          0           // bundled-Tor bootstrap % (connect bar)
    property string torStage:            ""           // current Tor bootstrap stage text
    property string torOnionStage:       ""           // onion-connection stage (post-bootstrap, real)
    property int    torOnionPct:         0            // onion-connection coarse % (from control port)
    // ── Zone-offline / local-sequencer failure surface ─────────────────────────
    property string seqReason:           ""          // local-zone problem: "" | binary-missing | launch-failed | exited | unhealthy | mismatch
    property string seqLaunchError:      ""          // human text for a failed sequencer spawn
    property string seqLogPath:          ""          // the local sequencer's log file (if one exists)
    property int    seqExitCode:         0           // exit code of a crashed local sequencer
    property string seqEndpoint:         ""          // the address the wallet actually dials
    property string zoneCompat:          "unknown"   // wallet-vs-zone build: unknown | ok | mismatch
    // The one problem string the banner + offline modal render ("" = none). A build
    // mismatch wins: the zone ANSWERS, so plain "offline" wording would mislead.
    // tor-missing is folded in for the Tor-tunnelled local zone (diaphani).
    readonly property string seqProblem: {
        if (zoneCompat === "mismatch") return "mismatch"
        if (seqMode === "local-standalone") return seqReason
        if (seqMode === "local-l1-tor" && torBinaryMissing) return "tor-missing"
        return ""
    }
    property bool   zoneOfflineOpen:     false       // the blocking zone-offline modal
    property string zoneOfflineOp:       ""          // label of the blocked/failed operation
    property bool   zoneOfflineMismatch: false       // route the modal to the mismatch wording
    property bool   zoneRetryBusy:       false       // a Retry health re-check is in flight
    property string cliPath:             ""
    property var    accounts:            []
    property bool   pollBusy:            false

    property string selectedFromId:      ""
    property string selectedFromType:    ""
    property string selectedFromBalance: ""
    property var    selectedTokens:      []      // [{definitionId,ticker,balance}] of the selected account
    property var    whitelistTokens:     []      // [{name,def}] curated tokens to add
    property string sendTokenDef:        ""      // "" = native LEZ, else token definition id
    property string sendTokenName:       "LEZ"
    property string sendStatus:          ""
    property bool   sendBusy:            false
    property var    txHistory:           []

    property string heroTotal:           "0"     // native LEZ totals shown in the hero card
    property string heroPublicTotal:     "0"
    property string heroPrivateTotal:    "0"
    property bool   deshieldAck:         false   // ack gate for de-anonymizing transfers

    // ── Privacy (shield / deshield) ────────────────────────────────────────────
    property string privAsset:     "native"   // native | token
    property string privTokenDef:  ""         // chosen token definition id (token asset)
    property string privTokenTicker: ""       // display name of the chosen token
    // Shield sources: DIRECT-owned holdings only (rc5: ATAs can't sign a private send).
    property var    shieldableTokens: []      // [{definitionId,ticker,balance,account}]
    // Deshield choices: every known definition (the def only picks the recipient ATA -
    // the private note itself carries the asset).
    property var    registryTokens:   []      // [{definitionId,ticker}]
    // Private accounts already used as a private OUTPUT destination - one-shot on rc5
    // (a second output is rejected on-chain AFTER the full proof), so never offer them.
    property var    usedPrivateDests: []
    property string privTokenName: ""
    property string privMode:      "shield"   // shield | deshield | transfer
    property string privToMode:    "owned"    // owned | foreign  (transfer only)
    property string privToId:      ""         // chosen owned destination account id
    property string privToNpk:     ""
    property string privToVpk:     ""
    property string privToIdent:   ""
    property string privAmount:    ""
    property bool   privBusy:      false      // a start* call is being submitted
    property bool   syncBusy:      false      // sync-private in flight
    property var    privJobs:      []         // tracked privacy jobs (newest first)
    property var    receiveKeys:   null       // {pk|npk,vpk} for the selected account

    property var    publicAccounts:  []
    property var    privateAccounts: []
    // Private accounts still in DEFAULT state (never funded/used). A private OUTPUT to a
    // non-default private account guest-panics the rc5 circuit, so only these are valid
    // shield / private-transfer destinations.
    property var    emptyPrivateAccounts: []

    // Accounts eligible as the destination, given the current mode. Used private
    // destinations are excluded even when they look empty (token notes are invisible
    // to the account list's initialized/balance).
    property var    eligibleTo: {
        var fresh = emptyPrivateAccounts.filter(function(x) { return usedPrivateDests.indexOf(x) < 0 })
        if (privMode === "shield")   return fresh
        if (privMode === "deshield") return publicAccounts
        if (privMode === "transfer") return fresh.filter(function(x) { return x !== selectedFromId })
        return []
    }
    // Is the currently-selected "from" account valid for the current mode?
    property bool   privFromValid: {
        if (selectedFromId.length === 0) return false
        if (privMode === "shield")   return selectedFromType === "public"
        return selectedFromType === "private"   // deshield + transfer
    }

    // ── Security & backup (encrypted storage, import/export) ───────────────────
    property bool   walletLocked:  false      // encrypted store + no/again password
    property bool   resetArmed:    false       // two-tap guard for "erase wallet"
    // Onboarding state machine: loading | new | locked | backup | ready.
    // There is deliberately no "unencrypted" state: an unencrypted store is a REACHABLE wallet and
    // not a locked one (its accounts, balances, faucet, receive, import, restore and erase all
    // work, and there is no password to prompt for), so it routes to "ready" and the fact is
    // carried by storeUnprotected instead. Making it a state was what stranded users in round 2.
    // What "ready" does NOT mean is "every verb works": the core's gate refuses every GATED verb
    // on a plaintext store, so the controls behind those verbs are disabled in that state (see
    // signingBlocked) rather than offered and then refused.
    property string walletState:   "loading"
    property bool   revealMnemonic: false
    property bool   revealKey:      false
    property string exportedMnemonic: ""
    property string exportedKey:      ""
    property string secBusy:          ""      // non-empty = an op label in flight
    // The password the user typed at unlock, kept for this session only (never persisted).
    // medusa_core cannot identify its caller, so it gates every verb that exports a secret or
    // moves funds on proof-of-user: the password re-presented as the trailing argument. Modules
    // are separate processes, so the UI holds it and re-presents it - the user still types it
    // exactly ONCE per session and there is deliberately NO per-transaction prompt.
    property string sessionPw:        ""
    property int    autoLockMs:       0       // core's idle auto-lock budget (getSecurityState)
    property int    idleMs:           0       // ms since the last PRIVILEGED action (not polls)
    property bool   lockWarned:       false   // one-shot "about to auto-lock" notice per session
    property bool   cliPathIgnored:   false   // a stored CLI-path override was disowned on read
    property string cliPathEff:       ""      // the binary the core will actually run
    // The store on disk is NOT encrypted (getSecurityState.unencrypted / protected:false). This is
    // not a lockout - the wallet is reachable and the way out is one ungated button on this very
    // screen - but it is not harmless either: the core's gate (authorize()) refuses EVERY gated
    // verb on a plaintext store, because there is no session password it could compare against and
    // unlock() will not mint one for a store it cannot verify a password against.
    // `protectionWarning` is the core's own wording for the risk.
    property bool   storeUnprotected: false
    property string protectionWarning: ""
    // The consequence of the above, named once so no control has to re-derive it. While this is
    // true every gated verb refuses with reason "unencrypted": sendTransfer, startSendTransfer,
    // startSendToken, startShield, startDeshield, startPrivateTransfer,
    // startPrivateTransferForeign, consolidateToken, approveAction, approveZone, exportMnemonic
    // and exportKey. resetWallet and restoreWallet are NOT in that set (they are ungated while no
    // session is live, which on a plaintext store is always), and every ungated verb - accounts,
    // balances, tokens, faucet, receive keys, importKey, zones - keeps working. Controls that call
    // a blocked verb are disabled and say why, instead of being offered and then refused. Setting
    // a password (encryptPlaintextWallet, ungated) is what clears it.
    readonly property bool signingBlocked: root.storeUnprotected
    property int    plaintextSeen:    0       // consecutive polls reporting an unencrypted store
    // A seal (create / migrate) just succeeded and granted NO session, so the very next screen is
    // the lock screen. Without this it greets a user who created their wallet ten seconds ago
    // with "Welcome back", which reads like something went wrong.
    property bool   freshlySealed:    false
    property var    displacedStores: []       // stores this module moved/copied aside (getWalletState)
    property bool   storeInPlace:    false    // getWalletState.exists - is a storage.json there now?
    property string restoreCandidate: ""      // displaced store the user opened the put-back flow for
    property bool   restoreAsideArmed: false  // two-tap guard for step 1 of that flow

    // ── Helpers ───────────────────────────────────────────────────────────────
    // ── Displaced stores: naming what each file is, and how to put one back ────
    // getWalletState lists every store the module moved aside (Erase, Restore) or copied aside
    // (the migration). Printing the paths was already better than the silence before it, but a
    // user whose wallet was displaced - by their own Erase, by a migration that rolled back, or
    // by a co-resident caller invoking the ungated encryptPlaintextWallet/resetWallet - was shown
    // where their money went and given nothing to do about it. These helpers back the put-back
    // flow in Security & Backup.
    function storeFileName(p) {
        var s = String(p || ""); var i = s.lastIndexOf("/")
        return i >= 0 ? s.slice(i + 1) : s
    }
    function storeDir(p) {
        var s = String(p || ""); var i = s.lastIndexOf("/")
        return i > 0 ? s.slice(0, i) : ""
    }
    // "plain" = the unencrypted copy encryptPlaintextWallet keeps before it seals; "bak" = the
    // store resetWallet/restoreWallet renamed out of the way. The two need different things from
    // the user afterwards, so they are never described with one sentence.
    function displacedKind(p) {
        var n = root.storeFileName(p)
        if (n.indexOf("storage.json.plain-") === 0) return "plain"
        if (n.indexOf("storage.json.bak-") === 0)   return "bak"
        return "other"
    }
    function displacedWhat(p) {
        var k = root.displacedKind(p)
        if (k === "plain")
            return "An UNENCRYPTED copy, kept just before a password was set on this wallet. It "
                 + "needs no password - and anything running as you can read its keys, which is "
                 + "why it should be put back only to rescue it, then given a password."
        if (k === "bak")
            return "The store that was in place when Erase or Restore ran. It opens with whatever "
                 + "password sealed it; if you have that wallet's recovery phrase, restoring from "
                 + "the phrase rebuilds the same accounts and needs no file at all."
        return "A wallet store this module moved aside."
    }
    // The one step Medusa cannot take for the user. medusa_core can move a store aside (it does
    // that itself, and never deletes), but it exposes no verb that puts one back, and a QML module
    // has no filesystem of its own - so this is handed over as an exact command instead of as a
    // button that would always fail. `cp -n` is chosen deliberately: it REFUSES rather than
    // overwrite, so running it can never destroy the store that is in place.
    function restoreCommandFor(p) {
        var dir = root.storeDir(p)
        if (dir.length === 0) return ""
        return "cp -n '" + p + "' '" + dir + "/storage.json'"
    }
    function refreshDisplacedStores() {
        if (typeof logos === "undefined" || !logos.callModule) return
        var st = root.callModuleParse(logos.callModule("medusa_core", "getWalletState", []))
        root.displacedStores = (st && Array.isArray(st.displacedStores)) ? st.displacedStores : []
        root.storeInPlace    = !!(st && st.exists === true)
    }
    // Step 3 of the flow: the file may or may not be there now, so ask rather than assume, and
    // hand the routing back to getWalletState the same way every other state change does.
    function recheckAfterRestore() {
        root.refreshDisplacedStores()
        root.rerouteWalletState()
        root.refreshSecurityState()
        root.logActivity(root.storeInPlace
            ? "A wallet store is in place. If it is encrypted, unlock it below; if it is not, set "
              + "a password on it."
            : "No wallet store is in place yet - run the copy command above, then check again.",
            !root.storeInPlace)
    }
    function displayId(id) {
        if (!id) return ""
        var s = id
        if (s.indexOf("Public/") === 0)  s = s.slice(7)
        else if (s.indexOf("Private/") === 0) s = s.slice(8)
        // MetaMask-style truncation: first6…last4
        return s.length > 13 ? s.slice(0, 6) + "…" + s.slice(-4) : s
    }

    // Networks the wallet can target (id, display label with version, sequencer mapping).
    function refreshZones() {
        if (typeof logos === "undefined" || !logos.callModule) return
        var r = callModuleParse(logos.callModule("medusa_core", "getZones", []))
        if (r && r.zones) { root.zones = r.zones; if (r.active) root.network = r.active }
    }
    function zoneName(id) {
        for (var i = 0; i < root.zones.length; i++)
            if (root.zones[i].id === id) return root.zones[i].name
        return id
    }
    function zoneKindDesc(z) {
        if (z.kind === "local-standalone") return "Local sandbox · for testing"
        if (z.kind === "local-l1-tor")     return "Default network · private over Tor"
        return (z.tor ? "Custom · over Tor" : "Custom · direct") + (z.endpoint ? " · " + z.endpoint : "")
    }
    // Connectivity colour for a zone's dot: only the ACTIVE zone has a live status; the
    // rest are simply "not connected" (neutral gray).
    function zoneDotColor(z) {
        if (root.network !== z.id) return root.connectGray
        if (root.seqStatus === "running")  return root.greenBright
        if (root.seqStatus === "starting") return root.connectGray
        return root.errorRed
    }
    function acctTitle(id, name) { return (name && name.length > 0) ? name : root.displayId(id) }
    function selectedAcctName() {
        for (var i = 0; i < accountModel.count; i++) {
            var a = accountModel.get(i)
            if (a.id === root.selectedFromId) return root.acctTitle(a.id, a.name)
        }
        return root.displayId(root.selectedFromId)
    }
    function renameAccount(id, name) {
        var r = callModuleParse(logos.callModule("medusa_core", "setAccountName", [id, name.trim()]))
        if (r && r.error) { logActivity("Rename failed: " + r.error, true); return }
        root.renamingAcctId = ""
        logActivity(name.trim().length > 0 ? "Account renamed" : "Name cleared", false)
        refreshAccounts()
    }
    function beginEditZone(z) {
        root.editingZoneId = z.id
        root.addZoneOpen = true
        zNameF.text = z.name || ""
        zTorTog.checked = !!z.tor
        zEndF.text = z.endpoint || ""
    }
    function switchZone(id) {
        runBusy("Switching zone", function() {
            var r = callModuleParse(logos.callModule("medusa_core", "setActiveZone", [id]))
            if (r && r.error) { root.logActivity("Zone switch failed: " + r.error, true); return }
            root.network = id
            root.zoneCompat = "unknown"       // the build-compat verdict is per-zone
            root.zoneOfflineOpen = false      // a stale offline modal refers to the old zone
            root.selectedFromId = ""; root.selectedTokens = []    // re-select on the new zone
            root.refreshSeqStatus(); root.refreshZones()
            netReloadTimer.restart()
            root.screen = "main"
        })
    }

    // Deterministic avatar colour from an account id (a simple hash → hue).
    function avatarColor(id) {
        if (!id) return root.borderColor
        var h = 0
        for (var i = 0; i < id.length; i++) h = (h * 31 + id.charCodeAt(i)) & 0xffffff
        return Qt.hsla((h % 360) / 360, 0.5, 0.45, 1)
    }

    function callModuleParse(raw) {
        try {
            var t = JSON.parse(raw)
            if (typeof t === 'string') { try { return JSON.parse(t) } catch(e) { return t } }
            return t
        } catch(e) { return null }
    }

    function logActivity(msg, isError) {
        if (isError === true) console.warn("[wallet]", msg)
        else console.log("[wallet]", msg)
        // Surface as a toast. Errors PERSIST (so they can be copied); confirmations auto-fade.
        root.notice = msg; root.noticeError = (isError === true)
        if (isError === true) noticeTimer.stop()
        else noticeTimer.restart()
    }
    property string notice:      ""
    property bool   noticeError: false

    // ── In-app self-update ────────────────────────────────────────────────────
    // Detects a newer medusa_ui/medusa_core in the repos (via Basecamp's package
    // manager) and installs it on demand. Entirely guarded on logos.callModuleAsync
    // so it silently no-ops (no button) if the async bridge isn't present. Basecamp
    // can't hot-reload a running module, so after install we ask the user to reopen.
    property bool   updAvailable: false
    property string updVersion:   ""     // newest medusa_ui version offered
    property var    updPlan:      []     // [{name,version,repoUrl,rootHash,isCore}] core-first
    property string updState:     ""     // "" | "downloading" | "installing" | "done" | "error"
    property string updMsg:       ""
    function jparse(raw) {
        try { var t = JSON.parse(raw); return (typeof t === 'string') ? JSON.parse(t) : t } catch(e) { return null }
    }
    // dotted-numeric compare: >0 if a newer than b.
    function verCmp(a, b) {
        var pa = String(a || "0").split("."), pb = String(b || "0").split(".")
        for (var i = 0; i < Math.max(pa.length, pb.length); i++) {
            var x = parseInt(pa[i] || "0", 10) || 0, y = parseInt(pb[i] || "0", 10) || 0
            if (x !== y) return x > y ? 1 : -1
        }
        return 0
    }
    function checkForUpdate() {
        if (typeof logos === "undefined" || !logos.callModuleAsync) return   // needs the async bridge
        if (root.updState === "downloading" || root.updState === "installing") return
        logos.callModuleAsync("package_manager", "getInstalledPackages", [], function(ij) {
            var inst = root.jparse(ij); if (!Array.isArray(inst)) return
            var mine = {}
            for (var i = 0; i < inst.length; i++) {
                var p = inst[i]; if (p && (p.name === "medusa_ui" || p.name === "medusa_core")) mine[p.name] = p
            }
            if (!mine["medusa_ui"]) return
            logos.callModuleAsync("package_downloader", "getCatalog", [], function(cj) {
                var cat = root.jparse(cj); if (!Array.isArray(cat)) return
                var plan = [], uiVer = ""
                for (var j = 0; j < cat.length; j++) {
                    var row = cat[j]; if (!row || (row.name !== "medusa_ui" && row.name !== "medusa_core")) continue
                    var cur = mine[row.name]; if (!cur) continue
                    var v = (row.versions && row.versions.length) ? row.versions[0] : null; if (!v) continue
                    var lv = (v.manifest && v.manifest.version) || v.version || ""
                    var lh = v.rootHash || (v.manifest && v.manifest.hashes && v.manifest.hashes.root) || ""
                    var ch = (cur.hashes && cur.hashes.root) || ""
                    if (root.verCmp(lv, cur.version) > 0 || (lv === cur.version && lh && ch && lh !== ch)) {
                        plan.push({ name: row.name, version: lv, isCore: row.name === "medusa_core",
                                    repoUrl: row.repositoryUrl || row.repository || "", rootHash: lh })
                        if (row.name === "medusa_ui") uiVer = lv
                    }
                }
                plan.sort(function(a, b) { return (b.isCore ? 1 : 0) - (a.isCore ? 1 : 0) })   // core before ui
                root.updPlan = plan
                root.updVersion = uiVer || (plan.length ? plan[0].version : "")
                if (plan.length > 0 && root.updState === "") root.updAvailable = true
            })
        })
    }
    function doUpdate() {
        if (!root.updAvailable || !root.updPlan.length || !logos.callModuleAsync) return
        var plan = root.updPlan.slice(), paths = []
        root.updState = "downloading"; root.updMsg = "Downloading update…"
        function dl(i) {
            if (i >= plan.length) { inst(0); return }
            var p = plan[i]
            logos.callModuleAsync("package_downloader", "downloadPinned",
                                  [p.repoUrl, p.name, p.version, p.rootHash], function(rj) {
                var r = root.jparse(rj)
                if (!r || r.error || !r.path) {
                    root.updState = "error"; root.updMsg = "Download failed: " + ((r && r.error) || "unknown"); return
                }
                paths.push(r.path); dl(i + 1)
            })
        }
        function inst(i) {
            if (i >= paths.length) {
                root.updState = "done"; root.updAvailable = false
                root.updMsg = "Updated to v" + root.updVersion + " - reopen Medusa to apply."
                return
            }
            root.updState = "installing"; root.updMsg = "Installing…"
            logos.callModuleAsync("package_manager", "installPlugin", [paths[i], false], function(rj) {
                var r = root.jparse(rj)
                if (!r || r.error) {
                    root.updState = "error"; root.updMsg = "Install failed: " + ((r && r.error) || "unknown"); return
                }
                inst(i + 1)
            })
        }
        dl(0)
    }
    Timer { id: updateCheckTimer; interval: 900000; running: true; repeat: true   // re-check every 15 min
            onTriggered: root.checkForUpdate() }

    function refreshAccounts() {
        if (typeof logos === "undefined" || !logos.callModule) return
        // Don't disturb the post-create "back up your phrase" screen.
        if (root.walletState === "backup") return

        // Route on lifecycle state only until the wallet is ready (a filesystem check -
        // never auto-creates a wallet). Once ready we just refresh the account list:
        // re-routing here would let a transient read during a concurrent on-chain write
        // bounce the user back to the create screen.
        if (root.walletState !== "ready") {
            var st = callModuleParse(logos.callModule("medusa_core", "getWalletState", []))
            if (!st) return
            // Stores this module has moved or copied aside. Nothing used to report them, and
            // "my wallet vanished and Reset buried it deeper" was the result - so they are shown,
            // and Security & Backup offers a confirmed way to put one back.
            root.displacedStores = Array.isArray(st.displacedStores) ? st.displacedStores : []
            root.storeInPlace    = (st.exists === true)
            if (!st.exists) {
                root.walletState = "new"; root.walletLocked = false
                accountModel.clear(); refreshAccountBuckets(); return
            }
            if (st.encrypted && !st.unlocked) {
                root.walletState = "locked"; root.walletLocked = true
                accountModel.clear(); refreshAccountBuckets(); return
            }
            // An UNENCRYPTED store is a REACHABLE wallet, not a locked one, so it must not route to
            // the lock screen: there is no password to ask for there (unlock() refuses a store it
            // cannot verify a password against), and blocking here was the round-2 regression that
            // left a user with funds and no working button. What it is not is a fully working
            // wallet: the core's gate refuses every gated verb on it, so signing, dApp approvals
            // and export stay DISABLED until the store is migrated. Both facts are surfaced rather
            // than routed - storeUnprotected drives the persistent warning, the disabled controls,
            // and the "set a password" migration in Security & Backup, which is the way out.
            root.walletState = "ready"; root.walletLocked = false
        }

        // listAccounts is now non-blocking (local list + async balance cache), so it's safe
        // to call any time - accounts show immediately, balances fill in from the background.
        var r = callModuleParse(logos.callModule("medusa_core", "listAccounts", []))
        if (r && r.error) {
            // Over Tor the balance-fetching list can time out even when connected - that's
            // expected noise; keep the current list quietly. Only surface it on non-Tor zones.
            if (!root.activeZoneIsTor()) logActivity("listAccounts: " + r.error, true)
            return
        }
        accountModel.clear()
        if (!r) return

        var arr = []
        if (Array.isArray(r)) {
            arr = r
        } else if (r.accounts && Array.isArray(r.accounts)) {
            arr = r.accounts
        } else if (r.output) {
            accountModel.append({ id: r.output, type: "public", balance: "", initialized: false, name: "" })
            return
        }
        root.accounts = arr

        arr.sort(function(a, b) {
            var ta = (a.type || "public"), tb = (b.type || "public")
            if (ta === "public" && tb !== "public") return -1
            if (ta !== "public" && tb === "public") return 1
            return (parseFloat(b.balance) || 0) - (parseFloat(a.balance) || 0)
        })

        for (var i = 0; i < arr.length; i++) {
            var a = arr[i]
            accountModel.append({
                id:      a.id      || a.accountId || JSON.stringify(a),
                type:    a.type    || "public",
                balance: a.balance !== undefined ? String(a.balance) : "-",
                // initialized==true means the account has on-chain state; a private one
                // with state (even a token note at balance 0) can't receive privacy output.
                initialized: a.initialized === true,
                name:    a.name    || ""
            })
        }

        // Update balance/type of already-selected account (don't change selection)
        if (root.selectedFromId.length > 0) {
            for (var j = 0; j < accountModel.count; j++) {
                if (accountModel.get(j).id === root.selectedFromId) {
                    root.selectedFromBalance = accountModel.get(j).balance
                    root.selectedFromType    = accountModel.get(j).type
                    break
                }
            }
        }

        refreshAccountBuckets()
    }

    function refreshStatus() {
        if (typeof logos === "undefined" || !logos.callModule) return
        var s = callModuleParse(logos.callModule("medusa_core", "getStatus", []))
        if (s) {
            root.cliFound = s.cliFound === true
            root.cliPath  = s.cliPath || ""
        }
    }

    function refreshSeqStatus() {
        if (typeof logos === "undefined" || !logos.callModule) return
        var s = callModuleParse(logos.callModule("medusa_core", "getSequencerStatus", []))
        if (s && s.state) { root.seqStatus = s.state; if (s.mode) root.seqMode = s.mode }
        // Devnet (a local zone) needs a sequencer binary on disk to spawn one; if it's absent
        // the sequencer can never come up - flag it so the UI shows a clear disclaimer.
        root.seqBinaryMissing = !!(s && s.mode === "local-standalone" && s.binaryAvailable === false)
        root.seqBinaryPath = (s && s.binaryPath) ? s.binaryPath : ""
        // Tor/onion zone with no usable Tor binary (neither bundled medusa-tor nor a system tor).
        root.torBinaryMissing = !!(s && s.needsTor === true && s.torAvailable === false)
        // Failure surface for the offline modal + local-sequencer banner.
        root.seqReason      = (s && s.reason) ? s.reason : ""
        root.seqLaunchError = (s && s.lastLaunchError) ? s.lastLaunchError : ""
        root.seqLogPath     = (s && s.logPath) ? s.logPath : ""
        root.seqExitCode    = (s && s.exitCode !== undefined) ? s.exitCode : 0
        root.seqEndpoint    = (s && s.endpoint) ? s.endpoint : ""
        // The ACTIVE zone's port, so netExpectedDial can name the loopback address a local or
        // Tor zone is supposed to dial instead of accepting any 127.0.0.1:<anything>.
        root.seqDialPort    = (s && s.port) ? s.port : 0
        // Watch the address the wallet actually dials, so the approval sheets can say "this
        // moved N minutes ago". The FIRST observation of a session is not a change.
        if (root.seqEndpoint !== "") {
            if (root.netDialSeen === "") root.netDialSeen = root.seqEndpoint
            else if (root.netDialSeen !== root.seqEndpoint) {
                root.netDialSeen      = root.seqEndpoint
                root.netDialChangedAt = Date.now()
            }
        }
        root.netTick = root.netTick + 1   // ages netChangedRecently / netChangedAgo()
        // Only accept a real verdict: "unknown" must not erase a mismatch learned from an
        // op error (the plugin re-probes lazily). Zone switches reset this explicitly.
        if (s && (s.compat === "ok" || s.compat === "mismatch")) root.zoneCompat = s.compat
        // While connecting over Tor, surface bootstrap progress for the connect bar.
        if (root.activeZoneIsTor() && root.seqStatus !== "running") {
            var t = callModuleParse(logos.callModule("medusa_core", "getTorProgress", []))
            if (t) {
                root.torPercent = t.percent || 0; root.torStage = t.stage || ""
                root.torOnionStage = t.onionStage || ""; root.torOnionPct = t.onionPct || 0
            }
        }
    }
    function activeZoneIsTor() {
        if (root.network === "diaphani") return true   // built-in Tor zone (robust if zones not loaded yet)
        for (var i = 0; i < root.zones.length; i++)
            if (root.zones[i].id === root.network)
                return root.zones[i].kind === "local-l1-tor"
                    || (root.zones[i].kind === "remote" && root.zones[i].tor)
        return false
    }

    // ── Zone-offline guard: user-triggered sequencer ops must NEVER fail silently ──
    // (real incident: a send while the zone was down produced no feedback at all).
    // Returns true when the op may proceed; otherwise raises the blocking modal.
    // NB: passive refreshes (unlock, background listAccounts - incl. the deliberate
    // Tor-noise suppression above) are NOT ops and stay un-guarded.
    function guardZoneOp(opName) {
        if (root.seqProblem === "mismatch") { root.openZoneOffline(opName, true); return false }
        if (root.seqStatus === "running") return true
        root.openZoneOffline(opName, false)
        return false
    }
    function openZoneOffline(opName, mismatch) {
        root.zoneOfflineOp = opName || ""
        root.zoneOfflineMismatch = (mismatch === true)
        root.zoneOfflineOpen = true
    }
    // Transport/connection-class error classifier. The wrapper's zone gate ("program ids
    // differ") routes to the mismatch wording instead - a reachable-but-incompatible zone
    // must not read as "check your network".
    function classifyOpError(msg) {
        var s = String(msg || "").toLowerCase()
        if (s.indexOf("program ids differ") >= 0 || s.indexOf("different from remote") >= 0)
            return "mismatch"
        var pats = ["connection refused", "connection reset", "timed out", "timeout",
                    "dns error", "error sending request", "transport", "failed to connect",
                    "connection closed", "network is unreachable"]
        for (var i = 0; i < pats.length; i++)
            if (s.indexOf(pats[i]) >= 0) return "offline"
        return ""
    }
    // Surface an operation failure: always the persistent error toast (copyable), PLUS
    // the blocking zone modal when the failure is connection-class or the mismatch gate.
    function surfaceOpError(label, msg) {
        var m = (msg && String(msg).length > 0) ? String(msg) : "unknown"
        root.logActivity(label + " failed: " + m, true)
        var cls = root.classifyOpError(m)
        if (cls === "mismatch") { root.zoneCompat = "mismatch"; root.openZoneOffline(label, true) }
        else if (cls === "offline") root.openZoneOffline(label, false)
    }
    // Retry from the offline modal: re-check health, close on success. Health for
    // remote/Tor zones is an async cached probe, so poll a few rounds (covering the
    // probe's 8s budget) instead of reading one stale answer.
    function retryZoneHealth() {
        root.zoneRetryBusy = true
        zoneRetryTimer.tries = 0
        root.refreshSeqStatus()
        zoneRetryTimer.start()
    }
    // The endpoint to NAME in offline messages: the zone's configured endpoint (URL or
    // onion) when known, else the effective address the wallet dials (local sequencer).
    function zoneEndpointDesc() {
        for (var i = 0; i < root.zones.length; i++)
            if (root.zones[i].id === root.network && root.zones[i].endpoint)
                return root.zones[i].endpoint
        return root.seqEndpoint
    }

    // ── Operating-network disclosure for the Connect approval surfaces ─────────
    // RESIDUAL RISK THIS CLOSES. Keys and signing are local and stay local, but any program
    // running as the user can rewrite ~/.medusa/wallet_config.json's `sequencer_addr` and
    // repoint the wallet at its own sequencer. Nothing is stolen by that write; instead every
    // balance the UI renders and every transaction the wallet broadcasts belong to whoever
    // owns that address - a fake incoming balance is a working merchant scam, and a censored
    // outgoing one makes the user pay twice. There is no way to PREVENT the write from inside
    // the UI, so the mitigation is disclosure at the only moment it matters: whenever the
    // wallet is about to act because the SDK asked it to, it names the network it will act on,
    // ADDRESS FIRST. The name is what a repoint keeps; the address is what it changes.
    //
    // SOURCING RULE (the whole point - a spoofable banner is worse than none):
    // every value below comes back from medusa_core, either getZones() (the wallet's own zone
    // list + its own active-zone id) or getSequencerStatus() (which re-reads sequencer_addr
    // off disk on every poll). NOTHING here reads root.pendingConn, so a dApp contributes no
    // input to any of it. The zone sheet does pass the dApp's PROPOSED address into
    // knownZoneFor()/zoneReqIsCurrent(), but those return the WALLET's verdict about it,
    // never the dApp's own string.

    property int    seqDialPort:      0     // netPort() for the ACTIVE zone (from getSequencerStatus)
    property int    netTick:          0     // bumped every status poll: gives the time-based
                                            // bindings below a dependency that actually moves
    property string netDialSeen:      ""    // last dialled address this session has observed
    property real   netDialChangedAt: 0     // ms epoch of the last CHANGE ("" -> x is not a change)

    // The zone record the wallet is actually on (plugin's list, plugin's active id).
    function activeZoneObj() {
        for (var i = 0; i < root.zones.length; i++)
            if (root.zones[i].id === root.network) return root.zones[i]
        return null
    }
    // Compare two endpoints the way applySequencer()/addZone() write them: trim, case-fold,
    // default a missing scheme to http (addZone does exactly this), ignore a trailing slash.
    // Deliberately FORGIVING - a false "repointed" on an ordinary tip is precisely the alarm
    // fatigue this surface exists to avoid.
    function sameEndpoint(a, b) {
        var norm = function(s) {
            var t = String(s || "").trim().toLowerCase()
            if (t === "") return ""
            if (t.indexOf("://") < 0) t = "http://" + t
            while (t.length > 1 && t.charAt(t.length - 1) === "/") t = t.slice(0, -1)
            return t
        }
        var x = norm(a)
        return x !== "" && x === norm(b)
    }
    // What the ACTIVE zone's own record says this wallet ought to be dialling:
    //   local zone, or ANY Tor zone -> the loopback sequencer / tunnel entrance on the zone's
    //                                  port (the .onion is reached THROUGH it, so the address
    //                                  actually dialled is loopback, by design)
    //   remote clearnet             -> the zone's URL, verbatim
    // "" means "not established yet" (zones not loaded, port unknown) and deliberately
    // suppresses the repoint verdict rather than guessing.
    readonly property string netExpectedDial: {
        var z = root.activeZoneObj()
        if (!z) return ""
        if (z.kind === "local-standalone" || z.kind === "local-l1-tor" || z.tor === true)
            return root.seqDialPort > 0 ? "http://127.0.0.1:" + root.seqDialPort + "/" : ""
        return z.endpoint || ""
    }
    // The address the wallet ACTUALLY dials. getSequencerStatus() reads this straight back out
    // of wallet_config.json on every poll, so if anything tampered with that file, THIS is the
    // tampered value - which is exactly why it is the one compared and the one printed.
    readonly property string netActualDial: root.seqEndpoint
    // The address to PRINT as the network's identity. For a Tor zone the loopback forwarder is
    // an implementation detail and the .onion is the identity, so print the .onion; for every
    // other zone the identity IS the dialled address.
    readonly property string netShownAddr: {
        var z = root.activeZoneObj()
        if (z && z.tor === true && z.endpoint) return z.endpoint
        return root.netActualDial !== "" ? root.netActualDial : root.zoneEndpointDesc()
    }
    // THE repoint tell: the address on disk is not the one this zone declares.
    readonly property bool netRepointed: root.netExpectedDial !== "" && root.netActualDial !== ""
                                         && !root.sameEndpoint(root.netExpectedDial, root.netActualDial)
    // Did the dialled address move recently? The first observation of a session is NOT a change
    // (the wallet has to start somewhere); every later one is timestamped in refreshSeqStatus().
    readonly property bool netChangedRecently: {
        var t = root.netTick    // dependency: re-evaluate on every status poll so this expires
        return t >= 0 && root.netDialChangedAt > 0
               && (Date.now() - root.netDialChangedAt) < 600000   // 10 min
    }
    function netChangedAgo() {
        var t = root.netTick    // same dependency, so the wording ages with the clock
        if (t < 0 || root.netDialChangedAt <= 0) return ""
        var m = Math.floor((Date.now() - root.netDialChangedAt) / 60000)
        return m <= 0 ? "just now" : (m === 1 ? "1 minute ago" : m + " minutes ago")
    }
    // Severity for the banner, worst first. "" keeps the sheet CALM, and that is a feature:
    // a warning that fires on every ordinary tip is a warning nobody reads. Note "starting"
    // (the async health probe still in flight) is NOT an alert - it renders as "checking…".
    //   repoint  - the address on disk is not the one this zone declares   (the attack)
    //   mismatch - the zone answers but runs a different LEZ build         (compat probe)
    //   offline  - the zone does not answer at all                         (health probe)
    //   changed  - the dialled address moved within the last 10 minutes    (recency)
    readonly property string netAlert: {
        if (root.netRepointed)                return "repoint"
        if (root.zoneCompat === "mismatch")   return "mismatch"
        if (root.seqStatus === "unreachable") return "offline"
        if (root.netChangedRecently)          return "changed"
        return ""
    }
    // Only the repoint gets the error colour; the rest reuse the file's existing amber-warning
    // treatment (errorTint fill + amber rule), the same pairing as the "not encrypted" banner.
    readonly property color netAlertColor: root.netAlert === "repoint" ? root.errorRed
                                         : root.netAlert === ""        ? root.borderColor
                                                                       : root.warningAmber
    function netAlertTitle() {
        if (root.netAlert === "repoint")  return "This is not the address this network should have"
        if (root.netAlert === "mismatch") return "This network runs a different build"
        if (root.netAlert === "offline")  return "This network is not answering"
        if (root.netAlert === "changed")  return "This network changed " + root.netChangedAgo()
        return ""
    }
    function netAlertBody() {
        if (root.netAlert === "repoint")
            return "Something rewrote this wallet's sequencer address. Balances and any "
                 + "broadcast would go to the address below, not to the zone you picked. "
                 + "Reject this, then re-select the zone under Network."
        if (root.netAlert === "mismatch")
            return "Its program ids differ from this wallet's, so this would fail on-chain. "
                 + "Update the Medusa module or switch zone."
        if (root.netAlert === "offline")
            return "The wallet cannot confirm this address is live, so it cannot confirm "
                 + "what it would be acting on."
        if (root.netAlert === "changed")
            return "If you did not switch networks yourself, reject this and check Network."
        return ""
    }
    // A zone NAME is only as trustworthy as whoever added the zone. Built-in names are the
    // wallet's own; a user zone's name is free text, and approveZone() names an auto-added
    // zone after the dApp's own `label`. So a dApp that once got a zone request approved can
    // have put the string "Paradox Computer · clearnet" into this list. That is exactly why
    // the ADDRESS is the load-bearing line on every sheet and the name is only a label - and
    // why a non-builtin active zone is tagged "custom" wherever its name is shown.
    readonly property bool activeZoneIsCustom: {
        var z = root.activeZoneObj()
        return z !== null && z.builtin !== true
    }
    // Does the wallet ALREADY know an endpoint? Mirrors approveZone()'s reuse rule (same
    // transport AND same endpoint), so the answer describes what the core would really do.
    // Returns the zone RECORD (callers need .name and .builtin), or null for a new network.
    function knownZoneFor(endpoint, tor) {
        for (var i = 0; i < root.zones.length; i++) {
            var z = root.zones[i]
            if (!z.endpoint || (z.tor === true) !== (tor === true)) continue
            if (root.sameEndpoint(z.endpoint, endpoint)) return z
        }
        return null
    }
    // Would approving a zone request actually change the network, or is it a no-op?
    function zoneReqIsCurrent(endpoint, tor) {
        var z = root.activeZoneObj()
        if (!z || (z.tor === true) !== (tor === true)) return false
        return root.sameEndpoint(z.endpoint || root.netActualDial, endpoint)
    }

    function seqProblemTitle() {
        return root.seqProblem === "mismatch" ? "Zone build mismatch" : "Local sequencer not running"
    }
    // Reason-specific advice for the banner + offline modal. Every branch says what to DO
    // (reinstall the module / restart Basecamp / switch zone).
    function seqProblemBody() {
        if (root.seqProblem === "mismatch")
            return "This zone's sequencer runs a different LEZ build than this wallet (program "
                 + "ids differ). Reinstall/update the Medusa module so both match, or switch zone."
        if (root.seqProblem === "binary-missing")
            return "The bundled sequencer binary was not found"
                 + (root.seqBinaryPath ? " (looked for " + root.seqBinaryPath + ")" : "")
                 + ". Reinstall the Medusa module, or switch zone."
        if (root.seqProblem === "launch-failed")
            return "The local sequencer failed to launch"
                 + (root.seqLaunchError ? ": " + root.seqLaunchError : "")
                 + ". Reinstall the module, restart Basecamp, or switch zone."
        if (root.seqProblem === "exited")
            return "The local sequencer process exited (code " + root.seqExitCode + ")"
                 + (root.seqLogPath ? " - log: " + root.seqLogPath : "")
                 + ". Restart Basecamp to relaunch it, or switch zone."
        if (root.seqProblem === "tor-missing")
            return "This zone tunnels over Tor, but no Tor binary was found. Reinstall the "
                 + "Medusa module (it bundles Tor) or install a system tor, then restart Basecamp."
        return "The local sequencer is not answering health checks"
             + (root.seqLogPath ? " - log: " + root.seqLogPath : "")
             + ". Restart Basecamp to relaunch it, or switch zone."
    }

    function refreshTxHistory() {
        if (typeof logos === "undefined" || !logos.callModule) return
        if (root.selectedFromId.length === 0) return
        var r = callModuleParse(logos.callModule("medusa_core", "getTransactions", [root.selectedFromId]))
        root.txHistory = Array.isArray(r) ? r : []
        txHistoryModel.clear()
        for (var i = 0; i < root.txHistory.length; i++)
            txHistoryModel.append(root.txHistory[i])
    }

    // Spendable balance of the asset currently selected in the Send screen.
    function sendBalance() {
        if (root.sendTokenDef === "")
            return parseInt(root.selectedFromBalance) || 0
        for (var i = 0; i < root.selectedTokens.length; i++)
            if (root.selectedTokens[i].definitionId === root.sendTokenDef)
                return parseInt(root.selectedTokens[i].balance) || 0
        return 0
    }

    function doSend(to, amount) {
        // Sequencer op: never let it fail silently against a dead/incompatible zone.
        if (!root.guardZoneOp(root.sendTokenDef === "" ? "Transfer" : "Token send")) return
        var bal = root.sendBalance()
        var sym = root.sendTokenDef === "" ? "LEZ" : root.sendTokenName
        var raw = String(amount).trim()
        // LEZ/token amounts are whole units - reject decimals/garbage with a clear message.
        if (!/^[0-9]+$/.test(raw)) { logActivity(sym + " amounts are whole numbers - no decimals (e.g. 1, not 0.13).", true); return }
        var amt = parseInt(raw, 10)
        if (amt <= 0) { logActivity("Enter an amount greater than 0.", true); return }
        if (bal <= 0) { logActivity("No " + sym + " balance on this account.", true); return }
        if (amt > bal) { logActivity("Amount exceeds your " + bal + " " + sym + " balance.", true); return }
        if (root.sendTokenDef === "") executeSend(root.selectedFromId, to, amount)
        else {
            // token send is a background job (derive/create ATAs + token-send + wait)
            var r = root.callGated("startSendToken",
                        [root.selectedFromId, to, root.sendTokenDef, amount])
            if (!r || r.error) {
                if (root.handleAuthRefusal("Token send", r)) return
                surfaceOpError("Token send", r && r.error ? r.error : "unknown"); return
            }
            if (!r.jobId) { logActivity("No jobId from token send", true); return }
            logActivity("Sending " + amount + " " + root.sendTokenName + "…", false)
            root.trackJob({ jobId: r.jobId, op: "tokensend", asset: "token",
                            from: root.selectedFromId, to: to, amount: amount, state: "running", elapsedMs: 0, txId: "", error: "" })
            root.screen = "main"
        }
    }

    function executeSend(from, to, amount) {
        if (typeof logos === "undefined" || !logos.callModule) {
            root.sendStatus = "Module not available"
            return
        }
        root.screen = "main"   // close form immediately
        // Single path for ALL sends (public, shield, private→private, deshield). Async because
        // any case with a Private endpoint is a multi-minute proof - running it blocking timed
        // out / froze the UI ("Transfer failed: wallet command timed out"). Now it's a tracked
        // background job; the wrapper auto-syncs + uses the proof budget when --from is Private.
        var r = root.callGated("startSendTransfer", [from, to, amount])
        if (!r || r.error) {
            if (root.handleAuthRefusal("Transfer", r)) return
            surfaceOpError("Transfer", r && r.error ? r.error : "unknown")
            return
        }
        if (!r.jobId) { logActivity("No jobId returned from transfer", true); return }
        var toPriv = (String(to).indexOf("Private/") === 0)
        logActivity("Transfer started" + (toPriv ? " - proving (may take minutes)…" : " - submitting…"), false)
        trackJob({ jobId: r.jobId, op: "send", asset: "native", from: from, to: to,
                   amount: amount, state: "running", elapsedMs: 0, txId: "", error: "" })
    }

    // ── Privacy helpers ─────────────────────────────────────────────────────────
    function refreshAccountBuckets() {
        var pub = [], priv = [], privEmpty = [], pubT = 0, privT = 0
        for (var i = 0; i < accountModel.count; i++) {
            var a = accountModel.get(i)
            var n = parseFloat(a.balance) || 0          // native LEZ balance
            if ((a.type || "public") === "private") {
                priv.push(a.id); privT += n
                if (!a.initialized && n === 0) privEmpty.push(a.id)
            }
            else { pub.push(a.id); pubT += n }
        }
        root.publicAccounts  = pub
        root.privateAccounts = priv
        root.emptyPrivateAccounts = privEmpty
        root.heroPublicTotal  = String(pubT)
        root.heroPrivateTotal = String(privT)
        root.heroTotal        = String(pubT + privT)
    }

    // Token choices for the privacy screen (shield: direct holdings; deshield: registry).
    function refreshPrivAssets() {
        root.shieldableTokens = []; root.registryTokens = []
        if (typeof logos === "undefined" || !logos.callModule) return
        var dh = callModuleParse(logos.callModule("medusa_core", "getDirectHoldings", []))
        if (Array.isArray(dh)) root.shieldableTokens = dh
        var tr = callModuleParse(logos.callModule("medusa_core", "getTokenRegistry", []))
        if (tr && tr.names) {
            var arr = []
            for (var d in tr.names) arr.push({ definitionId: d, ticker: tr.names[d] })
            root.registryTokens = arr
        }
        if (tr && Array.isArray(tr.privateDests)) root.usedPrivateDests = tr.privateDests
    }

    function refreshWhitelist() {
        if (typeof logos === "undefined" || !logos.callModule) return
        var r = callModuleParse(logos.callModule("medusa_core", "getWhitelist", []))
        root.whitelistTokens = Array.isArray(r) ? r : []
    }

    function refreshTokens() {
        root.selectedTokens = []
        if (typeof logos === "undefined" || !logos.callModule) return
        if (!root.selectedFromId || root.selectedFromId.length === 0) return
        var r = callModuleParse(logos.callModule("medusa_core", "getTokens", [root.selectedFromId]))
        if (Array.isArray(r)) root.selectedTokens = r
    }

    function doAddToken(defId) {
        if (typeof logos === "undefined" || !logos.callModule) return
        var r = callModuleParse(logos.callModule("medusa_core", "addToken", [defId]))
        if (!r || r.error) { logActivity("addToken: " + (r && r.error ? r.error : "failed"), true); return }
        logActivity("Token registered", false)
        root.refreshTokens()
    }

    function doClaimFaucet() {
        if (typeof logos === "undefined" || !logos.callModule) return
        if (!root.guardZoneOp("Faucet claim")) return   // sequencer op - never silent offline
        var acctId = root.selectedFromId
        if (!acctId && accountModel.count > 0) acctId = accountModel.get(0).id
        if (!acctId) { root.logActivity("No accounts - create one first", true); return }
        var r = callModuleParse(logos.callModule("medusa_core", "startFaucet", [acctId]))
        if (!r || r.error) { root.surfaceOpError("Faucet claim", r && r.error ? r.error : "unknown"); return }
        if (!r.jobId) { root.logActivity("No jobId returned from faucet", true); return }
        root.logActivity("Claiming faucet → " + root.displayId(acctId).substring(0, 16) + "…", false)
        root.trackJob({ jobId: r.jobId, op: "faucet", asset: "native",
                        from: acctId, to: "", amount: "150", state: "running", elapsedMs: 0, txId: "", error: "" })
    }

    function opLabel(op) {
        if (op === "shield")    return "Shield"
        if (op === "deshield")  return "Deshield"
        if (op === "private")   return "Private transfer"
        if (op === "send")      return "Transfer"
        if (op === "faucet")    return "Faucet claim"
        if (op === "tokensend") return "Token send"
        return op
    }

    function rebuildJobsModel() {
        // Append only the roles the delegate reads, uniformly. In particular we do
        // NOT expose "from" as a model role (reserved in QML delegate scope), and
        // every row carries the same keys so ListModel role inference is stable.
        jobsModel.clear()
        for (var i = 0; i < root.privJobs.length; i++) {
            var j = root.privJobs[i]
            jobsModel.append({
                op:        j.op    || "",
                asset:     j.asset || "native",
                state:     j.state || "running",
                phase:     j.phase || ((j.state === "done" || j.state === "error") ? "" : "processing"),
                amount:    j.amount || "",
                txId:      j.txId  || "",
                error:     j.error || "",
                elapsedMs: j.elapsedMs || 0
            })
        }
    }

    function trackJob(job) {
        var arr = root.privJobs.slice()
        arr.unshift(job)               // newest first
        if (arr.length > 12) arr.pop()
        root.privJobs = arr
        rebuildJobsModel()
        privJobsTimer.start()
    }

    // ── Medusa-Connect: dApp session + per-action approval ───────────────────────
    // A foreign module (via @paradoxcomputer/medusa-connect) asks the wallet to connect
    // and to run each write; the user gates both here. The connPollTimer surfaces the
    // FIFO-first pending request as a modal sheet, keyed off its "kind".
    property var    pendingConn:        []     // [{requestId,kind,app|action fields,...}]
    property var    connAccountSel:     ({})   // accountId -> bool, the Connect-sheet picker
    property string connAuthorizedApp:  ""     // non-empty → show the "Authorized! go back" confirmation

    // Find the pending request's app display name (for the confirmation modal).
    function connAppName(requestId) {
        for (var i = 0; i < root.pendingConn.length; i++) {
            if (root.pendingConn[i].requestId === requestId) {
                var a = root.pendingConn[i].app
                return (a && a.appName) ? a.appName : ""
            }
        }
        return ""
    }

    function approveConnectRequest(requestId, selectedIds) {
        if (typeof logos === "undefined" || !logos.callModule) return
        var appName = root.connAppName(requestId)   // capture before the request is cleared
        var r = root.callModuleParse(logos.callModule("medusa_core",
            "approveConnect", [requestId, JSON.stringify(selectedIds)]))
        if (r && r.error) { root.logActivity("Connect failed: " + r.error, true); return }
        root.logActivity("Connected" + (r && r.zone ? " (" + r.zone + ")" : ""), false)
        root.connAccountSel = ({})
        root.connAuthorizedApp = appName || "the app"   // surfaces the confirmation modal
        root.pollConnRequests()   // refresh immediately so the sheet dismisses
    }

    function rejectConnectRequest(requestId) {
        if (typeof logos === "undefined" || !logos.callModule) return
        logos.callModule("medusa_core", "rejectConnect", [requestId])
        root.connAccountSel = ({})
        root.pollConnRequests()
    }

    function approveActionRequest(req) {
        if (typeof logos === "undefined" || !logos.callModule) return
        // Approving a dApp action dispatches a sequencer op - same guard as in-wallet ops
        // (the request stays pending, so the user can approve once the zone is back).
        if (!root.guardZoneOp(root.opLabel(req.op || "send"))) return
        // Approval happens IN THE WALLET, never in the dApp, so the session password is right
        // here - the SDK/Tip Jar contract is untouched and no dApp ever sees the password.
        var r = root.callGated("approveAction", [req.requestId])
        if (r && r.error) {
            if (root.handleAuthRefusal("Action", r)) return
            root.surfaceOpError("Action", r.error); return
        }
        if (r && r.status === "rejected") {
            root.logActivity("Action rejected: " + (r.error || ""), true)
            root.pollConnRequests(); return
        }
        // r.jobId is an existing privacy/send job - track it with the SAME trackJob() the
        // Send screen uses, so connect-initiated actions show in the in-wallet job list too.
        root.logActivity("Action approved - " + (req.op || "send") + " started…", false)
        root.trackJob({ jobId: r.jobId, op: req.op || "send", asset: req.asset || "native",
                        from: req.from || "", to: req.to || "", amount: req.amount || "",
                        state: "running", elapsedMs: 0, txId: "", error: "" })
        root.pollConnRequests()
    }

    function rejectActionRequest(requestId) {
        if (typeof logos === "undefined" || !logos.callModule) return
        logos.callModule("medusa_core", "rejectConnect", [requestId])   // shared reject verb
        root.pollConnRequests()
    }

    // A connected dApp asked the wallet to switch its sequencer / zone; the user gates it
    // through the zoneSheet (mirrors the connect/action approval flow + reject verbs).
    function approveZoneRequest(requestId) {
        if (typeof logos === "undefined" || !logos.callModule) return
        // GATED in the core exactly like approveAction: what it approves is repointing the wallet
        // at someone else's sequencer. callGated() appends the session password; the zoneSheet is
        // only visible while walletState is "ready", so it is always there.
        var r = root.callGated("approveZone", [requestId])
        if (!r || r.error || r.status !== "approved") {
            if (root.handleAuthRefusal("Sequencer switch", r)) { root.pollConnRequests(); return }
            root.logActivity("Sequencer switch failed: "
                             + ((r && (r.error || r.status)) || "no response"), true)
            root.pollConnRequests()
            return
        }
        // The wallet is now on the requested zone - mirror switchZone(): re-select + refresh
        // so the network label, accounts and balances don't linger on the old zone.
        root.network = r.zoneId
        root.zoneCompat = "unknown"       // the build-compat verdict is per-zone
        root.zoneOfflineOpen = false
        root.selectedFromId = ""; root.selectedTokens = []
        refreshSeqStatus(); refreshZones()
        netReloadTimer.restart()
        root.logActivity("Sequencer switch approved", false)
        root.pollConnRequests()   // refresh immediately so the sheet dismisses / next surfaces
    }

    function rejectZoneRequest(requestId) {
        if (typeof logos === "undefined" || !logos.callModule) return
        logos.callModule("medusa_core", "rejectZone", [requestId])
        root.pollConnRequests()
    }

    function pollConnRequests() {
        if (typeof logos === "undefined" || !logos.callModule) return
        var r = root.callModuleParse(logos.callModule("medusa_core", "pendingRequests", []))
        var next = Array.isArray(r) ? r : []
        // When a NEW connect sheet appears, default its account picker to none-selected.
        var head = next.length > 0 ? next[0] : null
        var prevHead = root.pendingConn.length > 0 ? root.pendingConn[0] : null
        if (head && head.kind === "connect" &&
                (!prevHead || prevHead.requestId !== head.requestId)) {
            root.connAccountSel = ({})
        }
        root.pendingConn = next
    }

    // Estimated privacy-cost hint for the action sheet (mirrors the Send screen labels).
    function connActionHint(req) {
        if (!req) return ""
        var toPriv = (req.to || "").indexOf("Private/") === 0
        if (req.op === "private" || req.op === "shield" || toPriv)
            return "Generates a zero-knowledge proof - may take several minutes."
        if (req.op === "deshield")
            return "De-shields to a public account (de-anonymizing) - proof may take minutes."
        return "Public transfer - confirms in a few seconds."
    }

    function startPrivacyOp() {
        if (typeof logos === "undefined" || !logos.callModule) return
        // Sequencer op (shield / deshield / private transfer) - never silent offline.
        if (!root.guardZoneOp(opLabel(root.privMode === "transfer" ? "private" : root.privMode))) return
        var amt = root.privAmount.trim()
        if (!root.privFromValid || amt.length === 0) return

        var from = root.selectedFromId
        var method, args
        // args stops BEFORE the session password - callGated() appends it (invariant 1). On
        // shield/deshield that still forces definitionId to be passed always (empty for native),
        // because it had a default and the password sits behind it.
        if (root.privMode === "shield") {
            if (root.privToId.length === 0) { root.logActivity("Pick a private destination account", true); return }
            method = "startShield"
            args   = [root.privAsset, from, root.privToId, amt,
                      root.privAsset === "token" ? root.privTokenDef : ""]   // token shield needs the def
        } else if (root.privMode === "deshield") {
            if (root.privToId.length === 0) { root.logActivity("Pick a public destination account", true); return }
            method = "startDeshield"
            args   = [root.privAsset, from, root.privToId, amt,
                      root.privAsset === "token" ? root.privTokenDef : ""]   // def routes to the recipient's ATA
        } else { // transfer
            if (root.privToMode === "foreign") {
                if (root.privToNpk.trim().length === 0 || root.privToVpk.trim().length === 0
                        || root.privToIdent.trim().length === 0) {
                    root.logActivity("Foreign transfer needs npk, vpk and identifier", true); return
                }
                method = "startPrivateTransferForeign"
                args   = [root.privAsset, from, root.privToNpk.trim(), root.privToVpk.trim(),
                          root.privToIdent.trim(), amt]
            } else {
                if (root.privToId.length === 0) { root.logActivity("Pick a private destination account", true); return }
                method = "startPrivateTransfer"
                args   = [root.privAsset, from, root.privToId, amt]
            }
        }

        root.privBusy = true
        var r = root.callGated(method, args)
        root.privBusy = false

        if (!r || r.error) {
            var privLabel = opLabel(root.privMode === "transfer" ? "private" : root.privMode)
            if (root.handleAuthRefusal(privLabel, r)) return
            root.surfaceOpError(privLabel, r && r.error ? r.error : "unknown")
            return
        }
        if (!r.jobId) { root.logActivity("No jobId returned from module", true); return }

        var op = (root.privMode === "transfer") ? "private" : root.privMode
        root.logActivity(opLabel(op) + " started - proving (may take minutes)…", false)
        trackJob({
            jobId: r.jobId, op: op, asset: root.privAsset,
            from: from, to: (root.privToMode === "foreign" && root.privMode === "transfer")
                            ? "(foreign)" : root.privToId,
            amount: amt, state: "running", elapsedMs: 0, txId: "", error: ""
        })

        // Reset the form for the next op (the job card carries the in-flight state)
        root.privAmount = ""; root.privToId = ""
        root.privAsset = "native"; root.privTokenDef = ""; root.privTokenTicker = ""
        root.privToNpk = ""; root.privToVpk = ""; root.privToIdent = ""
    }

    function pollJobs() {
        if (typeof logos === "undefined" || !logos.callModule) { privJobsTimer.stop(); return }
        var arr = root.privJobs.slice()
        var anyRunning = false
        for (var i = 0; i < arr.length; i++) {
            if (arr[i].state !== "running") continue
            var r = callModuleParse(logos.callModule("medusa_core", "getJob", [arr[i].jobId]))
            if (!r || r.error) { anyRunning = true; continue }
            arr[i].state     = r.state || "running"
            arr[i].phase     = r.phase || arr[i].phase || "processing"
            arr[i].elapsedMs = r.elapsedMs || arr[i].elapsedMs
            if (r.state === "done")  { arr[i].txId  = r.txId || "" }
            if (r.state === "error") { arr[i].error = r.error || "failed" }
            if (arr[i].state === "running") anyRunning = true
            else onPrivJobDone(arr[i])
        }
        // Finished jobs are summarised in the job-done modal (see onPrivJobDone), so drop
        // them from the tracked array - the jobs box should now show only RUNNING jobs.
        var live = []
        for (var k = 0; k < arr.length; k++)
            if (arr[k].state === "running") live.push(arr[k])
        root.privJobs = live
        rebuildJobsModel()
        if (!anyRunning) privJobsTimer.stop()
    }

    function onPrivJobDone(j) {
        if (j.state === "done") {
            root.logActivity(opLabel(j.op) + " done"
                             + (j.txId ? " - " + j.txId.substring(0, 14) + "…" : ""), false)
        } else {
            // Connection-class / zone-mismatch failures ALSO raise the blocking zone modal
            // (it stacks above the job-done sheet - higher z), so an async op that died
            // because the zone is down is never presented as a mystery failure.
            root.surfaceOpError(opLabel(j.op), j.error || "unknown")
        }
        // Surface a one-shot completion sheet. Several jobs can finish in one poll, so
        // entries are queued and shown one after another (see jobDoneSheet).
        root.enqueueJobDone(j)
        // Private balances are only visible after a sync; the faucet/public/token side just refreshes.
        // Only private-touching ops (shield/deshield/private) change private state and need a
        // sync-private scan. A public LEZ transfer (op "send"), token send, and faucet have NO
        // private effect - they just refresh public balances. (A public transfer must never
        // trigger sync-private: there is nothing to sync, and the scan can stall / time out.)
        if (j.op === "shield" || j.op === "deshield" || j.op === "private") doSyncPrivate()
        else { root.refreshAccounts(); root.refreshTokens() }
        balanceRefreshTimer.restart()
        refreshTxHistory()
    }

    // ── Job-done modal queue ──────────────────────────────────────────────────
    // A finished job pushes one summary row here; jobDoneSheet renders the head row
    // and advanceJobDone() pops it (showing the next, or hiding the sheet).
    function enqueueJobDone(j) {
        var st = (j.state === "error") ? "error" : "done"
        jobDoneModel.append({
            op:      j.op    || "",
            asset:   j.asset || "native",
            amount:  j.amount || "",
            state:   st,
            txId:    j.txId  || "",
            error:   j.error || ""
        })
    }

    function advanceJobDone() {
        if (jobDoneModel.count > 0) jobDoneModel.remove(0)
    }

    function doSyncPrivate() {
        if (typeof logos === "undefined" || !logos.callModule) return
        // Don't sync against a zone that isn't connected - over Tor it stalls on every retry.
        if (root.seqStatus !== "running") {
            root.logActivity("Zone isn't connected yet - can't sync private balances.", true)
            return
        }
        if (root.syncBusy) return
        // Non-blocking: kick the scan in the background and poll for completion, so a slow
        // sync over Tor on a loaded box can't freeze the UI (which the host watchdog kills).
        var r = callModuleParse(logos.callModule("medusa_core", "startSyncPrivate", []))
        if (r && r.error) { root.logActivity("sync-private: " + r.error, true); return }
        root.syncBusy = true
        syncPollTimer.start()
    }

    Timer {
        id: syncPollTimer
        interval: 2500; repeat: true
        onTriggered: {
            if (typeof logos === "undefined" || !logos.callModule) { stop(); root.syncBusy = false; return }
            var s = root.callModuleParse(logos.callModule("medusa_core", "syncPrivateStatus", []))
            if (!s || !s.running) {
                stop()
                root.syncBusy = false
                if (s && s.error && String(s.error).length > 0)
                    root.logActivity("sync-private: " + s.error, true)
                root.refreshAccounts()
            }
        }
    }

    function createPrivateAccount() {
        if (typeof logos === "undefined" || !logos.callModule) return
        root.logActivity("Creating private account…", false)
        var r = callModuleParse(logos.callModule("medusa_core", "createPrivateAccount", [""]))
        if (!r || r.error) { root.logActivity("createPrivateAccount: " + (r && r.error ? r.error : "failed"), true); return }
        root.logActivity("Private account created" + (r.id ? " - " + displayId(r.id) : ""), false)
        balanceRefreshTimer.restart()
    }

    function showReceiveKeys(accountId) {
        if (typeof logos === "undefined" || !logos.callModule) return
        var r = callModuleParse(logos.callModule("medusa_core", "getAccountKeys", [accountId]))
        if (!r || r.error) { root.logActivity("getAccountKeys: " + (r && r.error ? r.error : "failed"), true); root.receiveKeys = null; return }
        root.receiveKeys = r
    }

    // ── Session password & the authorization gate ──────────────────────────────
    //
    // INVARIANT 1 - THE SESSION PASSWORD IS APPENDED IN EXACTLY ONE PLACE.
    // medusa_core gates every verb that exports a secret, moves funds or repoints the wallet
    // behind authorize(), and each of those verbs takes the session password as its TRAILING
    // argument. So call sites do NOT write root.sessionPw into an argument list: they name the
    // verb and pass everything BEFORE the password, and callGated() supplies it. That is what
    // makes the round-2 regression structurally impossible instead of merely fixed: approveZone
    // was gated in the core while its single call site kept passing one argument, so every
    // dApp-initiated zone switch failed with "wallet password required for this operation". A
    // call site cannot forget an argument it does not write. gatedVerbs mirrors the core's
    // authorize() sites (WalletPlugin.cpp: consolidateToken, sendTransfer, startSendToken,
    // startSendTransfer, startShield, startDeshield, startPrivateTransfer,
    // startPrivateTransferForeign, approveAction, approveZone, exportMnemonic, exportKey, and
    // conditionally resetWallet + restoreWallet); when the core gates one more verb, listing it
    // here is the only change any call site needs.
    readonly property var gatedVerbs: [
        "sendTransfer", "startSendTransfer", "startSendToken",
        "startShield", "startDeshield", "startPrivateTransfer", "startPrivateTransferForeign",
        "consolidateToken", "approveAction", "approveZone",
        "exportMnemonic", "exportKey",
        // Conditional in the core: gated only while a session is live, ungated while locked so
        // a forgotten password is still recoverable. Passing the (empty) session password is
        // correct in both cases.
        "resetWallet", "restoreWallet"
    ]
    // Call a gated verb. `args` is everything EXCEPT the password. Returns the parsed reply, or
    // null when the module bridge is absent (every caller already treats null as a failure).
    function callGated(verb, args) {
        if (typeof logos === "undefined" || !logos.callModule) return null
        if (root.gatedVerbs.indexOf(verb) < 0) {
            // Refuse to hand the session password to a verb that is not on the gated list: an
            // ungated verb has no password parameter, so this would be a silent arity mismatch.
            root.logActivity("internal error: " + verb + " is not a gated verb", true)
            return null
        }
        var a = (args || []).slice()
        a.push(root.sessionPw)
        return root.callModuleParse(logos.callModule("medusa_core", verb, a))
    }
    // Forget the cached password. Called on lock, on reset and on restore, and whenever the
    // core tells us the session is gone - a stale secret must never outlive its session.
    function forgetSession() {
        root.sessionPw = ""
        root.lockWarned = false
        root.idleMs = 0
    }
    // Drop the UI back to the lock screen. walletState/walletLocked are re-derived from
    // getWalletState() inside refreshAccounts(), which only re-routes while the state is NOT
    // "ready" - so leaving "ready" here is exactly what lets that routing run again.
    function toLockedScreen() {
        root.forgetSession()
        root.walletLocked = true
        root.walletState  = "locked"
        root.revealMnemonic = false; root.revealKey = false
        root.exportedMnemonic = ""; root.exportedKey = ""
        root.refreshAccounts()
    }
    // Lock now: forget the password in BOTH processes. clearSessionPassword is deliberately
    // ungated in the core (needing a password to lock would be absurd), so this always works.
    function lockWallet() {
        if (typeof logos === "undefined" || !logos.callModule) return
        logos.callModule("medusa_core", "clearSessionPassword", [])
        root.toLockedScreen()
        root.logActivity("Wallet locked", false)
    }
    // Ask the core what the store on disk is, rather than trusting a cached walletState. Used
    // wherever the UI is about to pick a verb or a screen on the strength of "encrypted or not":
    // guessing that is exactly how round 2 shipped an onboarding screen whose only button called
    // a verb the core refuses.
    function storeIsPlaintextNow() {
        if (typeof logos === "undefined" || !logos.callModule) return false
        var st = root.callModuleParse(logos.callModule("medusa_core", "getWalletState", []))
        return !!(st && st.exists === true && st.encrypted === false)
    }
    // Drop every derived UI state and let refreshAccounts() re-derive it from getWalletState().
    // refreshAccounts only re-routes while the state is NOT "ready", so "loading" is what hands
    // the routing back to the core.
    function rerouteWalletState() {
        root.forgetSession()
        root.walletLocked = false
        root.resetArmed = false
        root.revealMnemonic = false; root.revealKey = false
        root.exportedMnemonic = ""; root.exportedKey = ""
        root.walletState = "loading"
        root.refreshAccounts()
    }

    // INVARIANT 2 - EVERY REFUSAL REASON THE CORE CAN EMIT HAS A USER-FACING OUTCOME.
    // A refusal is a reply carrying BOTH `error` and `reason` (getSequencerStatus also has a
    // `reason` field, but it is a status and never an error, so it can never land here). These
    // are every reason string medusa_core emits, and what the user is left looking at:
    //
    //   locked               session cleared (idle / Lock)       → lock screen, "enter your password"
    //   unauthorized         wrong password, or the cached one
    //                        no longer matches                   → clear the core session, lock screen
    //   rate-limited         unlock/reset/restore backoff        → error toast naming the wait
    //   unencrypted          unlock(), or ANY gated verb, on an  → latch storeUnprotected (which
    //                        unencrypted store: the gate has no    disables every control that would
    //                        secret to compare there, so it        hit the same refusal) + re-derive
    //                        refuses instead of passing            state (landing on a REACHABLE
    //                                                              wallet, not a lock screen) + the
    //                                                              warning naming the migration;
    //                                                              never a password prompt, because
    //                                                              there is nothing to unlock
    //   no-wallet            unlock()/encryptPlaintextWallet with
    //                        no store at all                     → re-route to "Create your wallet"
    //   wallet-exists        createEncryptedWallet, store exists → re-route (lands on unlock)
    //   wallet-not-encrypted createEncryptedWallet, store exists
    //                        unencrypted                         → re-route + point at Security &
    //                                                              Backup's "set a password"
    //   already-encrypted    encryptPlaintextWallet on an
    //                        encrypted store                     → re-route (lands on unlock)
    //   not-created          the CLI reported success but wrote
    //                        no store                            → loud build-problem error + re-route
    //   not-encrypted        the CLI wrote an unencrypted store,
    //                        or sealed one that will not open     → loud build-problem error + re-route;
    //                                                              the core has already rolled back
    //   not-supported        setCliPath (removed)                → error toast, no state change
    //
    // A reason this build does not know can only come from a NEWER core. It is deliberately NOT
    // swallowed: it is warned about verbatim in the console and handed back to the caller, and
    // every caller of a gated verb surfaces r.error to the user on a false return.
    // Returns true when it produced the user-facing outcome, so the caller stops there.
    function handleAuthRefusal(label, r) {
        if (!r || !r.reason || !r.error) return false   // an ordinary error, not a refusal
        var why = String(r.reason)

        if (why === "locked") {
            root.toLockedScreen()
            root.logActivity(label + " needs an unlocked wallet - enter your password", true)
            return true
        }
        if (why === "unauthorized") {
            if (typeof logos !== "undefined" && logos.callModule)
                logos.callModule("medusa_core", "clearSessionPassword", [])
            root.toLockedScreen()
            root.logActivity(label + " refused: this session's password no longer matches the "
                             + "wallet - unlock again", true)
            return true
        }
        if (why === "rate-limited") {
            var wait = r.retryAfterMs
                     ? " - try again in " + Math.ceil(r.retryAfterMs / 1000) + "s" : ""
            root.logActivity(label + " refused: " + r.error + wait, true)
            return true
        }
        if (why === "unencrypted") {
            // The core reads the store's header to decide this, and a store is momentarily
            // truncated every time the CLI rewrites it, so confirm before acting on it. If it
            // really is unencrypted there is no password to ask for - the gate cannot pass on such
            // a store and unlock() will not mint a session for it - so this is never a prompt.
            // Latching storeUnprotected here is what disables the controls that would hit this
            // same refusal again (signingBlocked), and it also closes the window in which the
            // two-poll debounce below has not yet raised the warning. Then re-derive the state and
            // let the warning carry the one route out.
            if (!root.storeIsPlaintextNow()) {
                root.logActivity(label + " failed: " + r.error, true)
                return true
            }
            root.storeUnprotected = true
            root.logActivity(label + " needs an encrypted wallet, and this store has no password "
                             + "on it, so nothing can prove who is asking. Accounts, balances, the "
                             + "faucet and receiving still work. Set a password in Security & "
                             + "Backup - it keeps your accounts - then unlock, and this works.", true)
            root.rerouteWalletState()
            return true
        }
        if (why === "wallet-not-encrypted") {
            root.storeUnprotected = true
            root.logActivity("This wallet already exists and is not encrypted - open Security & "
                             + "Backup and set a password on the wallet you have, which keeps its "
                             + "accounts, instead of creating a new one", true)
            root.rerouteWalletState()
            return true
        }
        if (why === "wallet-exists" || why === "already-encrypted") {
            root.logActivity("A wallet already exists here and is encrypted - unlock it, or erase "
                             + "it and start over", true)
            root.rerouteWalletState()
            return true
        }
        if (why === "no-wallet") {
            root.logActivity("There is no wallet here yet - create one first", true)
            root.rerouteWalletState()
            return true
        }
        if (why === "not-created" || why === "not-encrypted") {
            // The CLI ran but did not produce a wallet this build can protect. The core refuses to
            // claim protection it did not deliver (and rolls a half-migration back), so neither do
            // we: name the build problem and re-derive, so whatever DID end up on disk is what the
            // user is shown, with its escapes.
            root.logActivity(label + " failed: " + r.error
                             + " - reinstall the medusa_core module, or set MEDUSA_WALLET_CLI to "
                             + "a working wallet binary before launching Basecamp", true)
            root.rerouteWalletState()
            return true
        }
        if (why === "not-supported") {
            root.logActivity(label + ": " + r.error, true)
            return true
        }
        // Unknown reason: a core newer than this UI. Say so in the log with the reason verbatim,
        // then let the caller surface r.error (and classify it for the zone-offline modal).
        console.warn("[wallet] unrecognized refusal reason from medusa_core:", why, "-", r.error)
        return false
    }
    // The core auto-locks on idle and can be locked from elsewhere, so "unlocked" is not a state
    // the UI may assume once reached: poll it. getSecurityState() runs no CLI and is NOT counted
    // as activity by the core, so this neither costs anything nor holds the session open.
    function refreshSecurityState() {
        if (typeof logos === "undefined" || !logos.callModule) return
        var s = callModuleParse(logos.callModule("medusa_core", "getSecurityState", []))
        if (!s) return
        root.autoLockMs = s.autoLockMs || 0
        root.idleMs     = s.idleMs || 0
        // Never disturb the post-create "write these words down" screen - refreshAccounts()
        // guards it the same way, and locking there would wipe the phrase mid-transcription.
        if (root.walletState === "backup") return
        // `protected` is the core's verdict and is FALSE on an unencrypted store even when a
        // password is held, because holding one there protects nothing. Raising the warning needs
        // two consecutive polls (10s apart): a store is briefly truncated on every legitimate CLI
        // write and reads as unencrypted for that instant, and a red banner flashing mid-send
        // would be a lie. Clearing it is immediate - under-warning is the failure that matters.
        //
        // One case skips the debounce, because the debounce costs more than it saves there: the
        // core says there is NO SESSION and the UI is already in "ready". The transient this
        // debounce exists for is a CLI write, and a CLI write only happens while a session is
        // live (establishSession refuses to hand one out for a plaintext store, so the two facts
        // cannot both be true of the same healthy wallet). Waiting a second poll there means up
        // to ~20s in which the UI offers a Send button whose verb is already certain to refuse.
        if (s.unencrypted === true || s["protected"] === false) {   // bracketed: reserved word
            root.plaintextSeen += 1
            if (s.hasPassword === false && root.walletState === "ready")
                root.plaintextSeen = Math.max(root.plaintextSeen, 2)
            if (root.plaintextSeen >= 2 && !root.storeUnprotected) {
                root.storeUnprotected = true
                root.protectionWarning = s.warning || ""
                root.logActivity("This wallet's storage is not encrypted: anything running as you "
                                 + "can read its keys, and sending, shielding, dApp approvals and "
                                 + "export are refused until it is. Set a password on it in "
                                 + "Security & Backup - it keeps your accounts.", true)
            }
            // NOT a lock screen and NOT a dead end: there is no password to prompt for on such a
            // store (unlock() refuses it), and the way out is the ungated migration button rather
            // than a password field. The gated verbs DO refuse here, which is what signingBlocked
            // turns into disabled controls. Returning here also keeps the hasPassword check below
            // from mistaking "no session" - which is permanent on a plaintext store, not a lapse -
            // for "auto-locked", which would bounce the user to a lock screen with nothing to type.
            return
        }
        root.plaintextSeen = 0
        if (root.storeUnprotected) { root.storeUnprotected = false; root.protectionWarning = "" }
        if (s.hasPassword === false) {
            // Sitting there looking unlocked while every gated call fails is the bug; follow it.
            if (root.walletState === "ready" || root.sessionPw.length > 0) {
                root.toLockedScreen()
                root.logActivity("Wallet auto-locked after inactivity - unlock to continue", false)
            }
            return
        }
        // The core holds a session this UI cannot prove (medusa_ui reopened while medusa_core
        // kept running). Every gated verb would refuse "unauthorized", so lock now and show a
        // clean unlock screen instead of a first send that mysteriously fails.
        if (root.walletState === "ready" && root.sessionPw.length === 0) {
            logos.callModule("medusa_core", "clearSessionPassword", [])
            root.toLockedScreen()
            root.logActivity("Session needs re-authenticating - enter your password", false)
            return
        }
        // One shot before the session lapses, so an auto-lock never lands unannounced. A
        // running job holds the session open past the budget, so only warn while time is left.
        var left = root.autoLockMs - root.idleMs
        if (root.walletState === "ready" && !root.lockWarned && root.autoLockMs > 0
                && left > 0 && left <= 90000) {
            root.lockWarned = true
            root.logActivity("Wallet locks itself in about a minute - you'll need your password again", false)
        }
    }
    // getConfig() reports the binary the core will actually run (cliPathEff), whether a STORED
    // cli-path override exists and was disowned on read (cliPathIgnored), and whether the path is
    // settable at all (cliPathConfigurable, now always false: the setting was code execution plus
    // password capture and the core no longer reads it). On an install poisoned before that
    // removal the disowned value is the only visible trace it was ever there, so Settings says it.
    function refreshCliConfig() {
        if (typeof logos === "undefined" || !logos.callModule) return
        var cfg = callModuleParse(logos.callModule("medusa_core", "getConfig", []))
        root.cliPathIgnored = !!(cfg && cfg.cliPathIgnored)
        root.cliPathEff     = (cfg && cfg.cliPathEff) ? cfg.cliPathEff : ""
    }

    // ── Security & backup helpers ──────────────────────────────────────────────
    function doUnlock(pw) {
        if (typeof logos === "undefined" || !logos.callModule) return
        runBusy("Unlocking", function() {
            var r = callModuleParse(logos.callModule("medusa_core", "unlock", [pw]))
            if (r && r.error) {
                root.forgetSession()
                // Only two reasons are ordinary outcomes of typing a password at an unlock
                // prompt, and they must stay here because the shared router would send the user
                // to the lock screen they are already standing on. EVERY other reason is a
                // statement about the STORE ("unencrypted", "no-wallet", …), so it is routed
                // rather than flattened into "Unlock failed" - that flattening is how a user ends
                // up retyping a password at a wallet that has none.
                var why = String(r.reason || "")
                if (why !== "" && why !== "unauthorized" && why !== "rate-limited"
                        && root.handleAuthRefusal("Unlock", r)) return
                var wait = (r.reason === "rate-limited" && r.retryAfterMs)
                         ? " - try again in " + Math.ceil(r.retryAfterMs / 1000) + "s" : ""
                root.logActivity("Unlock failed: " + r.error + wait, true)
                return
            }
            // Hold the verified password for the session. This is what keeps the core's gate
            // from becoming a prompt on every send: the user typed it here, once.
            root.sessionPw = pw
            root.lockWarned = false
            root.walletLocked = false
            root.freshlySealed = false
            root.logActivity("Wallet unlocked", false)
            root.refreshAccounts()
            root.refreshSecurityState()
        })
    }

    // `forRestore` only changes the WORDING: this is the same verb either way (resetWallet, which
    // renames storage.json aside with a collision-safe name and never deletes it). Step 1 of the
    // put-back flow needs exactly that displacement, and routing it through a second call site
    // would be a second place to get a destructive verb wrong.
    function doResetWallet(forRestore) {
        if (typeof logos === "undefined" || !logos.callModule) return
        var r = root.callGated("resetWallet", [])
        if (!r || r.error) {
            if (root.handleAuthRefusal(forRestore ? "Move aside" : "Reset", r)) return
            root.logActivity((forRestore ? "Could not move the current wallet aside: "
                                         : "Reset failed: ")
                             + (r && r.error ? r.error : "unknown"), true); return
        }
        root.forgetSession()                  // the store is gone; its password goes with it
        root.walletLocked = false
        root.resetArmed = false
        root.restoreAsideArmed = false
        root.walletState = "loading"          // force re-routing → "new" after the wipe
        root.exportedMnemonic = ""; root.exportedKey = ""
        root.logActivity((forRestore
                            ? "Current wallet moved aside - the slot is free for the copy"
                            : "Wallet erased - starting fresh")
                         + (r.backup ? " (previous store kept at " + r.backup + ")" : ""), false)
        root.refreshAccounts()
        root.refreshDisplacedStores()         // the store just moved aside belongs in the list
    }

    // INVARIANT 3 - THE UI NEVER GUESSES WHICH LIFECYCLE VERB APPLIES; IT ASKS THE CORE.
    // "Set a password on this wallet" is ONE user intention with TWO core verbs behind it, and
    // which one applies is a property of the store on disk, not of the screen the user is on:
    //   no store yet          → createEncryptedWallet   (refuses if a store exists)
    //   store exists, plain   → encryptPlaintextWallet  (refuses if there is no store, or it is
    //                                                    already encrypted)
    // Round 2 hard-wired the first one into a screen that only ever shows for the second, so the
    // only button a plaintext user had called a verb the core refuses. Probing getWalletState()
    // immediately before the call is what stops that from recurring, and each verb's refusal
    // reasons are routed (handleAuthRefusal) so a race between the probe and the call still ends
    // somewhere useful instead of in a dead end.
    function doSecureWallet(pw) {
        if (typeof logos === "undefined" || !logos.callModule) return
        if (!pw || pw.length === 0) { root.logActivity("Choose a password first", true); return }
        var st = callModuleParse(logos.callModule("medusa_core", "getWalletState", []))
        if (st && st.exists === true && st.encrypted === false) root.doEncryptPlaintext(pw)
        else root.doCreateEncrypted(pw)
    }

    // Adopt (or refuse to fake) a session after a verb sealed a store. Sealing a store is NOT
    // evidence of who asked for it - a caller that chose the password can always open what it just
    // wrote - so the core deliberately grants no session there and returns {locked:true}. The UI
    // must never claim one it does not have, because "looks unlocked, every gated verb refuses" is
    // the worst state this app can be in. So ask the core rather than assume either way, which
    // also means this keeps working if that policy is ever relaxed.
    function adoptSessionAfterSeal(pw) {
        var s = callModuleParse(logos.callModule("medusa_core", "getSecurityState", []))
        if (s && s.hasPassword === true) {
            root.sessionPw = pw
            root.lockWarned = false
            root.walletLocked = false
            return true
        }
        root.forgetSession()
        root.walletLocked = true
        root.walletState = "locked"
        return false
    }

    function doCreateEncrypted(pw) {
        if (typeof logos === "undefined" || !logos.callModule) return
        if (!pw || pw.length === 0) { root.logActivity("Choose a password first", true); return }
        runBusy("Creating", function() {
            var r = callModuleParse(logos.callModule("medusa_core", "createEncryptedWallet", [pw]))
            if (!r || r.error) {
                if (root.handleAuthRefusal("Create wallet", r)) return
                root.logActivity("Create failed: " + (r && r.error ? r.error : "unknown"), true); return
            }
            var live = root.adoptSessionAfterSeal(pw)
            root.freshlySealed = !live
            root.logActivity("Encrypted wallet created"
                             + (live ? "" : " - " + (r.note || "unlock with the password you just chose")), false)
            if (r.mnemonic) {
                root.exportedMnemonic = r.mnemonic; root.revealMnemonic = true
                root.walletState = "backup"          // show the phrase before entering the wallet
            } else if (live) {
                root.walletState = "ready"; root.refreshAccounts()
            } else {
                root.refreshAccounts()               // stays on the lock screen adoptSessionAfterSeal set
            }
        })
    }

    // Encrypt an existing UNENCRYPTED store in place, keeping its accounts. This is the verb the
    // "set a password" button needs; createEncryptedWallet refuses here (reason
    // "wallet-not-encrypted") because re-sealing someone's existing wallet as if it were a new one
    // is how an attacker locks its owner out. A wallet saved this way never held a recovery
    // phrase, so there is no backup screen - the core copies the unencrypted store aside, names
    // it, and puts it back untouched if the sealed store does not open.
    function doEncryptPlaintext(pw) {
        if (typeof logos === "undefined" || !logos.callModule) return
        if (!pw || pw.length === 0) { root.logActivity("Choose a password first", true); return }
        runBusy("Encrypting", function() {
            var r = callModuleParse(logos.callModule("medusa_core", "encryptPlaintextWallet", [pw]))
            if (!r || r.error) {
                if (root.handleAuthRefusal("Set password", r)) return
                root.logActivity("Set password failed: " + (r && r.error ? r.error : "unknown"), true); return
            }
            var live = root.adoptSessionAfterSeal(pw)
            root.freshlySealed = !live
            root.exportedMnemonic = ""; root.revealMnemonic = false
            root.storeUnprotected = false; root.protectionWarning = ""; root.plaintextSeen = 0
            root.logActivity("Wallet encrypted"
                             + (r.backup ? " (unencrypted copy kept at " + r.backup + ")" : "")
                             + (live ? "" : " - " + (r.note || "unlock with the password you just chose")), false)
            if (live) root.walletState = "ready"
            root.refreshAccounts()
        })
    }

    function finishBackup() {
        root.revealMnemonic = false
        // Never walk out of the backup screen into a wallet the core is not holding a session
        // for: if the seal granted no session, adoptSessionAfterSeal already chose the lock
        // screen, and "ready" here would be the "looks unlocked, nothing works" state.
        if (root.sessionPw.length === 0) { root.walletState = "locked"; root.walletLocked = true }
        else root.walletState = "ready"
        root.refreshAccounts()
    }

    function doExportMnemonic() {
        if (typeof logos === "undefined" || !logos.callModule) return
        runBusy("Exporting", function() {
            var r = root.callGated("exportMnemonic", [])
            if (!r || r.error) {
                if (root.handleAuthRefusal("Reveal phrase", r)) return
                root.logActivity("Reveal phrase: " + (r && r.error ? r.error : "failed"), true); return
            }
            root.exportedMnemonic = r.mnemonic || ""
            root.revealMnemonic = true
        })
    }

    function doExportKey() {
        if (typeof logos === "undefined" || !logos.callModule) return
        if (root.selectedFromId.length === 0) { root.logActivity("Select an account first", true); return }
        if (root.selectedFromType !== "public") { root.logActivity("Key export is for public accounts", true); return }
        runBusy("Exporting", function() {
            var r = root.callGated("exportKey", [root.selectedFromId])
            if (!r || r.error) {
                if (root.handleAuthRefusal("Export key", r)) return
                root.logActivity("Export key: " + (r && r.error ? r.error : "failed"), true); return
            }
            root.exportedKey = r.privateKey || ""
            root.revealKey = true
        })
    }

    function doRestore(phrase, pw, depth) {
        if (typeof logos === "undefined" || !logos.callModule) return
        if (!phrase || phrase.trim().split(/\s+/).length < 12) { root.logActivity("Enter a valid recovery phrase", true); return }
        runBusy("Restoring", function() {
            // The TRAILING argument is the CURRENT session password, not the new one - callGated
            // appends it. The core only demands it when a session is live, so restoring from the
            // lock screen still works with an empty one.
            var r = root.callGated("restoreWallet", [phrase.trim(), pw, depth])
            if (!r || r.error) {
                if (root.handleAuthRefusal("Restore", r)) return
                root.logActivity("Restore failed: " + (r && r.error ? r.error : "unknown"), true); return
            }
            // The old store is gone, so the password that opened it must not linger. Whether the
            // NEW one is a live session is the core's call: a restore run from the lock screen
            // deliberately stays locked (the caller chose that password, so keeping it would be a
            // way to mint a session), while a restore run from a proven session carries over.
            root.forgetSession()
            var live = root.adoptSessionAfterSeal(pw)
            root.logActivity("Wallet restored from recovery phrase"
                             + (r.backup ? " (previous store kept at " + r.backup + ")" : "")
                             + (live ? "" : " - unlock with the password you just set"), false)
            root.refreshAccounts()
        })
    }

    function doImportKey(key, label) {
        if (typeof logos === "undefined" || !logos.callModule) return
        if (!key || key.trim().length === 0) { root.logActivity("Enter a private key", true); return }
        runBusy("Importing", function() {
            var r = callModuleParse(logos.callModule("medusa_core", "importKey", [key.trim(), label || ""]))
            if (!r || r.error) { root.logActivity("Import failed: " + (r && r.error ? r.error : "unknown"), true); return }
            root.logActivity("Imported account" + (r.id ? " " + displayId(r.id) : ""), false)
            balanceRefreshTimer.restart()
        })
    }

    // Run a BLOCKING synchronous backend op behind the busy veil. Setting the label then
    // running the call in the same tick never paints (the thread is blocked) - so the UI
    // looked frozen. Here we set the label, let a frame land (the 40ms timer), THEN run the
    // work, so the user sees "<label>…" on the existing secBusy overlay while it loads.
    function runBusy(label, fn) {
        root.secBusy = label
        busyRunTimer.fn = fn
        busyRunTimer.restart()
    }
    Timer {
        id: busyRunTimer; interval: 40; repeat: false
        property var fn: null
        onTriggered: { var f = fn; fn = null; try { if (f) f() } finally { root.secBusy = "" } }
    }

    // ── Timers ────────────────────────────────────────────────────────────────
    Timer {
        interval: 10000; running: true; repeat: true
        onTriggered: {
            if (root.pollBusy) return
            root.pollBusy = true
            root.refreshStatus()
            root.refreshSeqStatus()
            root.refreshAccounts()
            root.refreshSecurityState()   // the core auto-locks on idle - follow it back
            root.pollBusy = false
        }
    }

    Timer {
        id: balanceRefreshTimer
        interval: 3000; onTriggered: root.refreshAccounts()
    }

    Timer { id: noticeTimer; interval: 4500; onTriggered: root.notice = "" }

    // After a network switch: wait for the new sequencer to come up, then reload balances.
    Timer {
        id: netReloadTimer
        interval: 6000
        onTriggered: { root.refreshSeqStatus(); root.refreshAccounts(); root.refreshTokens() }
    }

    // Polls in-flight privacy jobs; stops itself once none are running.
    Timer {
        id: privJobsTimer
        interval: 3000; repeat: true; running: false
        onTriggered: root.pollJobs()
    }

    // Polls Medusa-Connect pending requests; the head request surfaces as a modal sheet.
    Timer {
        id: connPollTimer
        interval: 1200; repeat: true; running: true
        onTriggered: root.pollConnRequests()
    }

    // Polls health while the offline modal's Retry is in flight; closes it on success.
    // Bounded (8 rounds ≈ 12s: covers the async probe's 8s budget) so "Checking…" can't
    // spin forever against a zone that stays down.
    Timer {
        id: zoneRetryTimer
        interval: 1500; repeat: true
        property int tries: 0
        onTriggered: {
            root.refreshSeqStatus()
            if (root.seqStatus === "running" && root.seqProblem === "") {
                stop(); root.zoneRetryBusy = false; root.zoneOfflineOpen = false
                root.logActivity("Zone connection restored", false)
            } else if (++tries >= 8) {
                stop(); root.zoneRetryBusy = false
            }
        }
    }

    Component.onCompleted: {
        if (typeof logos === "undefined" || !logos.callModule) return
        root.refreshCliConfig()
        var scfg = callModuleParse(logos.callModule("medusa_core", "getSequencerConfig", []))
        if (scfg) {
            root.seqPort = scfg.port || 3071
            if (scfg.network) root.network = scfg.network
        }
        root.refreshStatus()
        root.refreshSeqStatus()
        root.refreshZones()
        root.refreshAccounts()
        root.refreshSecurityState()   // after refreshAccounts: it judges the routed walletState
        root.refreshWhitelist()
        root.checkForUpdate()
        if (!root.cliFound) root.screen = "settings"
    }

    // The disowned-CLI-path notice is only interesting on the Settings screen, and the list of
    // displaced stores only on Security & Backup; re-read each there rather than paying for a
    // getConfig / getWalletState on every status poll.
    onScreenChanged: {
        if (root.screen === "settings") root.refreshCliConfig()
        if (root.screen === "security") root.refreshDisplacedStores()
        else { root.restoreCandidate = ""; root.restoreAsideArmed = false }   // never leave a
        // destructive two-tap armed on a screen the user walked away from
    }

    onSelectedFromIdChanged: {
        root.txHistory = []
        txHistoryModel.clear()
        toField.text = ""
        amountField.text = ""
        root.privToId = ""
        root.receiveKeys = null
        root.refreshTxHistory()
    }

    TextEdit { id: clipHelper; visible: false }

    // ── Root layout ───────────────────────────────────────────────────────────
    ColumnLayout {
        id: appBody   // the screen-content subtree - used as the backdrop-blur source for modal sheets
        anchors { fill: parent; margins: 12 }
        spacing: 10

        // ── Top bar (MetaMask-style: network · account · icons) ─────────────────
        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Image {
                source: "icons/medusa-logo.png"
                Layout.preferredWidth: 52; Layout.preferredHeight: 52
                fillMode: Image.PreserveAspectFit
                smooth: true; mipmap: true
            }

            // Network selector pill - status dot + network label, opens the Network screen
            Rectangle {
                height: 28; implicitWidth: netRow.implicitWidth + 18; radius: 14
                color: root.selectBg; border.color: root.screen === "network" ? root.accentOrange : root.borderColor; border.width: 1
                RowLayout {
                    id: netRow
                    anchors { left: parent.left; leftMargin: 9; verticalCenter: parent.verticalCenter }
                    spacing: 6
                    Rectangle {
                        width: 8; height: 8; radius: 4; Layout.alignment: Qt.AlignVCenter
                        // one indicator, colour only changes: green=connected, gray=connecting, red=down
                        color: root.seqStatus === "running"  ? root.greenBright
                             : root.seqStatus === "starting" ? root.connectGray : root.errorRed
                        SequentialAnimation on opacity {
                            running: root.seqStatus === "starting"; loops: Animation.Infinite
                            NumberAnimation { to: 0.3; duration: 500 }
                            NumberAnimation { to: 1.0; duration: 500 }
                        }
                    }
                    Text { font.family: root.faceFont; font.pixelSize: 11; color: root.textPrimary
                        text: root.zoneName(root.network) }
                    Text { font.family: root.faceFont; font.pixelSize: 10; color: root.connectGray
                        visible: root.seqStatus === "starting"; text: "· Connecting…" }
                    Text { font.family: root.faceFont; font.pixelSize: 9; color: root.textDisabled; text: "▾" }
                }
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.screen = (root.screen === "network" ? "main" : "network") }
            }

            Item { Layout.fillWidth: true }

            // Account selector - avatar + short id + ▾, opens the Accounts screen
            Rectangle {
                visible: root.walletState === "ready"
                height: 30; implicitWidth: acctRow.implicitWidth + 18; radius: 15
                color: root.screen === "accounts" ? root.accentTint10 : root.selectBg
                border.color: root.screen === "accounts" ? root.accentOrange : root.borderColor; border.width: 1
                RowLayout {
                    id: acctRow
                    anchors { left: parent.left; leftMargin: 9; verticalCenter: parent.verticalCenter }
                    spacing: 6
                    Rectangle {   // identicon-ish avatar derived from the id
                        width: 16; height: 16; radius: 8; Layout.alignment: Qt.AlignVCenter
                        color: root.avatarColor(root.selectedFromId)
                        border.color: root.selectedFromType === "private" ? root.accentOrange : root.borderColor
                    }
                    Text { font.family: root.faceFont; font.pixelSize: 11; color: root.textPrimary
                        text: root.selectedFromId.length > 0 ? root.selectedAcctName() : "No account" }
                    Text { font.family: root.faceFont; font.pixelSize: 9; color: root.textDisabled; text: "▾" }
                }
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.screen = (root.screen === "accounts" ? "main" : "accounts") }
            }

            // Lock now - forget the session password in BOTH processes. The core also drops it
            // by itself after an idle timeout; this is the manual door. Only shown while
            // unlocked, so it never clashes with the key icon's locked 🔒 expression.
            Rectangle {
                visible: root.walletState === "ready"
                width: 30; height: 30; radius: 15; color: "transparent"
                border.color: lockBtnMa.containsMouse ? root.brandRedHover : root.brandRed
                border.width: 1
                Text { font.family: root.faceFont; anchors.centerIn: parent; text: "🔒"; font.pixelSize: 13
                    color: lockBtnMa.containsMouse ? root.brandRedHover : root.brandRed }
                MouseArea { id: lockBtnMa; anchors.fill: parent; hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor; onClicked: root.lockWallet() }
            }

            // Security & backup (lock / key icon) - crimson idle/active, red while locked.
            Rectangle {
                width: 30; height: 30; radius: 15; color: "transparent"
                border.color: root.screen === "security" ? root.brandRedHover
                            : root.walletLocked ? root.errorRed : root.brandRed
                border.width: 1
                // Locked: reliable 🔒 emoji (tinted by font colour). Unlocked: a real key SVG,
                // colourized to the SAME crimson/active expression (exotic key glyphs tofu'd).
                Text { font.family: root.faceFont;
                    visible: root.walletLocked
                    anchors.centerIn: parent; text: "🔒"; font.pixelSize: 14
                    color: root.screen === "security" ? root.brandRedHover : root.errorRed
                }
                Image {
                    visible: !root.walletLocked
                    source: "icons/key.svg"
                    sourceSize.width: 16; sourceSize.height: 16
                    anchors.centerIn: parent
                    layer.enabled: true
                    layer.effect: MultiEffect {
                        colorization: 1.0
                        colorizationColor: root.screen === "security" ? root.brandRedHover : root.brandRed
                    }
                }
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.screen = (root.screen === "security" ? "main" : "security") }
            }

            // Settings (cog) - crimson idle, brighter crimson when active.
            Rectangle {
                width: 30; height: 30; radius: 15; color: "transparent"
                border.color: root.screen === "settings" ? root.brandRedHover : root.brandRed; border.width: 1
                Text { font.family: root.faceFont; anchors.centerIn: parent; text: "⚙"; font.pixelSize: 14; color: root.screen === "settings" ? root.brandRedHover : root.brandRed }
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.screen = (root.screen === "settings" ? "main" : "settings") }
            }
        }

        // ── Unencrypted-storage banner ──────────────────────────────────────────
        // The wallet is REACHABLE in this state and the way out is one ungated button, so this is
        // a warning and not a lockout - but it is not "everything works" either: every gated verb
        // refuses here, so the banner names the consequence next to the risk instead of leaving
        // the user to discover it on a failed send. It is keyed on the core's own verdict and
        // carries the core's own wording, so the UI can never claim a protection the core says
        // does not exist, or the reverse.
        Rectangle {
            visible: root.storeUnprotected && root.walletState !== "loading"
                     && root.walletState !== "backup" && !root.escapeScreenOpen
            Layout.fillWidth: true
            implicitHeight: unprotRow.implicitHeight + 16
            radius: 10
            color: root.errorTint
            border.color: root.warningAmber; border.width: 1
            RowLayout {
                id: unprotRow
                anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter; leftMargin: 12; rightMargin: 12 }
                spacing: 10
                Text { text: "⚠"; color: root.warningAmber; font.pixelSize: 14; Layout.alignment: Qt.AlignTop; Layout.topMargin: 1 }
                ColumnLayout {
                    Layout.fillWidth: true; spacing: 2
                    Text { Layout.fillWidth: true; font.family: root.faceFont; font.pixelSize: 11; font.bold: true
                        color: root.textPrimary; elide: Text.ElideRight; text: "This wallet is not encrypted" }
                    Text { Layout.fillWidth: true; wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                        font.family: root.faceFont; font.pixelSize: 9; color: root.textSecondary
                        maximumLineCount: 4; elide: Text.ElideRight
                        text: (root.protectionWarning.length > 0 ? root.protectionWarning
                                : "Every key in its storage can be read by any program running as you.")
                            + " Sending, shielding, dApp approvals and key export are refused until "
                            + "you set a password: it encrypts the store in place and keeps your "
                            + "accounts." }
                }
                Rectangle {
                    Layout.preferredWidth: 104; Layout.preferredHeight: 26; radius: 8
                    color: unprotMa.containsMouse ? root.hoverWash : "transparent"
                    border.color: root.warningAmber; border.width: 1
                    Text { anchors.centerIn: parent; text: "Set a password"; color: root.textPrimary
                        font.family: root.faceFont; font.pixelSize: 10 }
                    MouseArea { id: unprotMa; anchors.fill: parent; hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor; onClicked: root.screen = "security" }
                }
            }
        }

        // ── Local-sequencer problem banner (right under the zone pill) ───────────
        // Persistent surface for a LOCAL zone whose plugin-managed sequencer is dead or
        // incompatible: crashed / failed to launch / failing health checks / build
        // mismatch. binary-missing and tor-missing keep the existing bottom prereq
        // disclaimer (same facts, richer install advice) - no double banner for those.
        Rectangle {
            visible: root.walletState === "ready" && root.seqProblem !== ""
                     && root.seqProblem !== "binary-missing" && root.seqProblem !== "tor-missing"
            Layout.fillWidth: true
            implicitHeight: seqProbRow.implicitHeight + 16
            radius: 10
            color: root.errorTint
            border.color: root.errorRed; border.width: 1
            RowLayout {
                id: seqProbRow
                anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter; leftMargin: 12; rightMargin: 12 }
                spacing: 10
                Text { text: "⚠"; color: root.errorRed; font.pixelSize: 14; Layout.alignment: Qt.AlignTop; Layout.topMargin: 1 }
                ColumnLayout {
                    Layout.fillWidth: true; spacing: 2
                    Text { Layout.fillWidth: true; font.family: root.faceFont; font.pixelSize: 11; font.bold: true
                        color: root.textPrimary; elide: Text.ElideRight; text: root.seqProblemTitle() }
                    Text { Layout.fillWidth: true; wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                        font.family: root.faceFont; font.pixelSize: 9; color: root.textSecondary
                        maximumLineCount: 3; elide: Text.ElideRight; text: root.seqProblemBody() }
                }
                Rectangle {
                    Layout.preferredWidth: 92; Layout.preferredHeight: 26; radius: 8
                    color: seqProbZoneMa.containsMouse ? root.hoverWash : "transparent"
                    border.color: root.borderStrong; border.width: 1
                    Text { anchors.centerIn: parent; text: "Switch zone"; color: root.textPrimary
                        font.family: root.faceFont; font.pixelSize: 10 }
                    MouseArea { id: seqProbZoneMa; anchors.fill: parent; hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor; onClicked: root.screen = "network" }
                }
            }
        }

        // ── Security & Backup panel ─────────────────────────────────────────────
        Rectangle {
            Layout.fillWidth: true
            // Backup / export / recovery screen. Reachable in EVERY state its key icon is (which
            // is every state): it is where a locked user restores from a recovery phrase or erases
            // and starts over, and where an unencrypted wallet gets a password. Gating it on
            // "ready" is what left those users looking at a screen full of nothing. The backup
            // screen still owns the window - an unwritten recovery phrase is never covered.
            visible: root.screen === "security" && root.walletState !== "loading"
                     && root.walletState !== "backup"
            Layout.fillHeight: true
            color: root.panelColor
            border.color: root.walletLocked ? root.errorRed : root.borderColor; border.width: 1; radius: 12
            clip: true

            Flickable {
                anchors { fill: parent; margins: 10 }
                contentWidth: width; contentHeight: secInner.implicitHeight; clip: true
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            ColumnLayout {
                id: secInner
                width: parent.width
                spacing: 8

                RowLayout {   // back header
                    Layout.fillWidth: true; spacing: 6
                    Rectangle { width: 26; height: 24; radius: 10; color: "transparent"; border.color: root.borderColor
                        Text { anchors.centerIn: parent; text: "←"; color: root.textSecondary; font.pixelSize: 13; font.family: root.faceFont }
                        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.screen = "main" } }
                    Text { font.family: root.faceFont; text: "Security & Backup"; color: root.textPrimary; font.pixelSize: 13; font.bold: true }
                    Item { Layout.fillWidth: true }
                }

                // ── Wallet storage this module moved or copied aside, and how to put it back ──
                // Reset, Restore and the migration all displace the previous store instead of
                // deleting it, and until the core reported them nothing on screen did. "My wallet
                // vanished and Reset buried it deeper" was the result, so the paths are named in
                // every state - most of all in "new", where the wallet looks gone.
                //
                // Naming them is not enough on its own. A user gets here without asking for it:
                // their own Erase, a migration that rolled back, or a co-resident caller invoking
                // the ungated encryptPlaintextWallet (which seals a legacy plaintext wallet under
                // a password only that caller knows) or resetWallet. Such a user was shown the
                // path to their money and given no control at all. So each entry now says WHAT it
                // is and opens a confirmed put-back flow.
                //
                // The flow is split into a step Medusa can take and a step it cannot, and it is
                // honest about which is which: medusa_core can move the store that is in place
                // aside (resetWallet, never deletes, collision-safe name) but exposes no verb that
                // puts a store back, and a QML module has no filesystem of its own - so the copy
                // itself is handed over as an exact `cp -n` command. Both halves are safe by
                // construction: nothing is deleted, and `-n` refuses rather than overwrites, so
                // neither step can destroy the store that is currently in place.
                ColumnLayout {
                    visible: root.displacedStores.length > 0
                    Layout.fillWidth: true; spacing: 4
                    Text { font.family: root.faceFont;
                        text: "Previous wallet storage kept on disk"
                        color: root.textSecondary; font.pixelSize: 10; font.bold: true
                        wrapMode: Text.WordWrap; Layout.fillWidth: true
                    }
                    Text { font.family: root.faceFont;
                        text: "Nothing here is ever deleted. If the wallet in front of you is not "
                            + "the one you expect, one of these is probably it."
                        color: root.textDisabled; font.pixelSize: 9; wrapMode: Text.WordWrap; Layout.fillWidth: true
                    }
                    Repeater {
                        model: root.displacedStores
                        delegate: ColumnLayout {
                            id: dsRow
                            required property string modelData
                            Layout.fillWidth: true; spacing: 3
                            readonly property bool open: root.restoreCandidate === dsRow.modelData

                            RowLayout {
                                Layout.fillWidth: true; spacing: 6
                                Text { font.family: root.faceFont;
                                    text: dsRow.modelData; color: root.textDisabled; font.pixelSize: 9
                                    elide: Text.ElideMiddle; Layout.fillWidth: true
                                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                                        onClicked: { clipHelper.text = dsRow.modelData; clipHelper.selectAll()
                                                     clipHelper.copy(); root.logActivity("Path copied", false) } }
                                }
                                Rectangle {
                                    Layout.preferredWidth: 74; height: 22; radius: 10
                                    color: "transparent"; border.color: root.accentOrange
                                    Text { font.family: root.faceFont; anchors.centerIn: parent
                                        text: dsRow.open ? "Close" : "Put back"
                                        color: root.accentOrange; font.pixelSize: 9 }
                                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                                        onClicked: {
                                            root.restoreAsideArmed = false
                                            root.restoreCandidate = dsRow.open ? "" : dsRow.modelData
                                            if (root.restoreCandidate.length > 0) root.refreshDisplacedStores()
                                        } }
                                }
                            }

                            // ── The confirmed put-back flow for THIS file ──
                            ColumnLayout {
                                visible: dsRow.open
                                Layout.fillWidth: true; Layout.leftMargin: 8; spacing: 4

                                Text { font.family: root.faceFont;
                                    text: root.displacedWhat(dsRow.modelData)
                                    color: root.textSecondary; font.pixelSize: 9
                                    wrapMode: Text.WordWrap; Layout.fillWidth: true
                                }

                                // Step 1 - free the slot, using the core's own non-destructive move.
                                Text { font.family: root.faceFont;
                                    text: root.storeInPlace
                                        ? "1. A wallet is in place right now. Move it aside first: it is renamed, "
                                          + "never deleted, and it appears in this list too, so this is reversible."
                                        : "1. Done - no wallet is in place, so the slot is free."
                                    color: root.storeInPlace ? root.textSecondary : root.textDisabled
                                    font.pixelSize: 9; wrapMode: Text.WordWrap; Layout.fillWidth: true
                                }
                                Rectangle {
                                    visible: root.storeInPlace
                                    Layout.preferredWidth: 188; height: 22; radius: 10
                                    color: "transparent"; border.color: root.errorRed
                                    Text { font.family: root.faceFont; anchors.centerIn: parent
                                        text: root.restoreAsideArmed ? "Tap again to move it aside"
                                                                     : "Move the current wallet aside"
                                        color: root.errorRed; font.pixelSize: 9 }
                                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                                        onClicked: { if (root.restoreAsideArmed) root.doResetWallet(true)
                                                     else root.restoreAsideArmed = true } }
                                }

                                // Step 2 - the copy. Handed over verbatim, not faked with a button.
                                Text { font.family: root.faceFont;
                                    text: "2. Copy the file back. Medusa moves stores aside but has no verb that "
                                        + "puts one back, so this step is yours. `cp -n` refuses rather than "
                                        + "overwrites, so it cannot destroy anything:"
                                    color: root.textSecondary; font.pixelSize: 9
                                    wrapMode: Text.WordWrap; Layout.fillWidth: true
                                }
                                Rectangle {
                                    Layout.fillWidth: true
                                    implicitHeight: dsCmd.implicitHeight + 10
                                    color: root.inputBg; border.color: root.borderColor; radius: 8
                                    Text { font.family: root.monoFont
                                        id: dsCmd
                                        anchors { left: parent.left; right: parent.right
                                                  verticalCenter: parent.verticalCenter
                                                  leftMargin: 6; rightMargin: 6 }
                                        text: root.restoreCommandFor(dsRow.modelData)
                                        color: root.textPrimary; font.pixelSize: 9
                                        wrapMode: Text.WrapAnywhere
                                    }
                                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                                        onClicked: { clipHelper.text = root.restoreCommandFor(dsRow.modelData)
                                                     clipHelper.selectAll(); clipHelper.copy()
                                                     root.logActivity("Command copied - run it in a terminal", false) } }
                                }

                                // Step 3 - re-ask the core instead of assuming it worked.
                                RowLayout {
                                    Layout.fillWidth: true; spacing: 6
                                    Text { font.family: root.faceFont;
                                        text: "3. Then check again. This re-reads the wallet from disk and drops "
                                            + "any session, so an encrypted store will ask for its password; an "
                                            + "unencrypted one warns and offers to encrypt itself."
                                        color: root.textSecondary; font.pixelSize: 9
                                        wrapMode: Text.WordWrap; Layout.fillWidth: true
                                    }
                                    Rectangle {
                                        Layout.preferredWidth: 84; height: 22; radius: 10
                                        color: "transparent"; border.color: root.accentOrange
                                        Text { font.family: root.faceFont; anchors.centerIn: parent
                                            text: "Check again"; color: root.accentOrange; font.pixelSize: 9 }
                                        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                                            onClicked: root.recheckAfterRestore() }
                                    }
                                }

                                // The in-app alternative that needs no file copy at all. Only an
                                // UNENCRYPTED copy can be read this way, which is exactly the file
                                // an ungated encryptPlaintextWallet leaves its victim with.
                                Text { font.family: root.faceFont;
                                    visible: root.displacedKind(dsRow.modelData) === "plain"
                                    text: "Or, without copying anything: it is unencrypted, so open it in a text "
                                        + "editor and paste each account's private key into \"Import a private "
                                        + "key\", which appears in this panel once a wallet is in place."
                                    color: root.textDisabled; font.pixelSize: 9
                                    wrapMode: Text.WordWrap; Layout.fillWidth: true
                                }
                                Text { font.family: root.faceFont;
                                    visible: root.displacedKind(dsRow.modelData) === "bak"
                                    text: "Or, without copying anything: if you have that wallet's recovery phrase, "
                                        + "\"Restore from recovery phrase\" rebuilds the same accounts from it."
                                    color: root.textDisabled; font.pixelSize: 9
                                    wrapMode: Text.WordWrap; Layout.fillWidth: true
                                }
                            }
                        }
                    }
                }

                // ── No wallet yet ──
                ColumnLayout {
                    visible: root.walletState === "new"
                    Layout.fillWidth: true; spacing: 6
                    Text { font.family: root.faceFont; text: "No wallet yet"; color: root.textPrimary; font.pixelSize: 12; font.bold: true }
                    Text { font.family: root.faceFont;
                        text: "Go back and choose a password to create one. If you already have a "
                            + "recovery phrase, create a wallet first and then restore over it here."
                        color: root.textSecondary; font.pixelSize: 10; wrapMode: Text.WordWrap; Layout.fillWidth: true
                    }
                    // Do not send someone who is halfway through putting an old store back off to
                    // create a new one: creating destroys nothing, but it re-fills the slot they
                    // just cleared and they would have to clear it again.
                    Text { font.family: root.faceFont;
                        visible: root.restoreCandidate.length > 0
                        text: "You are in the middle of putting a previous store back (above): the "
                            + "slot is free, so run the copy command and press Check again rather "
                            + "than creating a new wallet here."
                        color: root.warningAmber; font.pixelSize: 9; wrapMode: Text.WordWrap; Layout.fillWidth: true
                    }
                    Rectangle {
                        Layout.preferredWidth: 124; height: 24; radius: 10; color: "transparent"; border.color: root.brandRed
                        Text { font.family: root.faceFont; anchors.centerIn: parent; text: "Create a wallet"; color: root.brandRed; font.pixelSize: 10 }
                        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.screen = "main" }
                    }
                }

                // ── The store has no password on it ──
                // Keyed on the CORE's verdict (getSecurityState.protected === false), not on a UI
                // state, so it shows wherever that is true - including a wallet the user can
                // otherwise reach, which is exactly what an unencrypted store is. Round 2 blocked
                // this user instead and left them with no working button, so the wallet stays
                // reachable; but the gate refuses every gated verb here, so the controls behind
                // those verbs are disabled further down and THIS block is the one that turns them
                // back on. From here the user can set a password on the wallet they have (keeping
                // its accounts), restore over it from a phrase, or erase it and start again.
                ColumnLayout {
                    visible: root.storeUnprotected
                    Layout.fillWidth: true; spacing: 6
                    Text { font.family: root.faceFont; text: "⚠ This wallet is not encrypted"; color: root.warningAmber; font.pixelSize: 12; font.bold: true }
                    Text { font.family: root.faceFont;
                        text: (root.protectionWarning.length > 0 ? root.protectionWarning
                                : "Its storage has no password on it, so every key in it can be read "
                                + "by any program running as you, and no password can change that.")
                            + " Nothing can prove who is asking on a store like this, so sending, "
                            + "shielding, dApp approvals and revealing a key or recovery phrase are "
                            + "all refused until you set one. Setting one encrypts the store in "
                            + "place, keeps its accounts, and you unlock with it afterwards."
                        color: root.textSecondary; font.pixelSize: 10; wrapMode: Text.WordWrap; Layout.fillWidth: true
                    }
                    RowLayout {
                        Layout.fillWidth: true; spacing: 6
                        Rectangle {
                            Layout.fillWidth: true; height: 28; color: root.inputBg; border.color: root.borderColor; radius: 8
                            TextInput { font.family: root.faceFont;
                                id: secEncryptPw
                                anchors { fill: parent; leftMargin: 6; rightMargin: 6 }
                                verticalAlignment: TextInput.AlignVCenter; echoMode: TextInput.Password
                                color: root.textPrimary; font.pixelSize: 11; clip: true
                                onAccepted: root.doSecureWallet(text)
                                Text { font.family: root.faceFont;
                                    anchors.fill: parent; verticalAlignment: Text.AlignVCenter
                                    text: parent.text.length === 0 ? "choose a password" : ""
                                    color: root.textDisabled; font.pixelSize: 11
                                }
                            }
                        }
                        Rectangle {
                            Layout.preferredWidth: 84; height: 28; radius: 10
                            color: secEncMa.pressed ? root.brandRedPressed
                                 : secEncMa.containsMouse ? root.brandRedHover : root.brandRed
                            border.color: root.brandRed
                            Behavior on color { ColorAnimation { duration: root.motionQuick } }
                            Text { font.family: root.faceFont; anchors.centerIn: parent
                                text: root.secBusy === "Encrypting" ? "…" : "Set password"
                                color: root.textPrimary; font.pixelSize: 11 }
                            MouseArea { id: secEncMa; anchors.fill: parent; hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor; onClicked: root.doSecureWallet(secEncryptPw.text) }
                        }
                    }
                    Text { font.family: root.faceFont;
                        text: "The unencrypted store is copied aside first and put back untouched if "
                            + "the encrypted one will not open, so this is reversible. A wallet saved "
                            + "this way never held a recovery phrase, so none is shown - back it up "
                            + "by exporting each account's key. You unlock with this password after."
                        color: root.textDisabled; font.pixelSize: 9; wrapMode: Text.WordWrap; Layout.fillWidth: true
                    }
                    RowLayout {
                        Layout.fillWidth: true; spacing: 6
                        Text { font.family: root.faceFont;
                            text: "Not your wallet, or you would rather start clean?"
                            color: root.textDisabled; font.pixelSize: 9; wrapMode: Text.WordWrap; Layout.fillWidth: true
                        }
                        Rectangle {
                            Layout.preferredWidth: 124; height: 22; radius: 10; color: "transparent"; border.color: root.errorRed
                            Text { font.family: root.faceFont;
                                anchors.centerIn: parent
                                text: root.resetArmed ? "Tap again to erase" : "Erase & start over"
                                color: root.errorRed; font.pixelSize: 9
                            }
                            MouseArea {
                                anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                                onClicked: { if (root.resetArmed) root.doResetWallet(); else root.resetArmed = true }
                            }
                        }
                    }
                }

                // ── Locked: unlock ──
                ColumnLayout {
                    visible: root.walletState === "locked"
                    Layout.fillWidth: true; spacing: 6
                    Text { font.family: root.faceFont; text: "🔒 Wallet is locked"; color: root.errorRed; font.pixelSize: 12; font.bold: true }
                    Text { font.family: root.faceFont;
                        text: "Enter your password to unlock the encrypted wallet."
                        color: root.textSecondary; font.pixelSize: 10; wrapMode: Text.WordWrap; Layout.fillWidth: true
                    }
                    RowLayout {
                        Layout.fillWidth: true; spacing: 6
                        Rectangle {
                            Layout.fillWidth: true; height: 28; color: root.inputBg; border.color: root.borderColor; radius: 8
                            TextInput { font.family: root.faceFont;
                                id: unlockField
                                anchors { fill: parent; leftMargin: 6; rightMargin: 6 }
                                verticalAlignment: TextInput.AlignVCenter; echoMode: TextInput.Password
                                color: root.textPrimary; font.pixelSize: 11; clip: true
                                onAccepted: root.doUnlock(text)
                            }
                        }
                        Rectangle {
                            Layout.preferredWidth: 74; height: 28; radius: 10
                            color: secUnlockMa.pressed ? root.brandRedPressed
                                 : secUnlockMa.containsMouse ? root.brandRedHover : root.brandRed
                            border.color: root.brandRed
                            Behavior on color { ColorAnimation { duration: root.motionQuick } }
                            Text { font.family: root.faceFont; anchors.centerIn: parent; text: root.secBusy === "Unlocking" ? "…" : "Unlock"; color: root.textPrimary; font.pixelSize: 11 }
                            MouseArea { id: secUnlockMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: root.doUnlock(unlockField.text) }
                        }
                    }

                    Rectangle { Layout.fillWidth: true; height: 1; color: root.borderColor }

                    // FORGOT THE PASSWORD, STILL HAVE THE PHRASE. This is the canonical wallet
                    // recovery and it belongs on the locked screen, which is where that user is.
                    // The core allows a restore while locked on purpose (the phrase IS the
                    // credential there, and it moves the outgoing store aside rather than deleting
                    // it); without this the only route was Erase → create a throwaway wallet →
                    // restore over it, which asks a frightened user to erase first.
                    Text { font.family: root.faceFont; text: "Restore from your recovery phrase"
                        color: root.textSecondary; font.pixelSize: 10 }
                    Text { font.family: root.faceFont;
                        text: "Replaces the wallet on this machine with the one your phrase derives. "
                            + "The current storage is kept aside, never deleted."
                        color: root.textDisabled; font.pixelSize: 9; wrapMode: Text.WordWrap; Layout.fillWidth: true
                    }
                    Rectangle {
                        Layout.fillWidth: true; height: 46; color: root.inputBg
                        border.color: lockedPhrase.activeFocus ? root.accentOrange : root.borderColor; radius: 8
                        TextEdit {
                            id: lockedPhrase
                            anchors { fill: parent; margins: 6 }
                            color: root.textPrimary; font.pixelSize: 10
                            wrapMode: TextEdit.WordWrap; clip: true; selectByMouse: true
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true; spacing: 6
                        Rectangle {
                            Layout.fillWidth: true; height: 26; color: root.inputBg
                            border.color: lockedRestorePw.activeFocus ? root.accentOrange : root.borderColor; radius: 8
                            TextInput { font.family: root.faceFont;
                                id: lockedRestorePw
                                anchors { fill: parent; leftMargin: 6; rightMargin: 6 }
                                verticalAlignment: TextInput.AlignVCenter; echoMode: TextInput.Password
                                color: root.textPrimary; font.pixelSize: 10; clip: true
                                Text { font.family: root.faceFont;
                                    anchors.fill: parent; verticalAlignment: Text.AlignVCenter
                                    text: parent.text.length === 0 ? "new password" : ""
                                    color: root.textDisabled; font.pixelSize: 10
                                }
                            }
                        }
                        Rectangle {
                            Layout.preferredWidth: 74; height: 26; radius: 10; color: "transparent"; border.color: root.accentOrange
                            Text { font.family: root.faceFont; anchors.centerIn: parent
                                text: root.secBusy === "Restoring" ? "…" : "Restore"
                                color: root.accentOrange; font.pixelSize: 10 }
                            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                                onClicked: root.doRestore(lockedPhrase.text, lockedRestorePw.text, 5) }
                        }
                    }

                    // Escape hatch - forgotten password, no phrase either → start over.
                    RowLayout {
                        Layout.fillWidth: true; spacing: 6
                        Text { font.family: root.faceFont;
                            text: "No phrase either, or it isn't your wallet?"
                            color: root.textDisabled; font.pixelSize: 9; wrapMode: Text.WordWrap; Layout.fillWidth: true
                        }
                        Rectangle {
                            Layout.preferredWidth: 124; height: 22; radius: 10; color: "transparent"; border.color: root.errorRed
                            Text { font.family: root.faceFont;
                                anchors.centerIn: parent
                                text: root.resetArmed ? "Tap again to erase" : "Erase & start over"
                                color: root.errorRed; font.pixelSize: 9
                            }
                            MouseArea {
                                anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                                onClicked: { if (root.resetArmed) root.doResetWallet(); else root.resetArmed = true }
                            }
                        }
                    }
                }

                // ── Unlocked: backup + import/export ──
                // Keyed on the wallet state, not on !walletLocked: walletLocked is a second
                // variable saying the same thing, and the two drifting apart is how a section
                // shows in a state none of its buttons work in.
                ColumnLayout {
                    visible: root.walletState === "ready"
                    Layout.fillWidth: true; spacing: 8

                    Text { font.family: root.faceFont; text: "SECURITY & BACKUP"; color: root.brandRed; font.pixelSize: 9; font.bold: true; font.letterSpacing: 1.2 }
                    Text { font.family: root.faceFont;
                        text: "Never share your recovery phrase or private keys - anyone with them controls your funds."
                        color: root.textDisabled; font.pixelSize: 9; wrapMode: Text.WordWrap; Layout.fillWidth: true
                    }

                    // Session state. The password typed at unlock is held in memory for this
                    // session and re-checked by every sensitive operation; it is dropped on idle
                    // and by the Lock button. No operation ever asks for it a second time.
                    //
                    // On a PLAINTEXT store there is no session and there cannot be one: unlock()
                    // refuses a store it cannot verify a password against, so nothing ever reaches
                    // establishSession. Claiming "Unlocked, locks itself in 15 min" there described
                    // a protection the store does not have, next to a banner correctly saying it is
                    // not encrypted, and offered a Lock button with no session to forget. Both are
                    // replaced by the truth and by the route that changes it.
                    RowLayout {
                        Layout.fillWidth: true; spacing: 6
                        Text { font.family: root.faceFont; Layout.fillWidth: true
                            color: root.textDisabled; font.pixelSize: 9; wrapMode: Text.WordWrap
                            text: root.signingBlocked
                                ? "Not unlocked, and it cannot be: this store has no password on it, "
                                  + "so there is no session to hold, to time out, or to lock. Set a "
                                  + "password above and it becomes a normal encrypted wallet."
                                : root.autoLockMs > 0
                                ? "Unlocked. Locks itself after " + Math.round(root.autoLockMs / 60000)
                                  + " min without a wallet operation (about "
                                  + Math.max(0, Math.round((root.autoLockMs - root.idleMs) / 60000))
                                  + " min left)."
                                : "Unlocked. The session password is held in memory only, never stored."
                        }
                        Rectangle {
                            visible: !root.signingBlocked
                            Layout.preferredWidth: 84; height: 22; radius: 10
                            color: "transparent"
                            border.color: secLockMa.containsMouse ? root.brandRedHover : root.brandRed
                            Text { font.family: root.faceFont; anchors.centerIn: parent
                                text: "Lock now"; color: root.brandRed; font.pixelSize: 9 }
                            MouseArea { id: secLockMa; anchors.fill: parent; hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor; onClicked: root.lockWallet() }
                        }
                    }

                    // The "create a new encrypted wallet" row used to live here. It is gone on
                    // purpose: this section shows for walletState === "ready", which means a store
                    // exists (encrypted and open, or legacy plaintext), and createEncryptedWallet
                    // refuses in both cases ("wallet-exists" / "wallet-not-encrypted"). Creating is
                    // the onboarding screen's job; the "new" section above points at it. Replacing
                    // an existing wallet is Restore (below) or Erase & start over; giving a
                    // plaintext store a password is the block above, not this one.

                    // ── Export: the two controls the gate refuses on a plaintext store ──
                    // exportMnemonic and exportKey both call authorize(), which on a store with no
                    // crypto envelope has no secret to compare and refuses with reason
                    // "unencrypted". Offering them anyway meant a user pressed "Reveal recovery
                    // phrase" and got a refusal on the same screen that had just told them they
                    // were "Unlocked". They are disabled instead, with the reason and the route on
                    // the button's own row, and they come back the moment the store is migrated.
                    Text { font.family: root.faceFont;
                        visible: root.signingBlocked
                        text: "Revealing the recovery phrase or a private key needs a wallet that can "
                            + "prove who is asking, so both are disabled while this store has no "
                            + "password on it. Set one above and they work again. (A wallet migrated "
                            + "this way never held a recovery phrase, so only the key export applies.)"
                        color: root.warningAmber; font.pixelSize: 9
                        wrapMode: Text.WordWrap; Layout.fillWidth: true
                    }
                    ColumnLayout {
                        Layout.fillWidth: true; spacing: 8
                        enabled: !root.signingBlocked
                        opacity: enabled ? 1.0 : 0.4

                    // Reveal recovery phrase
                    RowLayout {
                        Layout.fillWidth: true; spacing: 6
                        Rectangle {
                            Layout.preferredWidth: 160; height: 26; radius: 10; color: "transparent"; border.color: root.borderColor
                            Text { font.family: root.faceFont; anchors.centerIn: parent; text: "Reveal recovery phrase"; color: root.textSecondary; font.pixelSize: 10 }
                            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.doExportMnemonic() }
                        }
                        Rectangle {
                            visible: root.exportedMnemonic.length > 0
                            Layout.preferredWidth: 52; height: 26; radius: 10; color: "transparent"; border.color: root.borderColor
                            Text { font.family: root.faceFont; anchors.centerIn: parent; text: root.revealMnemonic ? "Hide" : "Show"; color: root.textSecondary; font.pixelSize: 10 }
                            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.revealMnemonic = !root.revealMnemonic }
                        }
                        Item { Layout.fillWidth: true }
                    }
                    Rectangle {
                        visible: root.exportedMnemonic.length > 0 && root.revealMnemonic
                        Layout.fillWidth: true; height: phraseText.implicitHeight + 12
                        color: root.inputBg; border.color: root.borderColor; radius: 8
                        Text { font.family: root.faceFont;
                            id: phraseText
                            anchors { fill: parent; margins: 6 }
                            text: root.exportedMnemonic; color: root.textPrimary
                            font.pixelSize: 11; wrapMode: Text.WordWrap
                        }
                        MouseArea {
                            anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                            onClicked: { clipHelper.text = root.exportedMnemonic; clipHelper.selectAll(); clipHelper.copy(); root.logActivity("Recovery phrase copied", false) }
                        }
                    }

                    // Export account private key
                    RowLayout {
                        Layout.fillWidth: true; spacing: 6
                        Rectangle {
                            Layout.preferredWidth: 160; height: 26; radius: 10; color: "transparent"; border.color: root.borderColor
                            Text { font.family: root.faceFont; anchors.centerIn: parent; text: "Export account key"; color: root.textSecondary; font.pixelSize: 10 }
                            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.doExportKey() }
                        }
                        Text { font.family: root.faceFont;
                            text: root.selectedFromId.length > 0 ? root.displayId(root.selectedFromId) : "select a public account"
                            color: root.textDisabled; font.pixelSize: 9; elide: Text.ElideMiddle; Layout.fillWidth: true
                        }
                    }
                    Rectangle {
                        visible: root.exportedKey.length > 0
                        Layout.fillWidth: true; height: 26; color: root.inputBg; border.color: root.borderColor; radius: 8
                        Text { font.family: root.faceFont;
                            anchors { fill: parent; leftMargin: 6; rightMargin: 6 }
                            verticalAlignment: Text.AlignVCenter
                            text: root.revealKey ? root.exportedKey : "•••••••••••••••• (tap to copy)"
                            color: root.textPrimary; font.pixelSize: 10; elide: Text.ElideRight
                        }
                        MouseArea {
                            anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                            onClicked: { clipHelper.text = root.exportedKey; clipHelper.selectAll(); clipHelper.copy(); root.revealKey = true; root.logActivity("Private key copied", false) }
                        }
                    }
                    }   // end of the gate-dependent export block

                    Rectangle { Layout.fillWidth: true; height: 1; color: root.borderColor }

                    // Restore from recovery phrase
                    Text { font.family: root.faceFont; text: "Restore from recovery phrase"; color: root.textSecondary; font.pixelSize: 10 }
                    Rectangle {
                        Layout.fillWidth: true; height: 46; color: root.inputBg
                        border.color: restorePhrase.activeFocus ? root.accentOrange : root.borderColor; radius: 8
                        TextEdit {
                            id: restorePhrase
                            anchors { fill: parent; margins: 6 }
                            color: root.textPrimary; font.pixelSize: 10;                            wrapMode: TextEdit.WordWrap; clip: true; selectByMouse: true
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true; spacing: 6
                        Rectangle {
                            Layout.fillWidth: true; height: 26; color: root.inputBg; border.color: restorePw.activeFocus ? root.accentOrange : root.borderColor; radius: 8
                            TextInput { font.family: root.faceFont;
                                id: restorePw
                                anchors { fill: parent; leftMargin: 6; rightMargin: 6 }
                                verticalAlignment: TextInput.AlignVCenter; echoMode: TextInput.Password
                                color: root.textPrimary; font.pixelSize: 10; clip: true
                                Text { font.family: root.faceFont;
                                    anchors.fill: parent; verticalAlignment: Text.AlignVCenter
                                    text: parent.text.length === 0 ? "new password" : ""
                                    color: root.textDisabled; font.pixelSize: 10
                                }
                            }
                        }
                        Rectangle {
                            Layout.preferredWidth: 54; height: 26; color: root.inputBg; border.color: restoreDepth.activeFocus ? root.accentOrange : root.borderColor; radius: 8
                            TextInput { font.family: root.faceFont;
                                id: restoreDepth
                                anchors { fill: parent; leftMargin: 6; rightMargin: 6 }
                                verticalAlignment: TextInput.AlignVCenter; text: "5"
                                inputMethodHints: Qt.ImhDigitsOnly; color: root.textPrimary; font.pixelSize: 10; clip: true
                            }
                        }
                        Rectangle {
                            Layout.preferredWidth: 74; height: 26; radius: 10; color: "transparent"; border.color: root.accentOrange
                            Text { font.family: root.faceFont; anchors.centerIn: parent; text: root.secBusy === "Restoring" ? "…" : "Restore"; color: root.accentOrange; font.pixelSize: 10 }
                            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.doRestore(restorePhrase.text, restorePw.text, parseInt(restoreDepth.text, 10) || 5) }
                        }
                    }

                    // Import a private key
                    Text { font.family: root.faceFont; text: "Import a private key"; color: root.textSecondary; font.pixelSize: 10 }
                    RowLayout {
                        Layout.fillWidth: true; spacing: 6
                        Rectangle {
                            Layout.fillWidth: true; height: 26; color: root.inputBg; border.color: importKeyField.activeFocus ? root.accentOrange : root.borderColor; radius: 8
                            TextInput { font.family: root.faceFont;
                                id: importKeyField
                                anchors { fill: parent; leftMargin: 6; rightMargin: 6 }
                                verticalAlignment: TextInput.AlignVCenter
                                color: root.textPrimary; font.pixelSize: 10; clip: true
                                Text { font.family: root.faceFont;
                                    anchors.fill: parent; verticalAlignment: Text.AlignVCenter
                                    text: parent.text.length === 0 ? "64-char hex private key" : ""
                                    color: root.textDisabled; font.pixelSize: 10;                                }
                            }
                        }
                        Rectangle {
                            Layout.preferredWidth: 70; height: 26; color: root.inputBg; border.color: importLabelField.activeFocus ? root.accentOrange : root.borderColor; radius: 8
                            TextInput { font.family: root.faceFont;
                                id: importLabelField
                                anchors { fill: parent; leftMargin: 6; rightMargin: 6 }
                                verticalAlignment: TextInput.AlignVCenter
                                color: root.textPrimary; font.pixelSize: 10; clip: true
                                Text { font.family: root.faceFont;
                                    anchors.fill: parent; verticalAlignment: Text.AlignVCenter
                                    text: parent.text.length === 0 ? "label" : ""
                                    color: root.textDisabled; font.pixelSize: 10
                                }
                            }
                        }
                        Rectangle {
                            Layout.preferredWidth: 64; height: 26; radius: 10; color: "transparent"; border.color: root.accentOrange
                            Text { font.family: root.faceFont; anchors.centerIn: parent; text: root.secBusy === "Importing" ? "…" : "Import"; color: root.accentOrange; font.pixelSize: 10 }
                            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.doImportKey(importKeyField.text, importLabelField.text) }
                        }
                    }
                }
            }
            }  // security Flickable
        }

        // ── Settings screen ─────────────────────────────────────────────────────
        Rectangle {
            Layout.fillWidth: true
            visible: root.screen === "settings"
            Layout.fillHeight: true
            color: root.panelColor; border.color: root.borderColor; border.width: 1; radius: 12
            clip: true

            ColumnLayout {
                id: settingsInner
                anchors { left: parent.left; right: parent.right; top: parent.top; margins: 10 }
                spacing: 8

                RowLayout {   // back header
                    Layout.fillWidth: true; spacing: 6
                    Rectangle { width: 26; height: 24; radius: 10; color: "transparent"; border.color: root.borderColor
                        Text { anchors.centerIn: parent; text: "←"; color: root.textSecondary; font.pixelSize: 13; font.family: root.faceFont }
                        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.screen = "main" } }
                    Text { font.family: root.faceFont; text: "Settings"; color: root.textPrimary; font.pixelSize: 13; font.bold: true }
                    Item { Layout.fillWidth: true }
                }

                Text { font.family: root.faceFont; text: "Wallet CLI"; color: root.textSecondary; font.pixelSize: 10 }
                // READ-ONLY on purpose. A stored CLI path was code execution plus password
                // capture that outlived both a reboot and the module that planted it, so the core
                // no longer reads the setting and setCliPath always refuses ("not-supported").
                // An editable field with a Save button that can only fail is a lie the user pays
                // for, so this shows what will actually run and how to change it instead.
                Rectangle {
                    Layout.fillWidth: true; height: 26; color: root.inputBg
                    border.color: root.borderColor; radius: 8
                    Text { font.family: root.faceFont;
                        id: cliPathField
                        anchors { fill: parent; leftMargin: 6; rightMargin: 6 }
                        verticalAlignment: Text.AlignVCenter
                        text: root.cliPathEff.length > 0 ? root.cliPathEff
                            : (root.cliPath.length > 0 ? root.cliPath : "wallet")
                        color: root.textPrimary; font.pixelSize: 11; elide: Text.ElideMiddle; clip: true
                    }
                    MouseArea {
                        anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                        onClicked: { clipHelper.text = cliPathField.text; clipHelper.selectAll()
                                     clipHelper.copy(); root.logActivity("Wallet CLI path copied", false) }
                    }
                }
                Text { font.family: root.faceFont
                    visible: !root.cliFound
                    text: "This binary is missing, so no wallet operation can run. Reinstall the "
                        + "medusa_core module, or set MEDUSA_WALLET_CLI to a wallet binary in the "
                        + "environment that launches Basecamp, then reopen Medusa."
                    color: root.errorRed; font.pixelSize: 9; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                // A stored override from an older build is IGNORED rather than deleted, so say so:
                // on an install poisoned before the override was removed this is the only visible
                // trace of what was planted.
                Rectangle {
                    visible: root.cliPathIgnored
                    Layout.fillWidth: true
                    implicitHeight: cliIgnoredTxt.implicitHeight + 14
                    radius: 8; color: root.errorTint; border.color: root.errorRed; border.width: 1
                    Text {
                        id: cliIgnoredTxt
                        anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter
                                  leftMargin: 8; rightMargin: 8 }
                        font.family: root.faceFont; font.pixelSize: 9; wrapMode: Text.WordWrap
                        color: root.errorRed
                        text: "A wallet CLI path saved by an older build is being ignored: medusa "
                            + "runs the binary bundled with the module and never a path stored on "
                            + "disk, because anything running as you could rewrite it. Use "
                            + "MEDUSA_WALLET_CLI to point at a different build."
                    }
                }
                Text { font.family: root.faceFont
                    text: "The wallet CLI is not configurable from here - medusa runs the binary "
                        + "bundled with the module. Set MEDUSA_WALLET_CLI before launching to use "
                        + "a different build."
                    color: root.textDisabled; font.pixelSize: 9; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                Text { font.family: root.faceFont
                    text: "The network connection is configured per-zone - switch or add zones from the network selector at the top."
                    color: root.textDisabled; font.pixelSize: 9; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            }
        }

        // ── Accounts screen (opened from the top bar selector) ──────────────────
        Rectangle {
            Layout.fillWidth: true
            visible: root.screen === "accounts" && root.walletState === "ready"
            Layout.fillHeight: true
            color: root.panelColor; border.color: root.borderColor; border.width: 1; radius: 12
            clip: true
            // Real elevation - soft drop shadow (autoPadding stops the shadow clipping).
            layer.enabled: true
            layer.effect: MultiEffect {
                shadowEnabled: true; autoPaddingEnabled: true
                shadowColor: "#000000"; shadowVerticalOffset: 4; shadowBlur: 0.45; shadowOpacity: 0.25
            }

            ColumnLayout {
                id: acctMenuCol
                anchors { fill: parent; margins: 10 }
                spacing: 6

                RowLayout {
                    Layout.fillWidth: true; spacing: 6
                    Rectangle { width: 26; height: 24; radius: 10; color: "transparent"; border.color: root.borderColor
                        Text { anchors.centerIn: parent; text: "←"; color: root.textSecondary; font.pixelSize: 13; font.family: root.faceFont }
                        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.screen = "main" } }
                    Text { font.family: root.faceFont; text: "Accounts"; color: root.textPrimary; font.pixelSize: 13; font.bold: true }
                    Item { Layout.fillWidth: true }
                    Rectangle {   // sync private balances
                        width: 22; height: 18; radius: 10; color: "transparent"
                        border.color: root.syncBusy ? root.accentOrange : root.borderColor
                        Text { font.family: root.faceFont; anchors.centerIn: parent; text: "⟳"
                            color: root.syncBusy ? root.accentOrange : root.textSecondary; font.pixelSize: 11
                            SequentialAnimation on opacity { running: root.syncBusy; loops: Animation.Infinite
                                NumberAnimation { to: 0.3; duration: 400 } NumberAnimation { to: 1.0; duration: 400 } } }
                        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; enabled: !root.syncBusy; onClicked: root.doSyncPrivate() }
                    }
                }

                ListView {
                    id: accountListView
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    model: ListModel { id: accountModel }
                    clip: true; spacing: 2
                    section.property: "type"
                    section.delegate: RowLayout {
                        width: accountListView.width; height: 20; spacing: 6
                        Text { font.family: root.faceFont; text: section === "public" ? "PUBLIC" : "PRIVATE"
                            color: root.textDisabled; font.pixelSize: 9; font.bold: true; font.letterSpacing: 1.2; Layout.leftMargin: 2 }
                        Rectangle { Layout.fillWidth: true; height: 1; color: root.borderColor }
                    }
                    delegate: Rectangle {
                        required property string id
                        required property string type
                        required property string balance
                        required property string name
                        property bool renaming: root.renamingAcctId === id
                        width: accountListView.width; height: 44; radius: 10
                        color: root.selectedFromId === id ? root.accentTint10
                             : (rowMa.containsMouse ? Qt.rgba(255,255,255,0.05) : "transparent")
                        border.color: root.selectedFromId === id ? Qt.rgba(196/255, 196/255, 196/255, 0.40) : "transparent"
                        border.width: 1
                        // row switch (behind) - disabled while renaming so taps go to the field
                        MouseArea {
                            id: rowMa
                            anchors.fill: parent; hoverEnabled: true
                            cursorShape: parent.renaming ? Qt.ArrowCursor : Qt.PointingHandCursor
                            enabled: !parent.renaming
                            onClicked: {
                                root.selectedFromId = id; root.selectedFromType = type; root.selectedFromBalance = balance
                                root.refreshTokens(); root.screen = "main"
                            }
                        }
                        RowLayout {
                            anchors { fill: parent; leftMargin: 8; rightMargin: 8 }
                            spacing: 8
                            Rectangle { width: 18; height: 18; radius: 9; Layout.alignment: Qt.AlignVCenter
                                color: root.avatarColor(id)
                                border.color: type === "private" ? root.accentOrange : root.borderColor }
                            ColumnLayout {
                                spacing: 2; Layout.fillWidth: true
                                // title row: name/displayId, OR a rename field when editing
                                Rectangle {
                                    visible: renaming; Layout.fillWidth: true; height: 22; radius: 6
                                    color: root.inputBg; border.color: root.accentOrange
                                    TextInput { id: renameField
                                        anchors { fill: parent; leftMargin: 6; rightMargin: 6 }
                                        verticalAlignment: TextInput.AlignVCenter
                                        font.family: root.faceFont; font.pixelSize: 11; color: root.textPrimary; clip: true
                                        onVisibleChanged: if (visible) { text = name; forceActiveFocus(); selectAll() }
                                        onAccepted: root.renameAccount(id, text)
                                        Text { anchors.fill: parent; verticalAlignment: Text.AlignVCenter
                                            text: parent.text.length === 0 ? "account name" : ""
                                            color: root.textDisabled; font.pixelSize: 11; font.family: root.faceFont } }
                                }
                                Text { visible: !renaming; font.family: root.faceFont; text: root.acctTitle(id, name)
                                    color: root.selectedFromId === id ? root.textPrimary : root.textSecondary
                                    font.pixelSize: 11; font.bold: name.length > 0; Layout.fillWidth: true; elide: Text.ElideMiddle }
                                // subtitle: short id (when a name is shown) + balance
                                RowLayout { visible: !renaming; Layout.fillWidth: true; spacing: 6
                                    Text { font.family: root.faceFont; visible: name.length > 0; text: root.displayId(id)
                                        color: root.textDisabled; font.pixelSize: 8 }
                                    Text { font.family: root.faceFont; visible: balance !== "" && balance !== "-"
                                        text: balance + " LEZ"
                                        color: root.selectedFromId === id ? root.accentOrange : root.textDisabled; font.pixelSize: 9 }
                                    Item { Layout.fillWidth: true } }
                            }
                            // edit/confirm name button
                            Rectangle { Layout.preferredWidth: 24; height: 24; radius: 8
                                color: renaming ? root.accentTint14 : "transparent"
                                border.color: renaming ? root.accentOrange : root.borderColor
                                Text { anchors.centerIn: parent; text: renaming ? "✓" : "✎"
                                    color: renaming ? root.accentOrange : root.silver; font.pixelSize: 11; font.family: root.faceFont }
                                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                                    onClicked: { if (renaming) root.renameAccount(id, renameField.text); else root.renamingAcctId = id } }
                            }
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true; spacing: 6
                    Rectangle {
                        Layout.fillWidth: true; height: 30; radius: 10; color: "transparent"; border.color: root.accentOrange
                        Text { font.family: root.faceFont; anchors.centerIn: parent; text: "+ Public account"; color: root.accentOrange; font.pixelSize: 11 }
                        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                root.logActivity("Creating public account…", false)
                                var r = root.callModuleParse(logos.callModule("medusa_core", "createAccount", []))
                                if (r && r.error) root.logActivity("createAccount: " + r.error, true)
                                else { root.logActivity("Public account created", false); balanceRefreshTimer.restart() }
                            } }
                    }
                    Rectangle {
                        Layout.fillWidth: true; height: 30; radius: 10; color: "transparent"; border.color: root.borderColor
                        Text { font.family: root.faceFont; anchors.centerIn: parent; text: "+ Private account"; color: root.textSecondary; font.pixelSize: 11 }
                        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.createPrivateAccount() }
                    }
                }
            }
        }

        // ── Zones screen (switch / add LEZ chains - token-agnostic) ─────────────
        Rectangle {
            Layout.fillWidth: true
            visible: root.screen === "network"
            Layout.fillHeight: true
            color: root.panelColor; border.color: root.borderColor; border.width: 1; radius: 12
            clip: true
            Flickable {
                anchors { fill: parent; margins: 10 }
                contentWidth: width; contentHeight: zonesCol.implicitHeight; clip: true
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            ColumnLayout {
                id: zonesCol
                width: parent.width
                spacing: 8
                RowLayout {   // back header
                    Layout.fillWidth: true; spacing: 6
                    Rectangle { width: 26; height: 24; radius: 10; color: "transparent"; border.color: root.borderColor
                        Text { anchors.centerIn: parent; text: "←"; color: root.textSecondary; font.pixelSize: 13; font.family: root.faceFont }
                        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.screen = "main" } }
                    Text { font.family: root.faceFont; text: "Zones"; color: root.textPrimary; font.pixelSize: 13; font.bold: true }
                    Item { Layout.fillWidth: true }
                }
                Repeater {
                    model: root.zones
                    delegate: Rectangle {
                        required property var modelData
                        Layout.fillWidth: true; height: 52; radius: 12
                        color: root.network === modelData.id ? root.accentTint10 : "transparent"
                        border.color: root.network === modelData.id ? root.accentOrange : root.borderColor; border.width: 1
                        // Row switch - direct child of the Rectangle (anchors.fill works here),
                        // declared first so it sits behind the content; the remove ✕'s own
                        // MouseArea intercepts ✕ clicks (MouseArea doesn't propagate by default).
                        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                            onClicked: root.switchZone(modelData.id) }
                        RowLayout {
                            anchors { fill: parent; leftMargin: 12; rightMargin: 10 }
                            spacing: 10
                            Rectangle { width: 10; height: 10; radius: 5; Layout.alignment: Qt.AlignVCenter
                                color: root.zoneDotColor(modelData) }
                            ColumnLayout { spacing: 1; Layout.fillWidth: true
                                Text { font.family: root.faceFont; text: modelData.name; color: root.textPrimary; font.pixelSize: 12; font.bold: true }
                                Text { font.family: root.faceFont; text: root.zoneKindDesc(modelData); color: root.textDisabled; font.pixelSize: 9
                                    elide: Text.ElideRight; Layout.fillWidth: true } }
                            Text { visible: root.network === modelData.id; font.family: root.faceFont; text: "✓"; color: root.accentOrange; font.pixelSize: 14 }
                            // edit (user zones only) - visible "Edit" chip
                            Rectangle { visible: !modelData.builtin; Layout.preferredWidth: 40; height: 24; radius: 8
                                color: root.selectBg; border.color: root.borderColor; border.width: 1
                                Text { anchors.centerIn: parent; text: "Edit"; color: root.silver; font.pixelSize: 9; font.family: root.faceFont }
                                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                                    onClicked: root.beginEditZone(modelData) } }
                            // remove (user zones only)
                            Rectangle { visible: !modelData.builtin; Layout.preferredWidth: 24; height: 24; radius: 8
                                color: root.selectBg; border.color: root.borderColor; border.width: 1
                                Text { anchors.centerIn: parent; text: "✕"; color: root.errorRed; font.pixelSize: 11; font.family: root.faceFont }
                                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        var wasActive = (root.network === modelData.id)
                                        var rr = root.callModuleParse(logos.callModule("medusa_core", "removeZone", [modelData.id]))
                                        if (rr && rr.error) { root.logActivity("Remove zone: " + rr.error, true); return }
                                        root.refreshZones()
                                        if (wasActive) {
                                            root.zoneCompat = "unknown"       // the build-compat verdict is per-zone
                                            root.zoneOfflineOpen = false      // a stale offline modal refers to the old zone
                                            root.selectedFromId = ""; root.selectedTokens = []; root.refreshSeqStatus(); netReloadTimer.restart()
                                        }
                                    } } }
                        }
                    }
                }

                // + Add zone (remote sequencer) / Cancel
                Rectangle {
                    Layout.fillWidth: true; height: 36; radius: 12
                    color: "transparent"; border.color: root.accentOrange
                    Text { anchors.centerIn: parent; text: root.addZoneOpen ? "Cancel" : "+ Add zone"; color: root.accentOrange; font.pixelSize: 11; font.family: root.faceFont }
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            if (root.addZoneOpen) { root.addZoneOpen = false; root.editingZoneId = ""; zNameF.text = ""; zEndF.text = "" }
                            else { root.editingZoneId = ""; zNameF.text = ""; zEndF.text = ""; zTorTog.checked = false; root.addZoneOpen = true }
                        } }
                }
                ColumnLayout {
                    visible: root.addZoneOpen
                    Layout.fillWidth: true; spacing: 6
                    Text { font.family: root.faceFont; Layout.fillWidth: true; wrapMode: Text.WordWrap; font.pixelSize: 9; color: root.textDisabled
                        text: root.editingZoneId !== "" ? "Edit this zone's name, endpoint, or transport."
                                                        : "Connect to a shared LEZ zone (someone's sequencer)." }
                    Rectangle { Layout.fillWidth: true; height: 28; radius: 8; color: root.inputBg; border.color: zNameF.activeFocus ? root.accentOrange : root.borderColor
                        TextInput { id: zNameF; anchors { fill: parent; leftMargin: 8; rightMargin: 8 }
                            verticalAlignment: TextInput.AlignVCenter
                            font.family: root.faceFont; font.pixelSize: 11; color: root.textPrimary; clip: true
                            Text { anchors.fill: parent; verticalAlignment: Text.AlignVCenter; text: parent.text.length === 0 ? "name (e.g. Logos DEX)" : ""; color: root.textDisabled; font.pixelSize: 11; font.family: root.faceFont } } }
                    RowLayout { Layout.fillWidth: true; spacing: 8
                        Text { font.family: root.faceFont; text: "Transport"; color: root.textSecondary; font.pixelSize: 10 }
                        Rectangle { id: zTorTog; property bool checked: false   // default: clearnet
                            Layout.preferredWidth: 80; height: 24; radius: 12; color: root.inputBg; border.color: root.borderColor
                            Text { font.family: root.faceFont; anchors.centerIn: parent; text: parent.checked ? "Tor" : "Direct"; font.pixelSize: 10; color: root.textPrimary }
                            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: parent.checked = !parent.checked } }
                        Item { Layout.fillWidth: true } }
                    Rectangle { Layout.fillWidth: true; height: 28; radius: 8; color: root.inputBg; border.color: zEndF.activeFocus ? root.accentOrange : root.borderColor
                        TextInput { id: zEndF; anchors { fill: parent; leftMargin: 8; rightMargin: 8 }
                            verticalAlignment: TextInput.AlignVCenter
                            font.family: root.faceFont; font.pixelSize: 10; color: root.textPrimary; clip: true
                            Text { anchors.fill: parent; verticalAlignment: Text.AlignVCenter
                                text: parent.text.length === 0 ? (zTorTog.checked ? "sequencer .onion address" : "https://sequencer.example:3072/") : ""
                                color: root.textDisabled; font.pixelSize: 10; font.family: root.faceFont } } }
                    Rectangle { Layout.fillWidth: true; height: 32; radius: 10
                        color: root.accentTint14; border.color: root.accentOrange
                        Text { anchors.centerIn: parent; text: root.editingZoneId !== "" ? "Save changes" : "Add zone"; color: root.accentOrange; font.pixelSize: 11; font.bold: true; font.family: root.faceFont }
                        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                var tor = zTorTog.checked
                                var editing = root.editingZoneId !== ""
                                var r = editing
                                    ? root.callModuleParse(logos.callModule("medusa_core", "editZone",
                                        [root.editingZoneId, zNameF.text, tor ? "" : zEndF.text, tor ? zEndF.text : "", tor]))
                                    : root.callModuleParse(logos.callModule("medusa_core", "addZone",
                                        [zNameF.text, tor ? "" : zEndF.text, tor ? zEndF.text : "", tor]))
                                if (r && r.error) { root.logActivity((editing ? "Edit" : "Add") + " zone: " + r.error, true); return }
                                var editedId = root.editingZoneId
                                zNameF.text = ""; zEndF.text = ""; root.addZoneOpen = false; root.editingZoneId = ""
                                root.refreshZones()
                                if (r && r.id) root.switchZone(r.id)                                  // new zone → switch
                                else if (editedId && root.network === editedId) {
                                    root.zoneCompat = "unknown"       // the build-compat verdict is per-zone
                                    root.zoneOfflineOpen = false      // a stale offline modal refers to the old endpoint
                                    root.refreshSeqStatus(); netReloadTimer.restart()
                                }
                            } }
                    }
                }
                Text { font.family: root.faceFont; Layout.fillWidth: true; wrapMode: Text.WordWrap
                    text: "Your accounts are the same on every zone; balances and tokens are per-zone. The wallet must match the zone's Logos version."
                    color: root.textDisabled; font.pixelSize: 9 }
            }
            }  // zones Flickable
        }

        // ── Add-token screen (whitelist picker + custom id) ─────────────────────
        Rectangle {
            Layout.fillWidth: true
            visible: root.screen === "addtoken" && root.walletState === "ready"
            Layout.fillHeight: true
            color: root.panelColor; border.color: root.borderColor; border.width: 1; radius: 12
            clip: true
            ColumnLayout {
                anchors { left: parent.left; right: parent.right; top: parent.top; margins: 10 }
                spacing: 8
                RowLayout {   // back header
                    Layout.fillWidth: true; spacing: 6
                    Rectangle { width: 26; height: 24; radius: 10; color: "transparent"; border.color: root.borderColor
                        Text { anchors.centerIn: parent; text: "←"; color: root.textSecondary; font.pixelSize: 13; font.family: root.faceFont }
                        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.screen = "main" } }
                    Text { font.family: root.faceFont; text: "Add token"; color: root.textPrimary; font.pixelSize: 13; font.bold: true }
                    Item { Layout.fillWidth: true }
                }
                Text { font.family: root.faceFont; text: "FROM THE WHITELIST"; color: root.brandRed; font.pixelSize: 9; font.bold: true; font.letterSpacing: 1.2 }
                Repeater {
                    model: root.whitelistTokens
                    delegate: Rectangle {
                        required property var modelData
                        Layout.fillWidth: true; height: 46; radius: 12
                        color: "transparent"; border.color: root.borderColor; border.width: 1
                        RowLayout {
                            anchors { fill: parent; leftMargin: 10; rightMargin: 10 }
                            spacing: 10
                            Rectangle { width: 26; height: 26; radius: 13; color: root.avatarColor(modelData.def); Layout.alignment: Qt.AlignVCenter
                                Text { anchors.centerIn: parent; text: modelData.name.substring(0,1); color: root.textPrimary; font.pixelSize: 13; font.bold: true; font.family: root.faceFont } }
                            ColumnLayout { spacing: 0
                                Text { font.family: root.faceFont; text: modelData.name; color: root.textPrimary; font.pixelSize: 12; font.bold: true }
                                Text { font.family: root.faceFont; text: root.displayId(modelData.def); color: root.textDisabled; font.pixelSize: 9 } }
                            Item { Layout.fillWidth: true }
                            Rectangle { Layout.preferredWidth: 50; height: 24; radius: 10; color: "transparent"; border.color: root.accentOrange
                                Text { anchors.centerIn: parent; text: "+ add"; color: root.accentOrange; font.pixelSize: 10; font.family: root.faceFont }
                                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                                    onClicked: { root.doAddToken(modelData.def); root.activeTab = "tokens"; root.screen = "main" } } }
                        }
                    }
                }
                Text { font.family: root.faceFont; visible: root.whitelistTokens.length === 0; text: "No whitelist configured."; color: root.textDisabled; font.pixelSize: 10 }

                Rectangle { Layout.fillWidth: true; height: 1; color: root.borderColor }
                Text { font.family: root.faceFont; text: "OR BY DEFINITION ID"; color: root.textDisabled; font.pixelSize: 9; font.bold: true; font.letterSpacing: 1.2 }
                RowLayout {
                    Layout.fillWidth: true; spacing: 6
                    Rectangle {
                        Layout.fillWidth: true; height: 28; radius: 10
                        color: root.inputBg; border.color: customTokField.activeFocus ? root.accentOrange : root.borderColor
                        TextInput { id: customTokField; anchors { fill: parent; leftMargin: 8; rightMargin: 8 }
                            verticalAlignment: TextInput.AlignVCenter; font.family: root.faceFont; font.pixelSize: 10; color: root.textPrimary; clip: true
                            Text { anchors.fill: parent; verticalAlignment: Text.AlignVCenter
                                text: parent.text.length === 0 ? "paste token definition id…" : ""
                                color: root.textDisabled; font.pixelSize: 10; font.family: root.faceFont } }
                    }
                    Rectangle { Layout.preferredWidth: 54; height: 28; radius: 10; color: "transparent"; border.color: root.accentOrange
                        Text { anchors.centerIn: parent; text: "+ add"; color: root.accentOrange; font.pixelSize: 10; font.family: root.faceFont }
                        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                            onClicked: if (customTokField.text.trim().length > 0) { root.doAddToken(customTokField.text.trim()); customTokField.text = ""; root.activeTab = "tokens"; root.screen = "main" } } }
                }
            }
        }

        // ── Onboarding / lock screen - shown until a wallet is unlocked ─────────
        Rectangle {
            visible: root.walletState !== "ready" && root.walletState !== "loading"
                     && !root.escapeScreenOpen
            Layout.fillWidth: true
            Layout.fillHeight: true
            gradient: Gradient {
                GradientStop { position: 0.0; color: root.panelColor }
                GradientStop { position: 0.55; color: root.bgColor }
                GradientStop { position: 1.0; color: root.panelColor }
            }
            border.color: root.walletState === "locked" ? root.errorRed : root.borderColor
            border.width: 1; radius: 12

            ColumnLayout {
                anchors.centerIn: parent
                width: Math.min(parent.width - 48, 360)
                spacing: 12

                // ── Brand hero: Medusa logo + wordmark + slogan ──
                Item {
                    id: brandMark
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth: 92; Layout.preferredHeight: 92
                    // gentle entrance - scale + fade in (respects motion tokens)
                    opacity: 0
                    scale: 0.88
                    Component.onCompleted: { opacity = 1; scale = 1 }
                    Behavior on opacity { NumberAnimation { duration: root.motionSlow; easing.type: Easing.OutCubic } }
                    Behavior on scale   { NumberAnimation { duration: root.motionSlow; easing.type: Easing.OutBack } }
                    Rectangle {   // soft silver aura ring
                        anchors.centerIn: parent; width: 92; height: 92; radius: 46
                        color: "transparent"; border.width: 1
                        border.color: root.accentTint22
                    }
                    Rectangle {   // inner disc holding the mark
                        anchors.centerIn: parent; width: 78; height: 78; radius: 39
                        gradient: Gradient {
                            GradientStop { position: 0.0; color: root.surface2 }
                            GradientStop { position: 1.0; color: root.panelColor }
                        }
                        border.color: root.borderStrong; border.width: 1
                        Image {
                            anchors.centerIn: parent
                            source: "icons/medusa-logo.png"
                            width: 54; height: 54
                            fillMode: Image.PreserveAspectFit; smooth: true; mipmap: true
                        }
                    }
                }
                Text { font.family: root.faceFont;
                    Layout.alignment: Qt.AlignHCenter
                    text: "MEDUSA"; font.pixelSize: 26; font.bold: true; font.letterSpacing: 7
                    color: root.textPrimary
                }
                Text { font.family: root.faceFont;
                    Layout.alignment: Qt.AlignHCenter
                    text: "Your many heads to Logos"; font.pixelSize: root.fsXS; font.letterSpacing: 1
                    color: root.silver
                }
                Rectangle { Layout.alignment: Qt.AlignHCenter; Layout.topMargin: 2
                    Layout.preferredWidth: 44; Layout.preferredHeight: 2; radius: 1
                    color: root.silver; opacity: 0.85 }

                // ── State-specific prompt ──
                Text { font.family: root.faceFont;
                    Layout.alignment: Qt.AlignHCenter; Layout.topMargin: 4
                    text: root.walletState === "locked"    ? (root.freshlySealed ? "Wallet ready" : "Welcome back")
                        : root.walletState === "plaintext" ? "Secure your wallet"
                        : root.walletState === "backup"    ? "Back up your recovery phrase"
                        : "Create your wallet"
                    font.pixelSize: 15; font.bold: true; color: root.textPrimary
                }
                Text { font.family: root.faceFont;
                    Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap
                    color: root.textSecondary; font.pixelSize: 11
                    text: root.walletState === "locked"    ? (root.freshlySealed
                            ? "Your wallet is encrypted. Unlock it with the password you just chose - "
                              + "the wallet holds no session until that password has opened the store."
                            : "Enter your password to unlock.")
                        : root.walletState === "plaintext" ? "Set a password to encrypt this wallet."
                        : root.walletState === "backup"    ? "Write these words down and keep them safe - they're the only way to recover your wallet."
                        : "Choose a password. Your wallet is encrypted with it; you'll see your recovery phrase next."
                }

                // A dApp is waiting on a connect/action approval - say why the user is here, so
                // unlocking leads straight into the authorize / select-accounts sheet.
                Rectangle {
                    visible: root.walletState === "locked" && root.pendingConn.length > 0
                    Layout.fillWidth: true; Layout.topMargin: 2
                    implicitHeight: connHintTxt.implicitHeight + 16; radius: 10
                    color: root.accentTint10; border.color: root.accentOrange
                    Text {
                        id: connHintTxt
                        anchors { fill: parent; margins: 8 }
                        font.family: root.faceFont; font.pixelSize: 10; wrapMode: Text.WordWrap
                        horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                        color: root.accentOrange
                        text: {
                            var h = root.pendingConn.length > 0 ? root.pendingConn[0] : null
                            var nm = (h && h.kind === "connect" && h.app && h.app.appName) ? h.app.appName : "An app"
                            return "🔗 " + nm + " is waiting - unlock to "
                                 + (h && h.kind === "action" ? "approve the transfer." : "review & connect.")
                        }
                    }
                }

                // backup: reveal the recovery phrase + continue
                Rectangle {
                    visible: root.walletState === "backup"
                    Layout.fillWidth: true
                    Layout.preferredHeight: bphrase.implicitHeight + 16   // preferredHeight - a plain `height` is ignored by ColumnLayout, collapsing the box
                    color: root.inputBg; border.color: root.accentOrange; radius: 10
                    Text { font.family: root.faceFont;
                        id: bphrase
                        x: 8; y: 8
                        width: parent.width - 16                          // explicit wrap width (not anchors.fill) so implicitHeight computes cleanly
                        text: root.exportedMnemonic; color: root.textPrimary
                        font.pixelSize: 12; wrapMode: Text.WordWrap
                    }
                    MouseArea {
                        anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                        onClicked: { clipHelper.text = root.exportedMnemonic; clipHelper.selectAll(); clipHelper.copy(); root.logActivity("Recovery phrase copied", false) }
                    }
                }
                Rectangle {
                    visible: root.walletState === "backup"
                    Layout.fillWidth: true; height: 36; radius: 10
                    color: backupSavedMa.pressed ? root.brandRedPressed
                         : backupSavedMa.containsMouse ? root.brandRedHover : root.brandRed
                    border.color: root.brandRed
                    Behavior on color { ColorAnimation { duration: root.motionQuick } }
                    Text { font.family: root.faceFont; anchors.centerIn: parent; text: "I've saved it - open my wallet"; color: root.textPrimary; font.pixelSize: 12; font.bold: true }
                    MouseArea { id: backupSavedMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: root.finishBackup() }
                }

                // new / plaintext / locked: password entry
                Rectangle {
                    visible: root.walletState !== "backup"
                    Layout.fillWidth: true; height: 30; color: root.inputBg
                    border.color: onbPw.activeFocus ? root.accentOrange : root.borderColor; radius: 10
                    TextInput { font.family: root.faceFont;
                        id: onbPw
                        anchors { fill: parent; leftMargin: 8; rightMargin: 8 }
                        verticalAlignment: TextInput.AlignVCenter; echoMode: TextInput.Password
                        color: root.textPrimary; font.pixelSize: 12; clip: true
                        onAccepted: if (root.walletState === "locked") root.doUnlock(text)
                        Text { font.family: root.faceFont;
                            anchors.fill: parent; verticalAlignment: Text.AlignVCenter
                            text: parent.text.length === 0 ? "password" : ""
                            color: root.textDisabled; font.pixelSize: 12
                        }
                    }
                }
                // Confirm field. Its condition is the COMPLEMENT of the unlock case, not a list of
                // states that need it: onbBtn.can requires the two fields to match for every state
                // except "locked", so enumerating states here is how a state ends up with a button
                // that can never enable because the field it compares against is hidden.
                Rectangle {
                    visible: root.walletState !== "locked" && root.walletState !== "backup"
                    Layout.fillWidth: true; height: 30; color: root.inputBg
                    border.color: onbPw2.activeFocus ? root.accentOrange : root.borderColor; radius: 10
                    TextInput { font.family: root.faceFont;
                        id: onbPw2
                        anchors { fill: parent; leftMargin: 8; rightMargin: 8 }
                        verticalAlignment: TextInput.AlignVCenter; echoMode: TextInput.Password
                        color: root.textPrimary; font.pixelSize: 12; clip: true
                        Text { font.family: root.faceFont;
                            anchors.fill: parent; verticalAlignment: Text.AlignVCenter
                            text: parent.text.length === 0 ? "confirm password" : ""
                            color: root.textDisabled; font.pixelSize: 12
                        }
                    }
                }
                Text { font.family: root.faceFont;
                    visible: root.walletState !== "locked" && root.walletState !== "backup"
                             && onbPw2.text.length > 0 && onbPw.text !== onbPw2.text
                    text: "passwords don't match"; color: root.errorRed; font.pixelSize: 10
                    Layout.alignment: Qt.AlignHCenter
                }

                Rectangle {
                    id: onbBtn
                    visible: root.walletState !== "backup"
                    Layout.fillWidth: true; height: 36; radius: 10
                    property bool can: root.walletState === "locked"
                        ? onbPw.text.length > 0
                        : (onbPw.text.length > 0 && onbPw.text === onbPw2.text)
                    color: !can ? "transparent"
                         : onbBtnMa.pressed ? root.brandRedPressed
                         : onbBtnMa.containsMouse ? root.brandRedHover : root.brandRed
                    border.color: can ? root.brandRed : root.borderColor
                    opacity: can ? 1 : 0.5
                    Behavior on color { ColorAnimation { duration: root.motionQuick } }
                    Text { font.family: root.faceFont;
                        anchors.centerIn: parent
                        text: root.secBusy.length > 0 ? root.secBusy + "…"
                            : root.walletState === "locked"    ? "Unlock"
                            : root.walletState === "plaintext" ? "Encrypt wallet"
                            : "Create wallet"
                        color: onbBtn.can ? root.textPrimary : root.textDisabled
                        font.pixelSize: 12; font.bold: onbBtn.can
                    }
                    MouseArea {
                        id: onbBtnMa
                        anchors.fill: parent; hoverEnabled: true; enabled: onbBtn.can && root.secBusy.length === 0
                        cursorShape: onbBtn.can ? Qt.PointingHandCursor : Qt.ArrowCursor
                        onClicked: {
                            // doSecureWallet asks the core which of createEncryptedWallet /
                            // encryptPlaintextWallet applies, so this one button is right for both
                            // "no wallet yet" and "a wallet with no password on it".
                            if (root.walletState === "locked") root.doUnlock(onbPw.text)
                            else root.doSecureWallet(onbPw.text)
                        }
                    }
                }

                // ESCAPE HATCH. Deliberately NOT enumerated per state: it shows for every state
                // this screen can be in except the two where it would be wrong ("new" - there is
                // nothing to erase; "backup" - nothing may compete with an unwritten recovery
                // phrase). Round 2's version listed "locked" only, so any other degraded state
                // arrived with no way out, which is how a user ends up stranded. A state this
                // build has never heard of still gets a reset here.
                Rectangle {
                    visible: root.walletState !== "new" && root.walletState !== "backup"
                    Layout.alignment: Qt.AlignHCenter; Layout.preferredWidth: 190; height: 22; radius: 10
                    color: "transparent"; border.color: root.borderColor
                    Text { font.family: root.faceFont;
                        anchors.centerIn: parent
                        text: root.resetArmed ? "Tap again to erase wallet"
                            : root.walletState === "locked" ? "Forgot password? Reset"
                            : "Not your wallet? Erase & start over"
                        color: root.errorRed; font.pixelSize: 9
                    }
                    MouseArea {
                        anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                        onClicked: { if (root.resetArmed) root.doResetWallet(); else root.resetArmed = true }
                    }
                }
                // The other two escapes are behind unlabelled icons in the top bar, which is not
                // discoverable, so name them: Security & Backup restores from a recovery phrase,
                // Settings names the wallet binary that has to exist for any of this to work.
                Text { font.family: root.faceFont;
                    visible: root.walletState !== "backup"
                    Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap
                    text: "Have a recovery phrase, or nothing working at all? The key and cog icons "
                        + "at the top open Security & Backup and Settings from here."
                    color: root.textDisabled; font.pixelSize: 9
                }
            }
        }

        // ── Tor connect progress - shown while a Tor zone is connecting ─────────
        Rectangle {
            visible: root.walletState === "ready" && root.screen === "main"
                     && root.activeZoneIsTor() && root.seqStatus !== "running"
            Layout.fillWidth: true; Layout.topMargin: 8
            Layout.preferredHeight: torCol.implicitHeight + 22
            radius: 16; color: root.panelColor; border.color: root.borderColor; border.width: 1
            ColumnLayout {
                id: torCol
                anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter
                          leftMargin: 16; rightMargin: 16 }
                spacing: 8
                RowLayout {
                    Layout.fillWidth: true; spacing: 9
                    Rectangle { width: 9; height: 9; radius: 4.5; Layout.alignment: Qt.AlignVCenter
                        color: root.torPercent >= 100 ? root.greenBright : root.connectGray
                        SequentialAnimation on opacity { running: true; loops: Animation.Infinite
                            NumberAnimation { to: 0.3; duration: 550 } NumberAnimation { to: 1.0; duration: 550 } } }
                    ColumnLayout { spacing: 0; Layout.fillWidth: true
                        Text { font.family: root.faceFont; text: "Connecting to " + root.zoneName(root.network)
                            color: root.textPrimary; font.pixelSize: 12; font.bold: true; elide: Text.ElideRight; Layout.fillWidth: true }
                        Text { font.family: root.faceFont
                            text: root.torPercent < 100 ? "Step 1 of 2 · Tor network" : "Step 2 of 2 · Sequencer onion"
                            color: root.silver; font.pixelSize: 9 } }
                    // % shown when we have a real number (bootstrap, or onion stage from the control port)
                    Text { visible: root.torPercent < 100 || root.torOnionStage.length > 0
                        font.family: root.faceFont
                        text: (root.torPercent < 100 ? root.torPercent : root.torOnionPct) + "%"
                        color: root.torPercent < 100 ? root.accentOrange : root.successGreen; font.pixelSize: 14; font.bold: true }
                }
                // Phase 1 (bootstrap) + Phase 2 with a real onion stage → determinate fill. Only the
                // brief gap before the first onion event falls back to an indeterminate sweep.
                Rectangle { id: torTrack; Layout.fillWidth: true; height: 6; radius: 3; color: root.inputBg; clip: true
                    property bool determinate: root.torPercent < 100 || root.torOnionStage.length > 0
                    Rectangle { visible: torTrack.determinate; height: parent.height; radius: 3
                        width: Math.max(0, torTrack.width * Math.min(100, (root.torPercent < 100 ? root.torPercent : root.torOnionPct)) / 100.0)
                        color: root.torPercent < 100 ? root.accentOrange : root.successGreen
                        Behavior on width { NumberAnimation { duration: 350; easing.type: Easing.OutCubic } } }
                    Rectangle { id: torSlider; visible: !torTrack.determinate; height: parent.height; radius: 3
                        width: torTrack.width * 0.35; color: root.successGreen
                        SequentialAnimation on x {
                            running: torSlider.visible; loops: Animation.Infinite
                            NumberAnimation { from: -torTrack.width * 0.35; to: torTrack.width
                                duration: 1300; easing.type: Easing.InOutQuad } } }
                }
                Text { font.family: root.faceFont; Layout.fillWidth: true; wrapMode: Text.WordWrap; elide: Text.ElideRight; maximumLineCount: 2
                    text: root.torPercent < 100
                         ? (root.torStage.length > 0 ? "Bootstrapping Tor - " + root.torStage : "Starting Tor…")
                         : (root.torOnionStage.length > 0 ? root.torOnionStage
                                                          : "Reaching the sequencer onion over Tor - this can take ~10-30s")
                    color: root.textSecondary; font.pixelSize: 9 }
            }
        }

        // ── Hero balance - cinematic charcoal card, silver rim + glow ───────────
        Rectangle {
            visible: root.walletState === "ready" && root.screen === "main"
            Layout.fillWidth: true
            Layout.preferredHeight: 142
            Layout.topMargin: root.sp2
            radius: root.rHero
            // Real elevation - soft drop shadow (autoPadding stops the shadow clipping).
            layer.enabled: true
            layer.effect: MultiEffect {
                shadowEnabled: true; autoPaddingEnabled: true
                shadowColor: "#000000"; shadowVerticalOffset: 4; shadowBlur: 0.45; shadowOpacity: 0.25
            }
            gradient: Gradient {
                GradientStop { position: 0.0;  color: "#5A1326" }   // crimson glow top
                GradientStop { position: 0.55; color: "#2A1018" }
                GradientStop { position: 1.0;  color: "#141417" }   // charcoal bottom
            }
            // hairline crimson rim for the glassy/metallic edge (noir surface kept; accent → crimson)
            Rectangle { anchors.fill: parent; radius: parent.radius; color: "transparent"
                border.color: root.brandRedTint22; border.width: 1 }

            ColumnLayout {
                anchors.centerIn: parent
                width: parent.width - 36
                spacing: root.sp1 + 2
                Text { font.family: root.faceFont; text: "TOTAL BALANCE"; color: root.textDisabled
                    font.pixelSize: root.fsXS; font.letterSpacing: 2; Layout.alignment: Qt.AlignHCenter }
                RowLayout {
                    Layout.alignment: Qt.AlignHCenter; spacing: root.sp2
                    Text { font.family: root.faceFont; text: root.heroTotal; font.pixelSize: root.fs3XL; font.weight: Font.DemiBold; color: root.textPrimary }
                    Text { font.family: root.faceFont; text: "LEZ"; font.pixelSize: root.fsLG; font.weight: Font.Medium; color: root.silver; Layout.alignment: Qt.AlignBottom; bottomPadding: 7 }
                }
                RowLayout {
                    Layout.alignment: Qt.AlignHCenter; spacing: 14
                    RowLayout { spacing: 5
                        Rectangle { width: 7; height: 7; radius: 4; color: root.greenBright; Layout.alignment: Qt.AlignVCenter }
                        Text { font.family: root.faceFont; text: root.heroPublicTotal + " public"; color: root.textSecondary; font.pixelSize: root.fsXS } }
                    RowLayout { spacing: 5
                        Rectangle { width: 7; height: 7; radius: 4; color: root.silver; Layout.alignment: Qt.AlignVCenter }
                        Text { font.family: root.faceFont; text: root.heroPrivateTotal + " private"; color: root.textSecondary; font.pixelSize: root.fsXS } }
                    Rectangle {
                        id: faucetChip
                        Layout.preferredWidth: 96; height: 26; radius: root.rChip
                        color: faucetChipMa.containsMouse ? root.accentTint14 : root.accentTint10
                        border.color: root.accentTint22; border.width: 1
                        Behavior on color { ColorAnimation { duration: root.motionQuick } }
                        Text { anchors.centerIn: parent; text: "⛲  Faucet"; color: root.silver; font.pixelSize: root.fsXS - 1; font.weight: Font.Medium; font.family: root.faceFont }
                        MouseArea { id: faucetChipMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: root.doClaimFaucet() }
                    }
                }
            }
        }

        // ── Round action buttons (Send / Receive / Privacy) ─────────────────────
        RowLayout {
            visible: root.walletState === "ready" && root.screen === "main"
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: 6
            spacing: 30
            Repeater {
                model: [ { k: "send",    g: "↑", t: "Send" },
                         { k: "receive", g: "↓", t: "Receive" },
                         { k: "privacy", g: "◈", t: "Privacy" } ]
                delegate: ColumnLayout {
                    required property var modelData
                    spacing: 7
                    property bool on: root.screen === modelData.k
                    Rectangle {
                        id: actBtn
                        Layout.alignment: Qt.AlignHCenter
                        width: 56; height: 56; radius: 28
                        color: parent.on ? root.accentOrange
                             : actBtnMa.containsMouse ? root.surface3 : root.surface2
                        border.color: parent.on ? root.accentHover : root.borderColor; border.width: 1
                        scale: actBtnMa.pressed ? 0.94 : 1.0
                        Behavior on color { ColorAnimation { duration: root.motionQuick } }
                        Behavior on scale { NumberAnimation { duration: root.motionQuick; easing.type: Easing.OutCubic } }
                        // Send/Receive keep their reliable text arrows; Privacy uses a real
                        // shield SVG (the ◈ glyph read poorly), colourized to the same tint.
                        Text { visible: modelData.k !== "privacy"
                            anchors.centerIn: parent; text: modelData.g; font.pixelSize: 22; font.weight: Font.Medium
                            color: actBtn.parent.on ? root.bgColor : root.textPrimary; font.family: root.faceFont }
                        Image { visible: modelData.k === "privacy"
                            source: "icons/shield.svg"
                            sourceSize.width: 24; sourceSize.height: 24
                            anchors.centerIn: parent
                            layer.enabled: true
                            layer.effect: MultiEffect {
                                colorization: 1.0
                                colorizationColor: actBtn.parent.on ? root.bgColor : root.textPrimary
                            }
                        }
                        MouseArea { id: actBtnMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                            onClicked: { if (modelData.k === "receive") root.refreshTokens(); root.screen = modelData.k } }
                    }
                    Text { Layout.alignment: Qt.AlignHCenter; font.family: root.faceFont; text: modelData.t
                        color: parent.on ? root.silver : root.textSecondary; font.pixelSize: root.fsXS; font.weight: Font.Medium }
                }
            }
        }

        // ── Tab bar (Tokens / Activity) ─────────────────────────────────────────
        RowLayout {
            visible: root.walletState === "ready" && root.screen === "main"
            Layout.fillWidth: true
            Layout.topMargin: 6
            spacing: 0
            Repeater {
                model: [ { k: "tokens", t: "Tokens" }, { k: "activity", t: "Activity" } ]
                delegate: Rectangle {
                    required property var modelData
                    Layout.fillWidth: true; height: 30
                    color: "transparent"
                    Text { anchors.centerIn: parent; font.family: root.faceFont; text: modelData.t
                        color: root.activeTab === modelData.k ? root.textPrimary : root.textDisabled
                        font.pixelSize: 12; font.bold: root.activeTab === modelData.k }
                    Rectangle { anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                        height: 2; color: root.activeTab === modelData.k ? root.accentOrange : root.borderColor }
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                        onClicked: { root.activeTab = modelData.k; if (modelData.k === "activity") root.refreshTxHistory() } }
                }
            }
        }

        // ── Body (single column, MetaMask) - scrolls ───────────────────────────
        Flickable {
            id: mainFlick
            visible: root.walletState === "ready" && (root.screen === "main" || root.screen === "send" || root.screen === "receive" || root.screen === "privacy")
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: width
            contentHeight: scrollCol.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            ColumnLayout {
                id: scrollCol
                width: mainFlick.width
                spacing: 8

                // ── Tokens tab ──
                ColumnLayout {
                    visible: root.activeTab === "tokens" && root.screen === "main"
                    Layout.fillWidth: true
                    spacing: 6

                    Rectangle {   // native LEZ row
                        Layout.fillWidth: true; height: 46; radius: 12
                        color: root.panelColor; border.color: root.borderColor; border.width: 1
                        // Real elevation - soft drop shadow (autoPadding stops the shadow clipping).
                        layer.enabled: true
                        layer.effect: MultiEffect {
                            shadowEnabled: true; autoPaddingEnabled: true
                            shadowColor: "#000000"; shadowVerticalOffset: 4; shadowBlur: 0.45; shadowOpacity: 0.25
                        }
                        RowLayout {
                            anchors { fill: parent; leftMargin: 10; rightMargin: 12 }
                            spacing: 10
                            Rectangle { width: 26; height: 26; radius: 13; color: root.successGreen; Layout.alignment: Qt.AlignVCenter
                                Text { anchors.centerIn: parent; text: "Ł"; color: root.textPrimary; font.pixelSize: 14; font.bold: true; font.family: root.faceFont } }
                            ColumnLayout { spacing: 0
                                Text { font.family: root.faceFont; text: "LEZ"; color: root.textPrimary; font.pixelSize: 12; font.bold: true }
                                Text { font.family: root.faceFont; text: "Native token"; color: root.textDisabled; font.pixelSize: 9 } }
                            Item { Layout.fillWidth: true }
                            Text { font.family: root.faceFont
                                text: (root.selectedFromBalance !== "" && root.selectedFromBalance !== "-") ? root.selectedFromBalance : "0"
                                color: root.textPrimary; font.pixelSize: 15; font.bold: true }
                        }
                    }

                    Repeater {   // token holdings
                        model: root.selectedTokens
                        delegate: Rectangle {
                            required property var modelData
                            Layout.fillWidth: true; height: 46; radius: 12
                            color: root.panelColor; border.color: root.borderColor; border.width: 1
                            // Real elevation - soft drop shadow (autoPadding stops the shadow clipping).
                            layer.enabled: true
                            layer.effect: MultiEffect {
                                shadowEnabled: true; autoPaddingEnabled: true
                                shadowColor: "#000000"; shadowVerticalOffset: 4; shadowBlur: 0.45; shadowOpacity: 0.25
                            }
                            RowLayout {
                                anchors { fill: parent; leftMargin: 10; rightMargin: 12 }
                            spacing: 10
                                Rectangle { width: 26; height: 26; radius: 13; color: root.avatarColor(modelData.definitionId); Layout.alignment: Qt.AlignVCenter
                                    Text { anchors.centerIn: parent; text: modelData.ticker.substring(0,1); color: root.textPrimary; font.pixelSize: 13; font.bold: true; font.family: root.faceFont } }
                                ColumnLayout { spacing: 0
                                    Text { font.family: root.faceFont; text: modelData.ticker; color: root.textPrimary; font.pixelSize: 12; font.bold: true }
                                    Text { font.family: root.faceFont; text: "Token"; color: root.textDisabled; font.pixelSize: 9 } }
                                Item { Layout.fillWidth: true }
                                Text { font.family: root.faceFont; text: modelData.balance; color: root.textPrimary; font.pixelSize: 15; font.bold: true }
                            }
                        }
                    }

                    Rectangle {   // open the Add-token screen (whitelist picker + custom id)
                        Layout.fillWidth: true; height: 32; radius: 12
                        color: "transparent"; border.color: root.accentOrange
                        Text { anchors.centerIn: parent; text: "+ Add token"; color: root.accentOrange; font.pixelSize: 11; font.family: root.faceFont }
                        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                            onClicked: { root.refreshWhitelist(); root.screen = "addtoken" } }
                    }
                }

                // ── Send screen ──
                ColumnLayout {
                    visible: root.screen === "send"
                    Layout.fillWidth: true
                    spacing: 8

                    RowLayout {   // back header
                        Layout.fillWidth: true; spacing: 6
                        Rectangle { width: 26; height: 24; radius: 10; color: "transparent"; border.color: root.borderColor
                            Text { anchors.centerIn: parent; text: "←"; color: root.textSecondary; font.pixelSize: 13; font.family: root.faceFont }
                            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.screen = "main" } }
                        Text { font.family: root.faceFont; text: "Send"; color: root.textPrimary; font.pixelSize: 13; font.bold: true }
                        Item { Layout.fillWidth: true }
                    }

                    // Asset selector - LEZ + the account's tokens
                    Text { font.family: root.faceFont; text: "Asset"; color: root.textSecondary; font.pixelSize: 10 }
                    Flow {
                        Layout.fillWidth: true; spacing: 5
                        Rectangle {   // LEZ
                            width: lezChip.implicitWidth + 18; height: 24; radius: 12
                            color: root.sendTokenDef === "" ? root.accentTint14 : "transparent"
                            border.color: root.sendTokenDef === "" ? root.accentOrange : root.borderColor
                            Text { id: lezChip; anchors.centerIn: parent; text: "LEZ"; font.pixelSize: 10; font.family: root.faceFont
                                color: root.sendTokenDef === "" ? root.accentOrange : root.textSecondary }
                            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: { root.sendTokenDef = ""; root.sendTokenName = "LEZ" } }
                        }
                        Repeater {
                            model: root.selectedTokens
                            delegate: Rectangle {
                                required property var modelData
                                width: tChip.implicitWidth + 18; height: 24; radius: 12
                                color: root.sendTokenDef === modelData.definitionId ? root.accentTint14 : "transparent"
                                border.color: root.sendTokenDef === modelData.definitionId ? root.accentOrange : root.borderColor
                                Text { id: tChip; anchors.centerIn: parent; text: modelData.ticker + " · " + modelData.balance; font.pixelSize: 10; font.family: root.faceFont
                                    color: root.sendTokenDef === modelData.definitionId ? root.accentOrange : root.textSecondary }
                                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: { root.sendTokenDef = modelData.definitionId; root.sendTokenName = modelData.ticker } }
                            }
                        }
                    }

                    Text { font.family: root.faceFont; text: "To"; color: root.textSecondary; font.pixelSize: 10 }
                    Rectangle {
                        Layout.fillWidth: true; height: 26; color: root.inputBg
                        border.color: toField.activeFocus ? root.accentOrange : root.borderColor; radius: 8
                        TextInput { font.family: root.faceFont;
                            id: toField
                            anchors { fill: parent; leftMargin: 6; rightMargin: 6 }
                            verticalAlignment: TextInput.AlignVCenter
                            color: root.textPrimary; font.pixelSize: 11; clip: true
                            Text { font.family: root.faceFont;
                                anchors.fill: parent; verticalAlignment: Text.AlignVCenter
                                text: parent.text.length === 0 ? "recipient account id" : ""
                                color: root.textDisabled; font.pixelSize: 11;                            }
                        }
                    }

                    RowLayout { Layout.fillWidth: true
                        Text { font.family: root.faceFont; text: "Amount (" + (root.sendTokenDef === "" ? "LEZ" : root.sendTokenName) + ")"; color: root.textSecondary; font.pixelSize: 10 }
                        Item { Layout.fillWidth: true }
                        Text { font.family: root.faceFont; font.pixelSize: 10
                            color: root.sendBalance() > 0 ? root.silver : root.errorRed
                            text: root.sendBalance() > 0 ? ("available: " + root.sendBalance()) : "no balance" } }
                    Rectangle {
                        Layout.fillWidth: true; height: 26; color: root.inputBg
                        border.color: amountField.activeFocus ? root.accentOrange : root.borderColor; radius: 8
                        TextInput { font.family: root.faceFont;
                            id: amountField
                            anchors { fill: parent; leftMargin: 6; rightMargin: 6 }
                            verticalAlignment: TextInput.AlignVCenter
                            color: root.textPrimary; font.pixelSize: 11; clip: true
                            inputMethodHints: Qt.ImhDigitsOnly
                            validator: IntValidator { bottom: 0 }   // whole LEZ only - no decimals
                            Text { font.family: root.faceFont;
                                anchors.fill: parent; verticalAlignment: Text.AlignVCenter
                                text: parent.text.length === 0 ? "e.g. 10" : ""
                                color: root.textDisabled; font.pixelSize: 11;                            }
                        }
                    }

                    // Signing is a gated verb, so it cannot succeed while the store is plaintext.
                    // Say so where the button is, not after it fails.
                    Text { font.family: root.faceFont; visible: root.signingBlocked
                        text: "Sending needs a wallet that can prove who is asking. This store has no "
                            + "password on it, so the send would be refused. Set a password in "
                            + "Security & Backup (it keeps your accounts), then unlock."
                        color: root.warningAmber; font.pixelSize: 10
                        wrapMode: Text.WordWrap; Layout.fillWidth: true }

                    Rectangle {
                        id: confirmBtn
                        Layout.fillWidth: true; height: 36; radius: 10
                        property bool canSend: !root.signingBlocked
                                               && root.selectedFromId.length > 0
                                               && toField.text.trim().length > 0
                                               && amountField.text.trim().length > 0
                        color: !canSend ? "transparent"
                             : confirmSendMa.pressed ? root.brandRedPressed
                             : confirmSendMa.containsMouse ? root.brandRedHover : root.brandRed
                        border.color: canSend ? root.brandRed : root.borderColor
                        opacity: canSend ? 1.0 : 0.4
                        Behavior on color { ColorAnimation { duration: root.motionQuick } }

                        Text { font.family: root.faceFont;
                            anchors.centerIn: parent
                            text: root.sendTokenDef === "" ? "Confirm Send" : ("Send " + root.sendTokenName)
                            color: confirmBtn.canSend ? root.textPrimary : root.textDisabled
                            font.pixelSize: 12; font.bold: confirmBtn.canSend
                        }

                        MouseArea {
                            id: confirmSendMa
                            anchors.fill: parent; hoverEnabled: true
                            cursorShape: confirmBtn.canSend ? Qt.PointingHandCursor : Qt.ArrowCursor
                            enabled: confirmBtn.canSend
                            onClicked: root.doSend(toField.text.trim(), amountField.text.trim())
                        }
                    }
                    Text { font.family: root.faceFont; visible: root.sendTokenDef !== ""
                        text: "Token sends create the recipient's token account and confirm on-chain - runs in the background (~30s)."
                        color: root.textDisabled; font.pixelSize: 9; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                }

                // ── Receive screen ──
                ColumnLayout {
                    visible: root.screen === "receive" && root.selectedFromId.length > 0
                    Layout.fillWidth: true
                    id: recvCol
                    spacing: 8
                    RowLayout {   // back header
                        Layout.fillWidth: true; spacing: 6
                        Rectangle { width: 26; height: 24; radius: 10; color: "transparent"; border.color: root.borderColor
                            Text { anchors.centerIn: parent; text: "←"; color: root.textSecondary; font.pixelSize: 13; font.family: root.faceFont }
                            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.screen = "main" } }
                        Text { font.family: root.faceFont; text: "Receive"; color: root.textPrimary; font.pixelSize: 13; font.bold: true }
                        Item { Layout.fillWidth: true }
                    }
                        Text { font.family: root.faceFont; text: "Share this account address to receive LEZ or tokens."
                            color: root.textDisabled; font.pixelSize: 9; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                        Rectangle {
                            Layout.fillWidth: true; Layout.preferredHeight: recvAddr.implicitHeight + 14
                            color: root.inputBg; border.color: root.borderColor; radius: 10
                            Text { id: recvAddr; x: 8; y: 7; width: parent.width - 16
                                text: root.selectedFromId; color: root.textPrimary; font.pixelSize: 11; font.family: root.faceFont; wrapMode: Text.WrapAnywhere }
                            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                                onClicked: { clipHelper.text = root.selectedFromId; clipHelper.selectAll(); clipHelper.copy(); root.logActivity("Address copied", false) } }
                        }
                        Text { font.family: root.faceFont; text: "Tap to copy"; color: root.textDisabled; font.pixelSize: 9 }
                        // private accounts: reveal receive keys (npk/vpk) for foreign senders
                        RowLayout {
                            visible: root.selectedFromType === "private"
                            Layout.fillWidth: true; spacing: 6
                            Rectangle { Layout.preferredWidth: 150; height: 24; radius: 10; color: "transparent"; border.color: root.borderColor
                                Text { font.family: root.faceFont; anchors.centerIn: parent; text: "Show receive keys (npk/vpk)"; color: root.textSecondary; font.pixelSize: 9 }
                                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.showReceiveKeys(root.selectedFromId) } }
                        }
                        ColumnLayout {
                            visible: root.receiveKeys !== null && root.selectedFromType === "private"
                            Layout.fillWidth: true; spacing: 2
                            Text { font.family: root.faceFont; visible: text.length > 0; Layout.fillWidth: true; elide: Text.ElideRight
                                text: root.receiveKeys && root.receiveKeys.npk ? ("npk " + root.receiveKeys.npk) : ""
                                color: root.textDisabled; font.pixelSize: 9 }
                            Text { font.family: root.faceFont; visible: text.length > 0; Layout.fillWidth: true; elide: Text.ElideRight
                                text: root.receiveKeys && root.receiveKeys.vpk ? ("vpk " + root.receiveKeys.vpk) : ""
                                color: root.textDisabled; font.pixelSize: 9 }
                        }
                }

                // ── Privacy screen - shield / deshield ──
                Rectangle {
                    visible: root.screen === "privacy"
                    Layout.fillWidth: true
                    height: visible ? privCol.implicitHeight + 20 : 0
                    color: root.panelColor; border.color: root.borderColor; border.width: 1; radius: 12
                    // Token choices are chain state - refresh them each time the screen opens.
                    onVisibleChanged: if (visible) root.refreshPrivAssets()

                    ColumnLayout {
                        id: privCol
                        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 10 }
                        spacing: 8

                        RowLayout {   // back header
                            Layout.fillWidth: true; spacing: 6
                            Rectangle { width: 26; height: 24; radius: 10; color: "transparent"; border.color: root.borderColor
                                Text { anchors.centerIn: parent; text: "←"; color: root.textSecondary; font.pixelSize: 13; font.family: root.faceFont }
                                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.screen = "main" } }
                            Text { font.family: root.faceFont; text: "Privacy - shield / deshield"; color: root.textPrimary; font.pixelSize: 13; font.bold: true }
                            Item { Layout.fillWidth: true }
                        }

                        // Privacy screen stays native-only BY CHOICE. Token shield/deshield exist
                        // (wrapper token-shield/token-deshield via startShield/startDeshield with a
                        // definitionId) but on rc5 the private-send source must be a DIRECT-owned
                        // token holding - ATAs can't sign - so for typical ATA-held tokens it fails;
                        // exposed to dApps via Connect, not in this screen. privAsset stays "native".

                        // Mode selector
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6
                            Text { font.family: root.faceFont; text: "Mode"; color: root.textSecondary; font.pixelSize: 10; Layout.preferredWidth: 48 }
                            Repeater {
                                model: [ { k: "shield", t: "Shield" }, { k: "deshield", t: "Deshield" } ]
                                delegate: Rectangle {
                                    required property var modelData
                                    Layout.fillWidth: true; height: 24; radius: 10
                                    color: root.privMode === modelData.k ? root.accentTint14 : "transparent"
                                    border.color: root.privMode === modelData.k ? root.accentOrange : root.borderColor
                                    Text { font.family: root.faceFont;
                                        anchors.centerIn: parent; text: modelData.t
                                        color: root.privMode === modelData.k ? root.accentOrange : root.textSecondary; font.pixelSize: 10
                                    }
                                    MouseArea {
                                        anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                                        onClicked: { root.privMode = modelData.k; root.privToId = ""; root.deshieldAck = false
                                                     root.privAsset = "native"; root.privTokenDef = ""; root.privTokenTicker = "" }
                                    }
                                }
                            }
                        }

                        // Asset - LEZ or a token. Shield can only source a token from a DIRECT-owned
                        // holding (rc5: ATAs can't sign a private send), so the shield list comes from
                        // getDirectHoldings; deshield lists every known token (pick the one the private
                        // account holds - the def routes the tokens into the recipient's ATA).
                        RowLayout {
                            visible: root.privMode === "shield" || root.privMode === "deshield"
                            Layout.fillWidth: true
                            spacing: 6
                            Text { font.family: root.faceFont; text: "Asset"; color: root.textSecondary; font.pixelSize: 10; Layout.preferredWidth: 48 }
                            Flow {
                                Layout.fillWidth: true
                                spacing: 4
                                Repeater {
                                    model: [{ definitionId: "", ticker: "LEZ", balance: "" }].concat(
                                               root.privMode === "shield" ? root.shieldableTokens
                                                                          : root.registryTokens)
                                    delegate: Rectangle {
                                        required property var modelData
                                        height: 22; radius: 11
                                        width: assetChipText.implicitWidth + 18
                                        color: root.privTokenDef === modelData.definitionId ? root.accentTint14 : "transparent"
                                        border.color: root.privTokenDef === modelData.definitionId ? root.accentOrange : root.borderColor
                                        Text { font.family: root.faceFont;
                                            id: assetChipText
                                            anchors.centerIn: parent
                                            text: modelData.ticker + (modelData.balance ? " · " + modelData.balance : "")
                                                  + (modelData.ataTotal && modelData.ataTotal !== "0"
                                                     ? "  (+" + modelData.ataTotal + " unshielded)" : "")
                                            color: root.privTokenDef === modelData.definitionId ? root.accentOrange : root.textSecondary
                                            font.pixelSize: 9
                                        }
                                        MouseArea {
                                            anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                                            onClicked: {
                                                root.privAsset       = modelData.definitionId ? "token" : "native"
                                                root.privTokenDef    = modelData.definitionId
                                                root.privTokenTicker = modelData.definitionId ? modelData.ticker : ""
                                            }
                                        }
                                    }
                                }
                            }
                        }
                        Text { font.family: root.faceFont;
                            visible: root.privMode === "shield" && root.shieldableTokens.length === 0
                            text: "Tokens in regular (ATA) balances can't be shielded on this chain version - only direct-owned holdings (e.g. a token you minted). LEZ shielding is unaffected."
                            wrapMode: Text.WordWrap; Layout.fillWidth: true
                            color: root.textDisabled; font.pixelSize: 9
                        }

                        // ⚠ De-anonymizing warning (deshield / foreign transfer) - mandatory ack
                        Rectangle {
                            visible: root.privMode === "deshield" || (root.privMode === "transfer" && root.privToMode === "foreign")
                            Layout.fillWidth: true
                            Layout.preferredHeight: warnCol.implicitHeight + 16
                            color: Qt.rgba(251/255, 113/255, 133/255, 0.10)
                            border.color: root.errorRed; border.width: 1; radius: 10
                            ColumnLayout {
                                id: warnCol
                                anchors { left: parent.left; right: parent.right; top: parent.top; margins: 8 }
                                spacing: 4
                                Text { font.family: root.faceFont; text: "⚠  You are revealing transaction info"
                                    color: root.errorRed; font.pixelSize: 11; font.bold: true }
                                Text {
                                    font.family: root.faceFont; Layout.fillWidth: true; wrapMode: Text.WordWrap
                                    color: root.textSecondary; font.pixelSize: 9
                                    text: root.privMode === "deshield"
                                        ? "Deshielding moves funds from a PRIVATE account to a PUBLIC one. The amount and destination become visible on-chain and link to your public identity."
                                        : "Sending to a FOREIGN recipient exposes the recipient's keys and de-anonymizes this transfer."
                                }
                                RowLayout {
                                    spacing: 6
                                    Rectangle {
                                        width: 14; height: 14; radius: 2; border.color: root.borderColor; border.width: 1
                                        color: root.deshieldAck ? root.accentOrange : "transparent"
                                        Text { anchors.centerIn: parent; visible: root.deshieldAck; text: "✓"; color: root.bgColor; font.pixelSize: 10; font.family: root.faceFont }
                                        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.deshieldAck = !root.deshieldAck }
                                    }
                                    Text { font.family: root.faceFont; text: "I understand this reveals transaction info"
                                        color: root.textSecondary; font.pixelSize: 9 }
                                }
                            }
                        }

                        // From (uses the account selected in the left column)
                        Text { font.family: root.faceFont; text: "From"; color: root.textSecondary; font.pixelSize: 10 }
                        Rectangle {
                            Layout.fillWidth: true; height: 26; radius: 8
                            color: root.inputBg; border.color: root.privFromValid ? root.borderColor : root.errorRed
                            RowLayout {
                                anchors { fill: parent; leftMargin: 6; rightMargin: 6 }
                                Text { font.family: root.faceFont;
                                    text: root.selectedFromId.length > 0 ? root.displayId(root.selectedFromId) : "- pick an account from the selector above -"
                                    color: root.selectedFromId.length > 0 ? root.textPrimary : root.textDisabled
                                    font.pixelSize: 11; elide: Text.ElideMiddle
                                    Layout.fillWidth: true
                                }
                                Text { font.family: root.faceFont;
                                    visible: root.selectedFromId.length > 0
                                    text: (root.selectedFromType || "public").toUpperCase()
                                    color: root.privFromValid ? root.successGreen : root.errorRed
                                    font.pixelSize: 9; font.bold: true
                                }
                            }
                        }
                        Text { font.family: root.faceFont;
                            visible: root.selectedFromId.length > 0 && !root.privFromValid
                            text: root.privMode === "shield" ? "Shield needs a PUBLIC source account."
                                                             : "Deshield / transfer needs a PRIVATE source account."
                            color: root.errorRed; font.pixelSize: 10; wrapMode: Text.WordWrap; Layout.fillWidth: true
                        }

                        // Transfer recipient sub-mode (owned | foreign)
                        RowLayout {
                            visible: root.privMode === "transfer"
                            Layout.fillWidth: true
                            spacing: 6
                            Text { font.family: root.faceFont; text: "To"; color: root.textSecondary; font.pixelSize: 10; Layout.preferredWidth: 48 }
                            Repeater {
                                model: [ { k: "owned", t: "Owned" }, { k: "foreign", t: "Foreign" } ]
                                delegate: Rectangle {
                                    required property var modelData
                                    Layout.preferredWidth: 70; height: 24; radius: 10
                                    color: root.privToMode === modelData.k ? root.accentTint14 : "transparent"
                                    border.color: root.privToMode === modelData.k ? root.accentOrange : root.borderColor
                                    Text { font.family: root.faceFont;
                                        anchors.centerIn: parent; text: modelData.t
                                        color: root.privToMode === modelData.k ? root.accentOrange : root.textSecondary; font.pixelSize: 10
                                    }
                                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: { root.privToMode = modelData.k; root.deshieldAck = false } }
                                }
                            }
                            Item { Layout.fillWidth: true }
                        }

                        // Owned destination - pick from eligible owned accounts (chips)
                        ColumnLayout {
                            visible: !(root.privMode === "transfer" && root.privToMode === "foreign")
                            Layout.fillWidth: true
                            spacing: 4
                            Text { font.family: root.faceFont;
                                text: root.privMode === "shield" ? "To (private account)"
                                    : root.privMode === "deshield" ? "To (public account)"
                                    : "To (private account)"
                                color: root.textSecondary; font.pixelSize: 10
                            }
                            Flow {
                                Layout.fillWidth: true
                                spacing: 4
                                Repeater {
                                    model: root.eligibleTo
                                    delegate: Rectangle {
                                        required property var modelData
                                        height: 22; radius: 11
                                        width: chipText.implicitWidth + 18
                                        color: root.privToId === modelData ? root.accentTint14 : "transparent"
                                        border.color: root.privToId === modelData ? root.accentOrange : root.borderColor
                                        Text { font.family: root.faceFont;
                                            id: chipText
                                            anchors.centerIn: parent
                                            text: root.displayId(modelData)
                                            color: root.privToId === modelData ? root.accentOrange : root.textSecondary
                                            font.pixelSize: 9;                                        }
                                        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.privToId = modelData }
                                    }
                                }
                            }
                            Text { font.family: root.faceFont;
                                visible: root.eligibleTo.length === 0
                                text: root.privMode === "deshield" ? "No public accounts - create one on the left."
                                                                   : "No FRESH private accounts - tap “+ Private” on the left. (A private account that already holds funds can't receive again - protocol limit.)"
                                wrapMode: Text.WordWrap
                                Layout.fillWidth: true
                                color: root.textDisabled; font.pixelSize: 10
                            }
                        }

                        // Foreign destination - recipient-supplied keys
                        ColumnLayout {
                            visible: root.privMode === "transfer" && root.privToMode === "foreign"
                            Layout.fillWidth: true
                            spacing: 4

                            Text { font.family: root.faceFont; text: "Recipient npk (32-byte hex)"; color: root.textSecondary; font.pixelSize: 10 }
                            Rectangle {
                                Layout.fillWidth: true; height: 24; color: root.inputBg
                                border.color: npkField.activeFocus ? root.accentOrange : root.borderColor; radius: 8
                                TextInput { font.family: root.faceFont;
                                    id: npkField
                                    anchors { fill: parent; leftMargin: 6; rightMargin: 6 }
                                    verticalAlignment: TextInput.AlignVCenter
                                    color: root.textPrimary; font.pixelSize: 10; clip: true
                                    text: root.privToNpk
                                    onTextEdited: root.privToNpk = text
                                }
                            }
                            Text { font.family: root.faceFont; text: "Recipient vpk (33-byte hex)"; color: root.textSecondary; font.pixelSize: 10 }
                            Rectangle {
                                Layout.fillWidth: true; height: 24; color: root.inputBg
                                border.color: vpkField.activeFocus ? root.accentOrange : root.borderColor; radius: 8
                                TextInput { font.family: root.faceFont;
                                    id: vpkField
                                    anchors { fill: parent; leftMargin: 6; rightMargin: 6 }
                                    verticalAlignment: TextInput.AlignVCenter
                                    color: root.textPrimary; font.pixelSize: 10; clip: true
                                    text: root.privToVpk
                                    onTextEdited: root.privToVpk = text
                                }
                            }
                            Text { font.family: root.faceFont; text: "Recipient identifier"; color: root.textSecondary; font.pixelSize: 10 }
                            Rectangle {
                                Layout.fillWidth: true; height: 24; color: root.inputBg
                                border.color: identField.activeFocus ? root.accentOrange : root.borderColor; radius: 8
                                TextInput { font.family: root.faceFont;
                                    id: identField
                                    anchors { fill: parent; leftMargin: 6; rightMargin: 6 }
                                    verticalAlignment: TextInput.AlignVCenter
                                    color: root.textPrimary; font.pixelSize: 10; clip: true
                                    text: root.privToIdent
                                    onTextEdited: root.privToIdent = text
                                }
                            }
                        }

                        // Reveal my own receive keys (to share for incoming foreign transfers)
                        RowLayout {
                            visible: root.privMode === "transfer"
                            Layout.fillWidth: true
                            spacing: 6
                            Rectangle {
                                Layout.preferredWidth: 130; height: 22; radius: 10; color: "transparent"; border.color: root.borderColor
                                Text { font.family: root.faceFont; anchors.centerIn: parent; text: "Show my receive keys"; color: root.textSecondary; font.pixelSize: 10 }
                                MouseArea {
                                    anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                                    enabled: root.selectedFromType === "private"
                                    onClicked: root.showReceiveKeys(root.selectedFromId)
                                }
                            }
                            Text { font.family: root.faceFont;
                                visible: root.selectedFromType !== "private"
                                text: "select a private account first"
                                color: root.textDisabled; font.pixelSize: 9
                            }
                        }
                        ColumnLayout {
                            visible: root.privMode === "transfer" && root.receiveKeys !== null
                            Layout.fillWidth: true
                            spacing: 2
                            Text { font.family: root.faceFont;
                                text: root.receiveKeys && root.receiveKeys.npk ? ("npk " + root.receiveKeys.npk) : ""
                                visible: text.length > 0
                                color: root.textDisabled; font.pixelSize: 9;                                Layout.fillWidth: true; elide: Text.ElideRight
                            }
                            Text { font.family: root.faceFont;
                                text: root.receiveKeys && root.receiveKeys.vpk ? ("vpk " + root.receiveKeys.vpk) : ""
                                visible: text.length > 0
                                color: root.textDisabled; font.pixelSize: 9;                                Layout.fillWidth: true; elide: Text.ElideRight
                            }
                        }

                        // Amount
                        Text { font.family: root.faceFont; text: "Amount (" + (root.privAsset === "token" ? (root.privTokenTicker || "tokens") : "LEZ") + ")"; color: root.textSecondary; font.pixelSize: 10 }
                        Rectangle {
                            Layout.fillWidth: true; height: 26; color: root.inputBg
                            border.color: privAmountField.activeFocus ? root.accentOrange : root.borderColor; radius: 8
                            TextInput { font.family: root.faceFont;
                                id: privAmountField
                                anchors { fill: parent; leftMargin: 6; rightMargin: 6 }
                                verticalAlignment: TextInput.AlignVCenter
                                color: root.textPrimary; font.pixelSize: 11; clip: true
                                inputMethodHints: Qt.ImhDigitsOnly
                                text: root.privAmount
                                onTextEdited: root.privAmount = text
                                Text { font.family: root.faceFont;
                                    anchors.fill: parent; verticalAlignment: Text.AlignVCenter
                                    text: parent.text.length === 0 ? "e.g. 10" : ""
                                    color: root.textDisabled; font.pixelSize: 11;                                }
                            }
                        }

                        // Shield / deshield / private transfer are all gated verbs, so none of
                        // them can succeed while the store is plaintext. Same rule as Send.
                        Text { font.family: root.faceFont; visible: root.signingBlocked
                            text: "Shielding, de-shielding and private transfers need a wallet that can "
                                + "prove who is asking. This store has no password on it, so they would "
                                + "be refused. Set a password in Security & Backup, then unlock."
                            color: root.warningAmber; font.pixelSize: 10
                            wrapMode: Text.WordWrap; Layout.fillWidth: true }

                        // Confirm
                        Rectangle {
                            id: privConfirmBtn
                            Layout.fillWidth: true; height: 36; radius: 10
                            // de-anonymizing modes (deshield, foreign transfer) require an explicit ack
                            readonly property bool needsAck:
                                root.privMode === "deshield" || (root.privMode === "transfer" && root.privToMode === "foreign")
                            property bool canConfirm:
                                !root.signingBlocked &&
                                root.privFromValid && !root.privBusy && root.privAmount.trim().length > 0 &&
                                ( (root.privMode === "transfer" && root.privToMode === "foreign")
                                    ? (root.privToNpk.trim().length > 0 && root.privToVpk.trim().length > 0 && root.privToIdent.trim().length > 0)
                                    : root.privToId.length > 0 ) &&
                                ( !needsAck || root.deshieldAck )
                            color: !canConfirm ? "transparent"
                                 : privConfirmMa.pressed ? root.brandRedPressed
                                 : privConfirmMa.containsMouse ? root.brandRedHover : root.brandRed
                            border.color: canConfirm ? root.brandRed : root.borderColor
                            opacity: canConfirm ? 1.0 : 0.4
                            Behavior on color { ColorAnimation { duration: root.motionQuick } }
                            Text { font.family: root.faceFont;
                                anchors.centerIn: parent
                                text: root.privBusy ? "Submitting…"
                                    : root.privMode === "shield" ? "Shield"
                                    : root.privMode === "deshield" ? "Deshield"
                                    : "Send privately"
                                color: privConfirmBtn.canConfirm ? root.textPrimary : root.textDisabled
                                font.pixelSize: 12; font.bold: privConfirmBtn.canConfirm
                            }
                            MouseArea {
                                id: privConfirmMa
                                anchors.fill: parent; hoverEnabled: true
                                cursorShape: privConfirmBtn.canConfirm ? Qt.PointingHandCursor : Qt.ArrowCursor
                                enabled: privConfirmBtn.canConfirm
                                onClicked: root.startPrivacyOp()
                            }
                        }

                        Text { font.family: root.faceFont;
                            text: "Generates a STARK locally - fast in dev-mode, several minutes on CPU. Runs in the background."
                            color: root.textDisabled; font.pixelSize: 9; wrapMode: Text.WordWrap; Layout.fillWidth: true
                        }
                    }
                }

                // (Privacy jobs panel relocated to a sticky bottom bar - see "jobsBar"
                //  below, as a sibling of mainFlick so it pins to the bottom.)

                // Wallet history - opened by the History button (otherwise hidden)
                Rectangle {
                    visible: root.activeTab === "activity"
                    Layout.fillWidth: true
                    Layout.preferredHeight: 320
                    color: root.panelColor; border.color: root.borderColor; border.width: 1; radius: 12

                    ColumnLayout {
                        anchors { fill: parent; margins: 8 }
                        spacing: 6

                        Text { font.family: root.faceFont;
                            text: "HISTORY"
                            color: root.textDisabled; font.pixelSize: 9; font.bold: true; font.letterSpacing: 1.2
                        }

                        Text { font.family: root.faceFont;
                            visible: txHistoryModel.count === 0
                            text: "No transactions yet"
                            color: root.textDisabled; font.pixelSize: 11
                            Layout.alignment: Qt.AlignHCenter
                        }

                        ListView {
                            id: txHistoryView
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            model: ListModel { id: txHistoryModel }
                            clip: true; spacing: 4

                            delegate: Rectangle {
                                required property string type
                                required property string amount
                                required property string ts
                                required property string sender
                                required property string receiver
                                width: txHistoryView.width
                                height: txRow.implicitHeight + 10
                                color: "transparent"
                                radius: 8

                                property bool isSent: type !== "faucet" && sender === root.selectedFromId
                                property string direction:
                                      type === "faucet"   ? "Faucet"
                                    : type === "shield"   ? "Shield"
                                    : type === "deshield" ? "Deshield"
                                    : type === "private"  ? (isSent ? "Sent (private)" : "Received (private)")
                                    : isSent ? "Sent" : "Received"
                                property string counterparty: isSent ? receiver : sender

                                ColumnLayout {
                                    id: txRow
                                    anchors { left: parent.left; right: parent.right; top: parent.top; topMargin: 5; leftMargin: 4; rightMargin: 4 }
                                    spacing: 2

                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: 6
                                        Text { font.family: root.faceFont;
                                            text: direction
                                            color: root.textSecondary
                                            font.pixelSize: 11; font.bold: true
                                        }
                                        Text { font.family: root.faceFont;
                                            text: amount + " LEZ"
                                            color: root.textPrimary; font.pixelSize: 11; font.bold: true
                                            Layout.fillWidth: true
                                        }
                                        Text { font.family: root.faceFont;
                                            text: ts.length > 16
                                                  ? ts.substring(0, 10) + "  " + ts.substring(11, 16)
                                                  : ts
                                            color: root.textDisabled; font.pixelSize: 10
                                        }
                                    }

                                    Text { font.family: root.faceFont;
                                        visible: type !== "faucet"
                                        text: (isSent ? "→ " : "← ") + root.displayId(counterparty)
                                        color: root.textDisabled; font.pixelSize: 10;                                        Layout.fillWidth: true; elide: Text.ElideMiddle
                                    }
                                }

                                Rectangle {
                                    anchors { bottom: parent.bottom; left: parent.left; right: parent.right }
                                    height: 1; color: root.borderColor
                                    visible: index < txHistoryModel.count - 1
                                }
                            }
                        }
                    }
                }
            }
            }  // mainFlick

        // ── Privacy jobs - sticky bottom status bar. Sibling of mainFlick (which is
        // Layout.fillHeight), so it stays pinned to the bottom and RESERVES its own
        // space: the scrolling body above shrinks to fit, so content is never covered.
        // Collapses to 0 height when there are no jobs.
        Rectangle {
            id: jobsBar
            visible: root.walletState === "ready" && jobsModel.count > 0
            Layout.fillWidth: true
            Layout.preferredHeight: visible ? jobsCol.implicitHeight + 16 : 0
            color: root.panelColor; border.color: root.borderColor; border.width: 1; radius: 12

            ColumnLayout {
                id: jobsCol
                anchors { left: parent.left; right: parent.right; top: parent.top; margins: 8 }
                spacing: 4

                Text { font.family: root.faceFont; text: "JOBS"; color: root.brandRed; font.pixelSize: 9; font.bold: true; font.letterSpacing: 1.2 }

                Repeater {
                    model: ListModel { id: jobsModel }
                    delegate: RowLayout {
                        required property string op
                        required property string asset
                        required property string state
                        required property string phase
                        required property string amount
                        required property string txId
                        required property string error
                        required property double elapsedMs
                        Layout.fillWidth: true
                        spacing: 6

                        Rectangle {
                            width: 6; height: 6; radius: 8
                            Layout.alignment: Qt.AlignVCenter
                            color: state === "done" ? root.successGreen
                                 : state === "error" ? root.errorRed
                                 : root.brandRed
                            SequentialAnimation on opacity {
                                running: state === "running"; loops: Animation.Infinite
                                NumberAnimation { to: 0.3; duration: 500 }
                                NumberAnimation { to: 1.0; duration: 500 }
                            }
                        }
                        Text { font.family: root.faceFont; text: root.opLabel(op); color: root.textPrimary; font.pixelSize: 10; font.bold: true }
                        Text { font.family: root.faceFont;
                            text: amount + " " + (asset === "token" ? "tok" : "LEZ")
                            color: root.textSecondary; font.pixelSize: 10
                        }
                        Item { Layout.fillWidth: true }
                        // Status: a clear phase label + a detail line (elapsed / txId / error).
                        // Never falls through to a bare "error". "done" == sequencer-accepted
                        // (landed on L2); L1 finalization is shown as still pending because the
                        // wallet does not track L1 here.
                        ColumnLayout {
                            spacing: 0
                            Layout.alignment: Qt.AlignVCenter
                            Layout.maximumWidth: 180
                            Text {
                                Layout.alignment: Qt.AlignRight
                                font.family: root.faceFont; font.pixelSize: 10; font.bold: true
                                text: state === "error" ? "failed"
                                    : state === "done"  ? "waiting L1 confirmation"
                                    : phase === "sent"  ? "sent to L2"
                                    : "processing"
                                color: state === "error" ? root.errorRed
                                     : state === "done"  ? root.accentOrange
                                     : root.textSecondary
                            }
                            Text {
                                Layout.alignment: Qt.AlignRight
                                Layout.maximumWidth: 180
                                font.family: root.faceFont; font.pixelSize: 9
                                elide: Text.ElideRight
                                visible: text.length > 0
                                text: state === "error" ? (error.length > 0 ? error : "")
                                    : state === "done"  ? (txId.length > 0 ? txId.substring(0, 12) + "…" : "")
                                    : (Math.round(elapsedMs / 1000) + "s")
                                color: root.textDisabled
                            }
                        }
                    }
                }
            }
        }

    } // ColumnLayout

    // ── Toast (notices / errors). Errors persist with Copy + Dismiss. ──────────
    // z is ABOVE the modal sheets (300/320) on purpose: this is where every refusal reason the
    // core can emit is surfaced, and a sheet that stays open across a refusal (a dApp action or
    // zone request the core declined) used to paint over the only explanation the user got.
    // "Handled" and "seen" are not the same thing, and a routed reason nobody can read is a
    // silent failure with extra steps.
    Rectangle {
        id: toastCard
        z: 340
        property bool copied: false
        visible: root.notice.length > 0
        anchors { bottom: parent.bottom; bottomMargin: 16; horizontalCenter: parent.horizontalCenter }
        width: Math.min(root.width - 32, 440); height: toastCol.implicitHeight + 16
        radius: 12
        color: root.noticeError ? Qt.rgba(251/255,113/255,133/255,0.16) : Qt.rgba(62/255,142/255,88/255,0.16)
        border.color: root.noticeError ? root.errorRed : root.successGreen; border.width: 1
        onVisibleChanged: if (!visible) copied = false
        ColumnLayout {
            id: toastCol
            anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter; leftMargin: 12; rightMargin: 12 }
            spacing: 7
            Text {
                id: noticeText
                Layout.fillWidth: true
                font.family: root.faceFont; font.pixelSize: 11; wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                horizontalAlignment: root.noticeError ? Text.AlignLeft : Text.AlignHCenter
                color: root.noticeError ? root.errorRed : root.silver
                text: root.notice; maximumLineCount: 8; elide: Text.ElideRight
            }
            // errors get Copy + Dismiss; confirmations just auto-fade
            RowLayout {
                visible: root.noticeError; Layout.fillWidth: true; spacing: 8
                Item { Layout.fillWidth: true }
                Rectangle { Layout.preferredWidth: 70; height: 26; radius: 8
                    color: toastCard.copied ? Qt.rgba(62/255,142/255,88/255,0.18) : root.selectBg
                    border.color: toastCard.copied ? root.successGreen : root.errorRed
                    Text { anchors.centerIn: parent; text: toastCard.copied ? "Copied ✓" : "Copy"
                        color: toastCard.copied ? root.successGreen : root.errorRed; font.pixelSize: 10; font.family: root.faceFont }
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            clipHelper.text = root.notice; clipHelper.selectAll(); clipHelper.copy()
                            toastCard.copied = true; copiedResetTimer.restart()
                        } } }
                Rectangle { Layout.preferredWidth: 70; height: 26; radius: 8; color: "transparent"; border.color: root.borderColor
                    Text { anchors.centerIn: parent; text: "Dismiss"; color: root.textSecondary; font.pixelSize: 10; font.family: root.faceFont }
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.notice = "" } }
            }
        }
        // non-error notices: click anywhere to dismiss
        MouseArea { anchors.fill: parent; enabled: !root.noticeError; onClicked: root.notice = "" }
        Timer { id: copiedResetTimer; interval: 1600; onTriggered: toastCard.copied = false }
    }

    // ── Disclaimer: the selected zone needs a local runtime that isn't present ──
    // devnet spawns a local sequencer; Tor/onion zones need a Tor binary. If the required
    // one is missing the zone can never come up (endless "Connecting…"), so say so plainly.
    // The two cases are mutually exclusive per zone, so one banner drives both.
    Rectangle {
        id: prereqBanner
        z: 90
        visible: (root.seqBinaryMissing || root.torBinaryMissing) && root.walletState === "ready"
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        height: bannerRow.implicitHeight + 22
        color: "#241A0A"
        Rectangle { anchors.top: parent.top; width: parent.width; height: 1; color: "#6E551F" }
        RowLayout {
            id: bannerRow
            anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter; leftMargin: 16; rightMargin: 16 }
            spacing: 12
            Text { text: "⚠"; color: "#E8A317"; font.pixelSize: 18; Layout.alignment: Qt.AlignTop; Layout.topMargin: 2 }
            ColumnLayout {
                Layout.fillWidth: true; spacing: 2
                Text { color: root.textPrimary; font.family: root.faceFont; font.pixelSize: 13; font.bold: true
                    text: root.torBinaryMissing ? "This network routes over Tor, but no Tor was found"
                                                : "This network needs a running local sequencer" }
                Text {
                    Layout.fillWidth: true; wrapMode: Text.WordWrap
                    color: root.textSecondary; font.family: root.faceFont; font.pixelSize: 11
                    text: root.torBinaryMissing
                        ? "This onion zone tunnels over Tor, but neither the bundled medusa-tor nor a system tor "
                          + "was found - install Tor (e.g. apt install tor), or pick a clearnet network below."
                        : "The devnet zone runs a sequencer on your machine, but none was found - install the "
                          + "sequencer_service binary in ~/.local/bin, or pick a hosted network below."
                }
            }
            Rectangle {
                Layout.preferredWidth: 128; Layout.preferredHeight: 30; radius: 8
                color: chooseNetMa.containsMouse ? "#3A2C10" : "transparent"; border.color: root.accentOrange; border.width: 1
                Text { anchors.centerIn: parent; text: "Choose network"; color: root.accentOrange; font.family: root.faceFont; font.pixelSize: 11 }
                MouseArea { id: chooseNetMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                    onClicked: root.screen = "network" }
            }
        }
    }

    // ── Modal loading overlay ──────────────────────────────────────────────────
    // Covers short blocking ops (send / sync / unlock / create). The long privacy
    // PROVE intentionally stays non-modal (the Privacy-jobs panel shows its progress).
    Rectangle {
        anchors.fill: parent
        z: 100
        visible: root.sendBusy || root.syncBusy || root.privBusy || root.secBusy.length > 0
        color: Qt.rgba(0, 0, 0, 0.62)
        MouseArea { anchors.fill: parent; hoverEnabled: true; preventStealing: true
            onClicked: {} onPressed: {} onWheel: {} }   // swallow all input while busy
        Rectangle {
            anchors.centerIn: parent
            width: 230; height: 100; radius: 8
            color: root.panelColor; border.color: root.borderColor; border.width: 1
            ColumnLayout {
                anchors.centerIn: parent; spacing: 12
                Rectangle {
                    Layout.alignment: Qt.AlignHCenter; width: 12; height: 12; radius: 12; color: root.accentOrange
                    SequentialAnimation on opacity {
                        running: true; loops: Animation.Infinite
                        NumberAnimation { to: 0.2; duration: 500 }
                        NumberAnimation { to: 1.0; duration: 500 }
                    }
                }
                Text {
                    Layout.alignment: Qt.AlignHCenter; font.family: root.faceFont
                    color: root.textPrimary; font.pixelSize: 12; font.letterSpacing: 1
                    text: root.secBusy.length > 0 ? root.secBusy + "…"
                        : root.sendBusy ? "Sending…"
                        : root.syncBusy ? "Syncing private state…"
                        : root.privBusy ? "Submitting…" : "Working…"
                }
            }
        }
    }

    // ── In-app update bar (floating, self-hides; only appears if a newer version
    //    is actually in the repos and the async bridge is available) ─────────────
    Rectangle {
        id: updateBar
        visible: root.updAvailable || root.updState.length > 0
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        height: 46; z: 260
        color: root.surface3
        Rectangle { anchors.top: parent.top; width: parent.width; height: 1
            color: root.updState === "error" ? root.errorRed
                 : root.updState === "done"  ? root.successGreen : root.accentOrange }
        RowLayout {
            anchors.fill: parent; anchors.leftMargin: 14; anchors.rightMargin: 10; spacing: 10
            Rectangle {
                Layout.alignment: Qt.AlignVCenter; width: 8; height: 8; radius: 4; color: root.accentOrange
                visible: root.updState === "downloading" || root.updState === "installing"
                SequentialAnimation on opacity { running: parent.visible; loops: Animation.Infinite
                    NumberAnimation { to: 0.25; duration: 500 }
                    NumberAnimation { to: 1.0; duration: 500 } }
            }
            Text {
                Layout.fillWidth: true; font.family: root.faceFont; font.pixelSize: 12
                color: root.updState === "error" ? root.errorRed : root.textPrimary; elide: Text.ElideRight
                text: root.updState.length > 0 ? root.updMsg : ("Update available - Medusa v" + root.updVersion)
            }
            Rectangle {
                visible: root.updAvailable && root.updState === ""
                implicitWidth: 80; height: 28; radius: 6
                color: updMa.containsMouse ? root.accentHover : root.accentOrange
                Text { anchors.centerIn: parent; text: "Update"; color: root.bgColor
                    font.family: root.faceFont; font.pixelSize: 11; font.bold: true }
                MouseArea { id: updMa; anchors.fill: parent; hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor; onClicked: root.doUpdate() }
            }
            Rectangle {
                visible: root.updState === "" || root.updState === "done" || root.updState === "error"
                width: 24; height: 24; radius: 12; color: "transparent"
                Text { anchors.centerIn: parent; text: "✕"; color: root.textSecondary
                    font.pixelSize: 12; font.family: root.faceFont }
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                    onClicked: { root.updAvailable = false; root.updState = ""; root.updMsg = "" } }
            }
        }
    }

    // ── Medusa-Connect: Connect approval sheet ─────────────────────────────────
    // Surfaces the FIFO-first pending CONNECT request. The account picker defaults to
    // none-selected (privacy); the user opts in to which accounts the dApp may see.
    Rectangle {
        id: connectSheet
        z: 300
        anchors.fill: parent
        property var req: (root.pendingConn.length > 0 && root.pendingConn[0].kind === "connect")
                          ? root.pendingConn[0] : null
        // Only over the UNLOCKED wallet. When locked/not-yet-set-up the onboarding screen shows
        // first (prompting unlock); after unlock (walletState "ready") this sheet appears with the
        // account picker populated - instead of an empty, unusable picker drawn over the lock screen.
        visible: req !== null && root.walletState === "ready"
        color: "transparent"   // tint is a child below, so the backdrop blur reads through
        // Glassmorphism: blur the screen content behind the sheet (appBody is a sibling - no recursion).
        MultiEffect { anchors.fill: parent; source: appBody; blurEnabled: true; blur: 0.85; autoPaddingEnabled: false }
        // Dark scrim tint ABOVE the blur - lightened so the frosted backdrop stays visible.
        Rectangle { anchors.fill: parent; color: Qt.rgba(0, 0, 0, 0.40) }
        MouseArea { anchors.fill: parent; hoverEnabled: true; preventStealing: true
            onClicked: {} onPressed: {} onWheel: {} }   // block input behind the modal

        Rectangle {
            anchors.centerIn: parent
            width: Math.min(root.width - 40, 380)
            height: Math.min(root.height - 40, connectCol.implicitHeight + 32)
            radius: root.rSheet
            color: root.surface2; border.color: root.borderStrong; border.width: 1
            // Deeper elevation for the floating modal (autoPadding stops the shadow clipping).
            layer.enabled: true
            layer.effect: MultiEffect {
                shadowEnabled: true; autoPaddingEnabled: true
                shadowColor: "#000000"; shadowVerticalOffset: 8; shadowBlur: 0.6; shadowOpacity: 0.35
            }
            // sheet entrance - scale + fade (premium modal choreography)
            opacity: connectSheet.visible ? 1 : 0
            scale: connectSheet.visible ? 1 : 0.92
            Behavior on opacity { NumberAnimation { duration: root.motionStandard; easing.type: Easing.OutCubic } }
            Behavior on scale   { NumberAnimation { duration: root.motionStandard; easing.type: Easing.OutCubic } }
            // hairline silver inner rim
            Rectangle { anchors.fill: parent; radius: parent.radius; color: "transparent"
                border.color: root.accentTint10; border.width: 1 }

            ColumnLayout {
                id: connectCol
                anchors { left: parent.left; right: parent.right; top: parent.top
                          margins: 16 }
                spacing: 12

                RowLayout {
                    Layout.fillWidth: true; spacing: 10
                    Rectangle {
                        Layout.preferredWidth: 38; Layout.preferredHeight: 38; radius: 10
                        color: root.surface2; border.color: root.borderColor
                        clip: true
                        Image {
                            anchors.fill: parent; anchors.margins: 2
                            visible: connectSheet.req && (connectSheet.req.app
                                     ? (connectSheet.req.app.icon || "") : "") !== ""
                            source: connectSheet.req && connectSheet.req.app
                                    ? (connectSheet.req.app.icon || "") : ""
                            fillMode: Image.PreserveAspectFit
                        }
                        Text {
                            anchors.centerIn: parent
                            visible: !(connectSheet.req && connectSheet.req.app
                                       && (connectSheet.req.app.icon || "") !== "")
                            text: "🔗"; font.pixelSize: 18
                        }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true; spacing: 1
                        // dApp-CONTROLLED STRING, rendered in the wallet's own title style, so
                        // it is hard-clamped to ONE line. connectRequest() only trims appName -
                        // it has no length or newline limit - and Text honours embedded "\n"
                        // even with wrapMode NoWrap, so without maximumLineCount an appName of
                        // "Tip Jar\nNetwork: Paradox Computer · clearnet\nhttps://…" would paint
                        // extra lines in 15px bold textPrimary right above the network card and
                        // read as the wallet's own statement of the network.
                        Text {
                            Layout.fillWidth: true
                            text: connectSheet.req && connectSheet.req.app
                                  ? (connectSheet.req.app.appName || "An app") : "An app"
                            color: root.textPrimary; font.family: root.faceFont
                            font.pixelSize: 15; font.bold: true
                            maximumLineCount: 1; elide: Text.ElideRight
                        }
                        Text {
                            Layout.fillWidth: true
                            text: "wants to connect to your wallet"
                            color: root.textSecondary; font.family: root.faceFont; font.pixelSize: 11
                        }
                    }
                }

                // ── Operating network (see the disclosure block above refreshSeqStatus) ──
                // WALLET-OWNED FACTS ONLY: name, address, health and alert verdict all come
                // back from medusa_core; connectSheet.req contributes nothing. Placed directly
                // under the app header rather than next to the CTA because the account picker
                // below is unbounded in length - this must not be pushed out of sight by a
                // wallet with many accounts. It also states the zone the session is minted
                // against (approveConnect stamps the session with the ACTIVE zone).
                Rectangle {
                    Layout.fillWidth: true; radius: 10
                    color: root.netAlert === "" ? root.inputBg : root.errorTint
                    border.color: root.netAlertColor; border.width: 1
                    implicitHeight: connNetCol.implicitHeight + 20
                    ColumnLayout {
                        id: connNetCol
                        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 10 }
                        spacing: 4
                        RowLayout {
                            Layout.fillWidth: true; spacing: 6
                            Rectangle {
                                Layout.preferredWidth: 8; Layout.preferredHeight: 8; radius: 4
                                Layout.alignment: Qt.AlignVCenter
                                color: root.seqStatus === "running"  ? root.greenBright
                                     : root.seqStatus === "starting" ? root.connectGray : root.errorRed
                            }
                            Text {
                                text: "CONNECTING YOU ON"
                                color: root.textSecondary; font.family: root.faceFont
                                font.pixelSize: 10; font.letterSpacing: 1
                            }
                            Item { Layout.fillWidth: true }
                            Text {
                                visible: root.seqStatus === "starting"
                                text: "checking…"; color: root.textDisabled
                                font.family: root.faceFont; font.pixelSize: 9
                            }
                        }
                        // Name line, clamped + "custom"-tagged (see the action sheet's copy).
                        RowLayout {
                            Layout.fillWidth: true; spacing: 6
                            Text {
                                Layout.fillWidth: true
                                text: root.zoneName(root.network)
                                color: root.textPrimary; font.family: root.faceFont
                                font.pixelSize: 12; font.bold: true
                                maximumLineCount: 1; elide: Text.ElideRight
                            }
                            Text {
                                visible: root.activeZoneIsCustom
                                text: "custom"; color: root.textDisabled
                                font.family: root.faceFont; font.pixelSize: 9
                            }
                        }
                        // Load-bearing: WRAPPED, never elided (see the action sheet's copy).
                        Text {
                            Layout.fillWidth: true; visible: text.length > 0
                            text: root.netShownAddr
                            color: root.textSecondary; font.family: root.monoFont
                            font.pixelSize: 10; wrapMode: Text.WrapAnywhere
                        }
                        RowLayout {
                            visible: root.netAlert !== ""
                            Layout.fillWidth: true; Layout.topMargin: 2; spacing: 6
                            Text { text: "⚠"; color: root.netAlertColor; font.pixelSize: 12
                                   Layout.alignment: Qt.AlignTop }
                            ColumnLayout {
                                Layout.fillWidth: true; spacing: 1
                                Text {
                                    Layout.fillWidth: true; text: root.netAlertTitle()
                                    color: root.netAlertColor; font.family: root.faceFont
                                    font.pixelSize: 10; font.bold: true
                                    wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                                }
                                Text {
                                    Layout.fillWidth: true; text: root.netAlertBody()
                                    color: root.textSecondary; font.family: root.faceFont
                                    font.pixelSize: 9; wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                                }
                                ColumnLayout {
                                    visible: root.netAlert === "repoint"
                                    Layout.fillWidth: true; Layout.topMargin: 3; spacing: 1
                                    Text { text: "configured for this zone"; color: root.textDisabled
                                           font.family: root.faceFont; font.pixelSize: 9 }
                                    Text { Layout.fillWidth: true; text: root.netExpectedDial
                                           color: root.textSecondary; font.family: root.monoFont
                                           font.pixelSize: 9; wrapMode: Text.WrapAnywhere }
                                    Text { text: "actually dialling"; color: root.textDisabled
                                           font.family: root.faceFont; font.pixelSize: 9
                                           Layout.topMargin: 2 }
                                    Text { Layout.fillWidth: true; text: root.netActualDial
                                           color: root.errorRed; font.family: root.monoFont
                                           font.pixelSize: 9; wrapMode: Text.WrapAnywhere }
                                }
                            }
                        }
                    }
                }

                Text {
                    Layout.fillWidth: true
                    text: "Permissions requested"
                    color: root.textSecondary; font.family: root.faceFont
                    font.pixelSize: 10; font.letterSpacing: 1
                }
                Flow {
                    Layout.fillWidth: true; spacing: 6
                    Repeater {
                        model: connectSheet.req ? (connectSheet.req.perms || []) : []
                        Rectangle {
                            width: permLbl.implicitWidth + 16; height: 22; radius: 11
                            color: root.selectBg; border.color: root.borderColor
                            Text {
                                id: permLbl; anchors.centerIn: parent; text: modelData
                                color: root.silver; font.family: root.faceFont; font.pixelSize: 10
                            }
                        }
                    }
                }

                Text {
                    Layout.fillWidth: true
                    text: "Expose accounts (none selected = nothing shared)"
                    color: root.textSecondary; font.family: root.faceFont
                    font.pixelSize: 10; font.letterSpacing: 1
                }
                ColumnLayout {
                    Layout.fillWidth: true; spacing: 5
                    Repeater {
                        model: accountModel
                        RowLayout {
                            id: connAcctRow
                            Layout.fillWidth: true; spacing: 8
                            property bool picked: root.connAccountSel[model.id] === true
                            Rectangle {
                                Layout.preferredWidth: 18; Layout.preferredHeight: 18; radius: 5
                                color: connAcctRow.picked ? root.accentOrange : root.inputBg
                                border.color: connAcctRow.picked ? root.accentOrange : root.borderColor
                                Text { anchors.centerIn: parent; visible: connAcctRow.picked
                                       text: "✓"; color: root.bgColor; font.pixelSize: 11 }
                            }
                            Text {
                                Layout.fillWidth: true
                                text: root.displayId(model.id) + "  ·  " + (model.type || "public")
                                color: root.textPrimary; font.family: root.faceFont
                                font.pixelSize: 12; elide: Text.ElideRight
                            }
                            MouseArea {
                                anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    var m = root.connAccountSel
                                    var copy = {}
                                    for (var k in m) copy[k] = m[k]
                                    copy[model.id] = !(copy[model.id] === true)
                                    root.connAccountSel = copy
                                }
                            }
                        }
                    }
                    Text {
                        visible: accountModel.count === 0
                        text: "No accounts yet - create one first."
                        color: root.textDisabled; font.family: root.faceFont; font.pixelSize: 11
                    }
                }

                RowLayout {
                    Layout.fillWidth: true; Layout.topMargin: 4; spacing: 10
                    Rectangle {
                        Layout.fillWidth: true; height: 38; radius: 10
                        color: "transparent"; border.color: root.borderColor
                        Text { anchors.centerIn: parent; text: "Reject"
                               color: root.textSecondary; font.family: root.faceFont; font.pixelSize: 13 }
                        MouseArea {
                            anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                            onClicked: if (connectSheet.req) root.rejectConnectRequest(connectSheet.req.requestId)
                        }
                    }
                    Rectangle {
                        id: connectApproveBtn
                        Layout.fillWidth: true; height: 38; radius: 10
                        color: connectApproveMa.pressed ? root.brandRedPressed
                             : connectApproveMa.containsMouse ? root.brandRedHover : root.brandRed
                        Behavior on color { ColorAnimation { duration: root.motionQuick } }
                        Text { anchors.centerIn: parent; text: "Connect"
                               color: root.textPrimary; font.family: root.faceFont; font.pixelSize: 13; font.bold: true }
                        MouseArea {
                            id: connectApproveMa
                            anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                if (!connectSheet.req) return
                                var ids = []
                                for (var k in root.connAccountSel)
                                    if (root.connAccountSel[k] === true) ids.push(k)
                                root.approveConnectRequest(connectSheet.req.requestId, ids)
                            }
                        }
                    }
                }
            }
        }
    }

    // ── Medusa-Connect: "Authorized!" confirmation ────────────────────────────
    // After a successful connect approval, tell the user the handshake is done and to
    // return to the dApp (the wallet has no way to focus the dApp for them). Auto-dismisses.
    Rectangle {
        id: authorizedSheet
        z: 320
        anchors.fill: parent
        visible: root.connAuthorizedApp !== "" && root.walletState === "ready"
        color: "transparent"
        MultiEffect { anchors.fill: parent; source: appBody; blurEnabled: true; blur: 0.85; autoPaddingEnabled: false }
        Rectangle { anchors.fill: parent; color: Qt.rgba(0, 0, 0, 0.40) }
        MouseArea { anchors.fill: parent; hoverEnabled: true; preventStealing: true
            onClicked: root.connAuthorizedApp = "" }   // tap anywhere to dismiss

        // auto-dismiss after a few seconds
        Timer {
            running: authorizedSheet.visible; interval: 4200; repeat: false
            onTriggered: root.connAuthorizedApp = ""
        }

        Rectangle {
            anchors.centerIn: parent
            width: Math.min(root.width - 40, 360)
            height: authCol.implicitHeight + 36
            radius: root.rSheet
            color: root.surface2; border.color: root.borderStrong; border.width: 1
            layer.enabled: true
            layer.effect: MultiEffect {
                shadowEnabled: true; autoPaddingEnabled: true
                shadowColor: "#000000"; shadowVerticalOffset: 8; shadowBlur: 0.6; shadowOpacity: 0.35
            }
            opacity: authorizedSheet.visible ? 1 : 0
            scale: authorizedSheet.visible ? 1 : 0.92
            Behavior on opacity { NumberAnimation { duration: root.motionStandard; easing.type: Easing.OutCubic } }
            Behavior on scale   { NumberAnimation { duration: root.motionStandard; easing.type: Easing.OutCubic } }
            Rectangle { anchors.fill: parent; radius: parent.radius; color: "transparent"
                border.color: root.accentTint10; border.width: 1 }

            ColumnLayout {
                id: authCol
                anchors { left: parent.left; right: parent.right; top: parent.top; margins: 18 }
                spacing: 12

                // success check
                Rectangle {
                    Layout.alignment: Qt.AlignHCenter
                    width: 52; height: 52; radius: 26
                    color: Qt.rgba(62/255, 158/255, 91/255, 0.16)
                    border.color: root.successGreen; border.width: 1.5
                    Text { anchors.centerIn: parent; text: "✓"; color: root.greenBright; font.pixelSize: 26; font.bold: true }
                }
                Text {
                    Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter
                    text: "Authorized!"
                    color: root.textPrimary; font.family: root.faceFont; font.pixelSize: 17; font.bold: true
                }
                Text {
                    Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    text: "Now go back to " + root.connAuthorizedApp + "."
                    color: root.textSecondary; font.family: root.faceFont; font.pixelSize: 12
                }
                Rectangle {
                    Layout.fillWidth: true; Layout.topMargin: 4; height: 38; radius: 10
                    color: authDoneMa.pressed ? root.brandRedPressed
                         : authDoneMa.containsMouse ? root.brandRedHover : root.brandRed
                    Behavior on color { ColorAnimation { duration: root.motionQuick } }
                    Text { anchors.centerIn: parent; text: "Done"
                           color: root.textPrimary; font.family: root.faceFont; font.pixelSize: 13; font.bold: true }
                    MouseArea {
                        id: authDoneMa
                        anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                        onClicked: root.connAuthorizedApp = ""
                    }
                }
            }
        }
    }

    // ── Medusa-Connect: Action approval sheet ──────────────────────────────────
    // Surfaces the FIFO-first pending ACTION request (a send/shield/deshield/private a
    // connected dApp asked to run). Approve dispatches to the existing job machinery.
    Rectangle {
        id: actionSheet
        z: 300
        anchors.fill: parent
        property var req: (root.pendingConn.length > 0 && root.pendingConn[0].kind === "action")
                          ? root.pendingConn[0] : null
        // Same gate as the connect sheet - unlock first (onboarding), then approve the transfer.
        visible: req !== null && root.walletState === "ready"
        color: "transparent"   // tint is a child below, so the backdrop blur reads through
        // Glassmorphism: blur the screen content behind the sheet (appBody is a sibling - no recursion).
        MultiEffect { anchors.fill: parent; source: appBody; blurEnabled: true; blur: 0.85; autoPaddingEnabled: false }
        // Dark scrim tint ABOVE the blur - lightened so the frosted backdrop stays visible.
        Rectangle { anchors.fill: parent; color: Qt.rgba(0, 0, 0, 0.40) }
        MouseArea { anchors.fill: parent; hoverEnabled: true; preventStealing: true
            onClicked: {} onPressed: {} onWheel: {} }

        Rectangle {
            anchors.centerIn: parent
            width: Math.min(root.width - 40, 380)
            // Clamped like connectSheet/zoneOfflineSheet: the network card can grow this sheet
            // (a wrapped .onion is two lines), and a sheet taller than the window has no CTA.
            height: Math.min(root.height - 40, actionCol.implicitHeight + 32)
            radius: root.rSheet
            color: root.surface2; border.color: root.borderStrong; border.width: 1
            // Deeper elevation for the floating modal (autoPadding stops the shadow clipping).
            layer.enabled: true
            layer.effect: MultiEffect {
                shadowEnabled: true; autoPaddingEnabled: true
                shadowColor: "#000000"; shadowVerticalOffset: 8; shadowBlur: 0.6; shadowOpacity: 0.35
            }
            // sheet entrance - scale + fade
            opacity: actionSheet.visible ? 1 : 0
            scale: actionSheet.visible ? 1 : 0.92
            Behavior on opacity { NumberAnimation { duration: root.motionStandard; easing.type: Easing.OutCubic } }
            Behavior on scale   { NumberAnimation { duration: root.motionStandard; easing.type: Easing.OutCubic } }
            Rectangle { anchors.fill: parent; radius: parent.radius; color: "transparent"
                border.color: root.accentTint10; border.width: 1 }

            ColumnLayout {
                id: actionCol
                anchors { left: parent.left; right: parent.right; top: parent.top; margins: 16 }
                spacing: 12

                Text {
                    Layout.fillWidth: true
                    text: "Approve " + (actionSheet.req ? (actionSheet.req.op || "send") : "send")
                    color: root.textPrimary; font.family: root.faceFont
                    font.pixelSize: 15; font.bold: true
                }
                Text {
                    Layout.fillWidth: true
                    text: "A connected app requested this transfer"
                    color: root.textSecondary; font.family: root.faceFont; font.pixelSize: 11
                }

                Rectangle {
                    Layout.fillWidth: true; radius: 10
                    color: root.inputBg; border.color: root.borderColor
                    implicitHeight: detailCol.implicitHeight + 20
                    ColumnLayout {
                        id: detailCol
                        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 10 }
                        spacing: 6
                        RowLayout {
                            Layout.fillWidth: true
                            Text { text: "From"; color: root.textSecondary; font.family: root.faceFont; font.pixelSize: 11 }
                            Item { Layout.fillWidth: true }
                            Text { text: actionSheet.req ? root.displayId(actionSheet.req.from) : ""
                                   color: root.textPrimary; font.family: root.faceFont; font.pixelSize: 11 }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Text { text: "To"; color: root.textSecondary; font.family: root.faceFont; font.pixelSize: 11 }
                            Item { Layout.fillWidth: true }
                            Text {
                                text: actionSheet.req
                                      ? (actionSheet.req.to && actionSheet.req.to.length > 0
                                         ? root.displayId(actionSheet.req.to) : "(foreign recipient)")
                                      : ""
                                color: root.textPrimary; font.family: root.faceFont; font.pixelSize: 11
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Text { text: "Amount"; color: root.textSecondary; font.family: root.faceFont; font.pixelSize: 11 }
                            Item { Layout.fillWidth: true }
                            Text { text: actionSheet.req
                                        ? (actionSheet.req.amount + " " +
                                           ((actionSheet.req.asset === "token") ? "token" : "LEZ"))
                                        : ""
                                   color: root.textPrimary; font.family: root.faceFont; font.pixelSize: 11; font.bold: true }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Text { text: "Mode"; color: root.textSecondary; font.family: root.faceFont; font.pixelSize: 11 }
                            Item { Layout.fillWidth: true }
                            Text { text: actionSheet.req ? (actionSheet.req.op || "send") : ""
                                   color: root.silver; font.family: root.faceFont; font.pixelSize: 11 }
                        }
                    }
                }

                // ── Operating network (see the disclosure block above refreshSeqStatus) ──
                // WALLET-OWNED FACTS ONLY: name, address, health and the alert verdict all
                // come back from medusa_core. actionSheet.req contributes NOTHING here, so a
                // dApp cannot influence a single character of this card. It sits directly
                // above the CTA because it is the last thing that should be read before
                // approving: what -> where -> approve.
                Rectangle {
                    Layout.fillWidth: true; radius: 10
                    color: root.netAlert === "" ? root.inputBg : root.errorTint
                    border.color: root.netAlertColor; border.width: 1
                    implicitHeight: actNetCol.implicitHeight + 20
                    ColumnLayout {
                        id: actNetCol
                        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 10 }
                        spacing: 4
                        RowLayout {
                            Layout.fillWidth: true; spacing: 6
                            Rectangle {
                                Layout.preferredWidth: 8; Layout.preferredHeight: 8; radius: 4
                                Layout.alignment: Qt.AlignVCenter
                                color: root.seqStatus === "running"  ? root.greenBright
                                     : root.seqStatus === "starting" ? root.connectGray : root.errorRed
                            }
                            Text {
                                text: "NETWORK THIS RUNS ON"
                                color: root.textSecondary; font.family: root.faceFont
                                font.pixelSize: 10; font.letterSpacing: 1
                            }
                            Item { Layout.fillWidth: true }
                            Text {
                                visible: root.seqStatus === "starting"
                                text: "checking…"; color: root.textDisabled
                                font.family: root.faceFont; font.pixelSize: 9
                            }
                        }
                        // Name line. Clamped to ONE line: a custom zone's name is free text
                        // (see activeZoneIsCustom) and Text renders embedded newlines even
                        // with NoWrap, so an unclamped name could paint extra wallet-styled
                        // lines. The "custom" tag says the name is not one of the wallet's.
                        RowLayout {
                            Layout.fillWidth: true; spacing: 6
                            Text {
                                Layout.fillWidth: true
                                text: root.zoneName(root.network)
                                color: root.textPrimary; font.family: root.faceFont
                                font.pixelSize: 12; font.bold: true
                                maximumLineCount: 1; elide: Text.ElideRight
                            }
                            Text {
                                visible: root.activeZoneIsCustom
                                text: "custom"; color: root.textDisabled
                                font.family: root.faceFont; font.pixelSize: 9
                            }
                        }
                        // The load-bearing line. WRAPPED, never elided: a repoint keeps the
                        // name above and changes only this, so a truncated address would
                        // defeat the entire purpose. WrapAnywhere so a 62-char .onion reads
                        // in full in a 380px sheet.
                        Text {
                            Layout.fillWidth: true; visible: text.length > 0
                            text: root.netShownAddr
                            color: root.textSecondary; font.family: root.monoFont
                            font.pixelSize: 10; wrapMode: Text.WrapAnywhere
                        }
                        RowLayout {
                            visible: root.netAlert !== ""
                            Layout.fillWidth: true; Layout.topMargin: 2; spacing: 6
                            Text { text: "⚠"; color: root.netAlertColor; font.pixelSize: 12
                                   Layout.alignment: Qt.AlignTop }
                            ColumnLayout {
                                Layout.fillWidth: true; spacing: 1
                                Text {
                                    Layout.fillWidth: true; text: root.netAlertTitle()
                                    color: root.netAlertColor; font.family: root.faceFont
                                    font.pixelSize: 10; font.bold: true
                                    wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                                }
                                Text {
                                    Layout.fillWidth: true; text: root.netAlertBody()
                                    color: root.textSecondary; font.family: root.faceFont
                                    font.pixelSize: 9; wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                                }
                                // Repoint only: both addresses together, because the tell is
                                // precisely that they differ.
                                ColumnLayout {
                                    visible: root.netAlert === "repoint"
                                    Layout.fillWidth: true; Layout.topMargin: 3; spacing: 1
                                    Text { text: "configured for this zone"; color: root.textDisabled
                                           font.family: root.faceFont; font.pixelSize: 9 }
                                    Text { Layout.fillWidth: true; text: root.netExpectedDial
                                           color: root.textSecondary; font.family: root.monoFont
                                           font.pixelSize: 9; wrapMode: Text.WrapAnywhere }
                                    Text { text: "actually dialling"; color: root.textDisabled
                                           font.family: root.faceFont; font.pixelSize: 9
                                           Layout.topMargin: 2 }
                                    Text { Layout.fillWidth: true; text: root.netActualDial
                                           color: root.errorRed; font.family: root.monoFont
                                           font.pixelSize: 9; wrapMode: Text.WrapAnywhere }
                                }
                            }
                        }
                    }
                }

                Text {
                    Layout.fillWidth: true
                    text: root.connActionHint(actionSheet.req)
                    color: root.textSecondary; font.family: root.faceFont
                    font.pixelSize: 10; wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                }

                // approveAction is a gated verb: on a plaintext store it is refused, so approving
                // could only ever fail and leave the dApp waiting. Reject stays live (it is
                // ungated and it is the honest answer here), and the reason is on the sheet.
                Text {
                    visible: root.signingBlocked
                    Layout.fillWidth: true
                    text: "This wallet's storage has no password on it, so it cannot prove who is "
                        + "approving and the core will refuse this. Reject it, set a password in "
                        + "Security & Backup, unlock, and ask the app again."
                    color: root.warningAmber; font.family: root.faceFont
                    font.pixelSize: 10; wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                }

                RowLayout {
                    Layout.fillWidth: true; Layout.topMargin: 2; spacing: 10
                    Rectangle {
                        Layout.fillWidth: true; height: 38; radius: 10
                        color: "transparent"; border.color: root.borderColor
                        Text { anchors.centerIn: parent; text: "Reject"
                               color: root.textSecondary; font.family: root.faceFont; font.pixelSize: 13 }
                        MouseArea {
                            anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                            onClicked: if (actionSheet.req) root.rejectActionRequest(actionSheet.req.requestId)
                        }
                    }
                    Rectangle {
                        id: actionApproveBtn
                        Layout.fillWidth: true; height: 38; radius: 10
                        enabled: !root.signingBlocked
                        opacity: enabled ? 1.0 : 0.4
                        color: !enabled ? "transparent"
                             : actionApproveMa.pressed ? root.brandRedPressed
                             : actionApproveMa.containsMouse ? root.brandRedHover : root.brandRed
                        border.color: enabled ? root.brandRed : root.borderColor
                        Behavior on color { ColorAnimation { duration: root.motionQuick } }
                        Text { anchors.centerIn: parent; text: "Approve"
                               color: actionApproveBtn.enabled ? root.textPrimary : root.textDisabled
                               font.family: root.faceFont; font.pixelSize: 13; font.bold: true }
                        MouseArea {
                            id: actionApproveMa
                            anchors.fill: parent; hoverEnabled: true
                            cursorShape: actionApproveBtn.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                            onClicked: if (actionSheet.req) root.approveActionRequest(actionSheet.req)
                        }
                    }
                }
            }
        }
    }

    // ── Medusa-Connect: Switch-sequencer (zone) approval sheet ─────────────────
    // Surfaces the FIFO-first pending ZONE request (a connected dApp asking the wallet to
    // switch its sequencer/zone). Approve/Reject dispatch to approveZone/rejectZone and
    // advance to the next pending request, exactly like the connect/action sheets.
    Rectangle {
        id: zoneSheet
        z: 300
        anchors.fill: parent
        property var req: (root.pendingConn.length > 0 && root.pendingConn[0].kind === "zone")
                          ? root.pendingConn[0] : null
        // Same gate as the connect/action sheets - unlock first (onboarding), then approve.
        visible: req !== null && root.walletState === "ready"
        color: "transparent"   // tint is a child below, so the backdrop blur reads through
        // Glassmorphism: blur the screen content behind the sheet (appBody is a sibling - no recursion).
        MultiEffect { anchors.fill: parent; source: appBody; blurEnabled: true; blur: 0.85; autoPaddingEnabled: false }
        // Dark scrim tint ABOVE the blur - lightened so the frosted backdrop stays visible.
        Rectangle { anchors.fill: parent; color: Qt.rgba(0, 0, 0, 0.40) }
        MouseArea { anchors.fill: parent; hoverEnabled: true; preventStealing: true
            onClicked: {} onPressed: {} onWheel: {} }   // block input behind the modal

        Rectangle {
            anchors.centerIn: parent
            width: Math.min(root.width - 40, 380)
            // Clamped like connectSheet/zoneOfflineSheet: this sheet now carries two address
            // cards, and a sheet taller than the window would put its CTAs off-screen.
            height: Math.min(root.height - 40, zoneCol.implicitHeight + 32)
            radius: root.rSheet
            color: root.surface2; border.color: root.borderStrong; border.width: 1
            // Deeper elevation for the floating modal (autoPadding stops the shadow clipping).
            layer.enabled: true
            layer.effect: MultiEffect {
                shadowEnabled: true; autoPaddingEnabled: true
                shadowColor: "#000000"; shadowVerticalOffset: 8; shadowBlur: 0.6; shadowOpacity: 0.35
            }
            // sheet entrance - scale + fade
            opacity: zoneSheet.visible ? 1 : 0
            scale: zoneSheet.visible ? 1 : 0.92
            Behavior on opacity { NumberAnimation { duration: root.motionStandard; easing.type: Easing.OutCubic } }
            Behavior on scale   { NumberAnimation { duration: root.motionStandard; easing.type: Easing.OutCubic } }
            // hairline silver inner rim
            Rectangle { anchors.fill: parent; radius: parent.radius; color: "transparent"
                border.color: root.accentTint10; border.width: 1 }

            ColumnLayout {
                id: zoneCol
                anchors { left: parent.left; right: parent.right; top: parent.top; margins: 16 }
                spacing: 12

                RowLayout {
                    Layout.fillWidth: true; spacing: 10
                    Rectangle {
                        Layout.preferredWidth: 38; Layout.preferredHeight: 38; radius: 10
                        color: root.surface2; border.color: root.borderColor
                        Text { anchors.centerIn: parent; text: "⇄"; font.pixelSize: 18; color: root.silver }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true; spacing: 1
                        // THE WALLET'S OWN SENTENCE - a constant, never interpolated with dApp
                        // text. It used to read `appName + " wants to switch your wallet's
                        // sequencer"` in one wrapping 15px-bold run, which handed a dApp the
                        // wallet's title voice: an appName carrying newlines could paint whole
                        // extra lines that read as the wallet talking.
                        Text {
                            Layout.fillWidth: true
                            text: "An app wants to change your network"
                            color: root.textPrimary; font.family: root.faceFont
                            font.pixelSize: 15; font.bold: true
                            maximumLineCount: 1; elide: Text.ElideRight
                        }
                        // The dApp's name for itself: attributed, subordinate colour, one line.
                        Text {
                            Layout.fillWidth: true
                            text: "asked by " + (zoneSheet.req ? (zoneSheet.req.appName || "an app")
                                                               : "an app")
                            color: root.textSecondary; font.family: root.faceFont; font.pixelSize: 11
                            maximumLineCount: 1; elide: Text.ElideRight
                        }
                    }
                }

                // ── THE change, shown AS a change: what you are on now, then what is asked ──
                // Requirement of this sheet: a dApp is asking to repoint the wallet itself, so
                // the two addresses have to sit together or the diff is not legible.

                // CURRENT - wallet-owned facts only (medusa_core), identical treatment to the
                // connect/action sheets so the "where am I" line reads the same everywhere.
                Rectangle {
                    Layout.fillWidth: true; radius: 10
                    color: root.netAlert === "" ? root.inputBg : root.errorTint
                    border.color: root.netAlertColor; border.width: 1
                    implicitHeight: zoneNowCol.implicitHeight + 20
                    ColumnLayout {
                        id: zoneNowCol
                        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 10 }
                        spacing: 4
                        RowLayout {
                            Layout.fillWidth: true; spacing: 6
                            Rectangle {
                                Layout.preferredWidth: 8; Layout.preferredHeight: 8; radius: 4
                                Layout.alignment: Qt.AlignVCenter
                                color: root.seqStatus === "running"  ? root.greenBright
                                     : root.seqStatus === "starting" ? root.connectGray : root.errorRed
                            }
                            Text {
                                text: "YOU ARE ON NOW"
                                color: root.textSecondary; font.family: root.faceFont
                                font.pixelSize: 10; font.letterSpacing: 1
                            }
                            Item { Layout.fillWidth: true }
                            Text {
                                visible: root.seqStatus === "starting"
                                text: "checking…"; color: root.textDisabled
                                font.family: root.faceFont; font.pixelSize: 9
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true; spacing: 6
                            Text {
                                Layout.fillWidth: true
                                text: root.zoneName(root.network)
                                color: root.textPrimary; font.family: root.faceFont
                                font.pixelSize: 12; font.bold: true
                                maximumLineCount: 1; elide: Text.ElideRight
                            }
                            Text {
                                visible: root.activeZoneIsCustom
                                text: "custom"; color: root.textDisabled
                                font.family: root.faceFont; font.pixelSize: 9
                            }
                        }
                        Text {
                            Layout.fillWidth: true; visible: text.length > 0
                            text: root.netShownAddr
                            color: root.textSecondary; font.family: root.monoFont
                            font.pixelSize: 10; wrapMode: Text.WrapAnywhere
                        }
                        RowLayout {
                            visible: root.netAlert !== ""
                            Layout.fillWidth: true; Layout.topMargin: 2; spacing: 6
                            Text { text: "⚠"; color: root.netAlertColor; font.pixelSize: 12
                                   Layout.alignment: Qt.AlignTop }
                            ColumnLayout {
                                Layout.fillWidth: true; spacing: 1
                                Text {
                                    Layout.fillWidth: true; text: root.netAlertTitle()
                                    color: root.netAlertColor; font.family: root.faceFont
                                    font.pixelSize: 10; font.bold: true
                                    wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                                }
                                Text {
                                    Layout.fillWidth: true; text: root.netAlertBody()
                                    color: root.textSecondary; font.family: root.faceFont
                                    font.pixelSize: 9; wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                                }
                                ColumnLayout {
                                    visible: root.netAlert === "repoint"
                                    Layout.fillWidth: true; Layout.topMargin: 3; spacing: 1
                                    Text { text: "configured for this zone"; color: root.textDisabled
                                           font.family: root.faceFont; font.pixelSize: 9 }
                                    Text { Layout.fillWidth: true; text: root.netExpectedDial
                                           color: root.textSecondary; font.family: root.monoFont
                                           font.pixelSize: 9; wrapMode: Text.WrapAnywhere }
                                    Text { text: "actually dialling"; color: root.textDisabled
                                           font.family: root.faceFont; font.pixelSize: 9
                                           Layout.topMargin: 2 }
                                    Text { Layout.fillWidth: true; text: root.netActualDial
                                           color: root.errorRed; font.family: root.monoFont
                                           font.pixelSize: 9; wrapMode: Text.WrapAnywhere }
                                }
                            }
                        }
                    }
                }

                Text {
                    Layout.alignment: Qt.AlignHCenter
                    text: "↓"; color: root.silver; font.pixelSize: 14
                }

                // PROPOSED - every value in this card except the wallet's verdict line comes
                // from the dApp, so the card SAYS SO and nothing in it is styled as a wallet
                // statement: the address is mono (it is the proposal being judged) but the
                // app's own label is quoted, unbolded, textDisabled and explicitly attributed,
                // so it can never be read as the wallet's name for a network.
                Rectangle {
                    id: zoneWantCard
                    Layout.fillWidth: true; radius: 10
                    // The wallet's verdict about the proposal, computed from ITS zone list.
                    readonly property bool isCurrent: zoneSheet.req !== null
                        && root.zoneReqIsCurrent(zoneSheet.req.sequencer || "", zoneSheet.req.tor === true)
                    readonly property var known: zoneSheet.req
                        ? root.knownZoneFor(zoneSheet.req.sequencer || "", zoneSheet.req.tor === true)
                        : null
                    // Calm when the wallet already knows this address; amber only for a network
                    // it has never used - which on THIS sheet is the case worth stopping at.
                    readonly property bool isNew: !isCurrent && known === null
                    color: root.inputBg
                    border.color: isNew ? root.warningAmber : root.borderColor; border.width: 1
                    implicitHeight: zoneWantCol.implicitHeight + 20
                    ColumnLayout {
                        id: zoneWantCol
                        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 10 }
                        spacing: 4
                        RowLayout {
                            Layout.fillWidth: true; spacing: 6
                            Text {
                                text: "THE APP WANTS YOU ON"
                                color: root.textSecondary; font.family: root.faceFont
                                font.pixelSize: 10; font.letterSpacing: 1
                            }
                            Item { Layout.fillWidth: true }
                            Text {
                                text: "supplied by the app"
                                color: root.textDisabled; font.family: root.faceFont; font.pixelSize: 9
                            }
                        }
                        // The proposal itself: wrapped in full, never elided. This used to be
                        // ElideMiddle, which hides the middle of an .onion - the only part that
                        // distinguishes it from a lookalike.
                        Text {
                            Layout.fillWidth: true
                            text: zoneSheet.req ? (zoneSheet.req.sequencer || "") : ""
                            color: root.textPrimary; font.family: root.monoFont
                            font.pixelSize: 11; wrapMode: Text.WrapAnywhere
                        }
                        // The app's own label, clearly marked as the app's words.
                        Text {
                            Layout.fillWidth: true
                            visible: zoneSheet.req !== null && (zoneSheet.req.label || "") !== ""
                            text: "the app calls it \"" + (zoneSheet.req ? (zoneSheet.req.label || "") : "") + "\""
                            color: root.textDisabled; font.family: root.faceFont; font.pixelSize: 9
                            maximumLineCount: 1; elide: Text.ElideRight
                        }
                        RowLayout {
                            Layout.fillWidth: true; spacing: 6
                            Rectangle {
                                width: 7; height: 7; radius: 4; Layout.alignment: Qt.AlignVCenter
                                color: (zoneSheet.req && zoneSheet.req.tor) ? root.successGreen : root.warningAmber
                            }
                            Text {
                                text: (zoneSheet.req && zoneSheet.req.tor)
                                      ? "Routed over Tor" : "Clearnet (not over Tor)"
                                color: root.textSecondary; font.family: root.faceFont; font.pixelSize: 10
                            }
                        }
                        // THE WALLET'S VERDICT on the proposal - derived from getZones(), not
                        // from anything the dApp said about itself.
                        Text {
                            Layout.fillWidth: true; Layout.topMargin: 2
                            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                            font.family: root.faceFont; font.pixelSize: 9
                            color: zoneWantCard.isNew ? root.warningAmber : root.textSecondary
                            text: {
                                if (!zoneSheet.req) return ""
                                if (zoneWantCard.isCurrent)
                                    return "This is the network you are already on - approving changes nothing."
                                if (zoneWantCard.known !== null)
                                    return zoneWantCard.known.builtin === true
                                        ? "Your wallet knows this address as a built-in network: "
                                          + (zoneWantCard.known.name || "")
                                        : "Your wallet already has this address saved, under a name that "
                                          + "was not chosen by the wallet."
                                return "Your wallet has never used this address. Approving points every "
                                     + "balance you see and everything you send at it."
                            }
                        }
                    }
                }

                // approveZone is gated for the same reason approveAction is (it repoints the wallet
                // at someone else's sequencer), so it is refused on a plaintext store. Same
                // treatment: Reject stays live, Approve is disabled and says why.
                Text {
                    visible: root.signingBlocked
                    Layout.fillWidth: true
                    text: "This wallet's storage has no password on it, so it cannot prove who is "
                        + "approving a zone switch and the core will refuse this. Reject it, set a "
                        + "password in Security & Backup, unlock, and ask the app again."
                    color: root.warningAmber; font.family: root.faceFont
                    font.pixelSize: 10; wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                }

                RowLayout {
                    Layout.fillWidth: true; Layout.topMargin: 2; spacing: 10
                    Rectangle {
                        Layout.fillWidth: true; height: 38; radius: 10
                        color: "transparent"; border.color: root.borderColor
                        Text { anchors.centerIn: parent; text: "Reject"
                               color: root.textSecondary; font.family: root.faceFont; font.pixelSize: 13 }
                        MouseArea {
                            anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                            onClicked: if (zoneSheet.req) root.rejectZoneRequest(zoneSheet.req.requestId)
                        }
                    }
                    Rectangle {
                        id: zoneApproveBtn
                        Layout.fillWidth: true; height: 38; radius: 10
                        enabled: !root.signingBlocked
                        opacity: enabled ? 1.0 : 0.4
                        color: !enabled ? "transparent"
                             : zoneApproveMa.pressed ? root.brandRedPressed
                             : zoneApproveMa.containsMouse ? root.brandRedHover : root.brandRed
                        border.color: enabled ? root.brandRed : root.borderColor
                        Behavior on color { ColorAnimation { duration: root.motionQuick } }
                        Text { anchors.centerIn: parent; text: "Approve"
                               color: zoneApproveBtn.enabled ? root.textPrimary : root.textDisabled
                               font.family: root.faceFont; font.pixelSize: 13; font.bold: true }
                        MouseArea {
                            id: zoneApproveMa
                            anchors.fill: parent; hoverEnabled: true
                            cursorShape: zoneApproveBtn.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                            onClicked: if (zoneSheet.req) root.approveZoneRequest(zoneSheet.req.requestId)
                        }
                    }
                }
            }
        }
    }

    // ── Job-done sheet ─────────────────────────────────────────────────────────
    // When a privacy job finishes it is removed from the jobs box and summarised here.
    // jobDoneModel is a FIFO queue (head shown first) so several completions that land
    // in the same poll are surfaced one after another. Mirrors connectSheet/actionSheet.
    Rectangle {
        id: jobDoneSheet
        z: 300
        anchors.fill: parent
        // The head queued completion (the one being shown). Null when the queue is empty.
        property var head: jobDoneModel.count > 0 ? jobDoneModel.get(0) : null
        visible: head !== null && root.walletState === "ready"
        color: "transparent"   // tint is a child below, so the backdrop blur reads through
        // Glassmorphism: blur the screen content behind the sheet (appBody is a sibling - no recursion).
        MultiEffect { anchors.fill: parent; source: appBody; blurEnabled: true; blur: 0.85; autoPaddingEnabled: false }
        // Dark scrim tint ABOVE the blur - lightened so the frosted backdrop stays visible.
        Rectangle { anchors.fill: parent; color: Qt.rgba(0, 0, 0, 0.40) }
        MouseArea { anchors.fill: parent; hoverEnabled: true; preventStealing: true
            onClicked: {} onPressed: {} onWheel: {} }   // block input behind the modal

        Rectangle {
            anchors.centerIn: parent
            width: Math.min(root.width - 40, 380)
            height: jobDoneCol.implicitHeight + 32
            radius: root.rSheet
            color: root.surface2; border.color: root.borderStrong; border.width: 1
            // Deeper elevation for the floating modal (autoPadding stops the shadow clipping).
            layer.enabled: true
            layer.effect: MultiEffect {
                shadowEnabled: true; autoPaddingEnabled: true
                shadowColor: "#000000"; shadowVerticalOffset: 8; shadowBlur: 0.6; shadowOpacity: 0.35
            }
            // sheet entrance - scale + fade
            opacity: jobDoneSheet.visible ? 1 : 0
            scale: jobDoneSheet.visible ? 1 : 0.92
            Behavior on opacity { NumberAnimation { duration: root.motionStandard; easing.type: Easing.OutCubic } }
            Behavior on scale   { NumberAnimation { duration: root.motionStandard; easing.type: Easing.OutCubic } }
            // hairline silver inner rim
            Rectangle { anchors.fill: parent; radius: parent.radius; color: "transparent"
                border.color: root.accentTint10; border.width: 1 }

            ColumnLayout {
                id: jobDoneCol
                anchors { left: parent.left; right: parent.right; top: parent.top; margins: 16 }
                spacing: 12

                RowLayout {
                    Layout.fillWidth: true; spacing: 10
                    // Outcome glyph - green tick on success, red cross on failure.
                    Rectangle {
                        Layout.preferredWidth: 38; Layout.preferredHeight: 38; radius: 19
                        color: "transparent"
                        border.width: 1
                        border.color: jobDoneSheet.head && jobDoneSheet.head.state === "error"
                                      ? root.errorRed : root.successGreen
                        Text {
                            anchors.centerIn: parent
                            text: jobDoneSheet.head && jobDoneSheet.head.state === "error" ? "✕" : "✓"
                            color: jobDoneSheet.head && jobDoneSheet.head.state === "error"
                                   ? root.errorRed : root.greenBright
                            font.pixelSize: 18; font.bold: true
                        }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true; spacing: 1
                        Text {
                            Layout.fillWidth: true
                            text: jobDoneSheet.head
                                  ? (root.opLabel(jobDoneSheet.head.op)
                                     + (jobDoneSheet.head.state === "error" ? " failed" : " complete"))
                                  : ""
                            color: root.textPrimary; font.family: root.faceFont
                            font.pixelSize: 15; font.bold: true; elide: Text.ElideRight
                        }
                        Text {
                            Layout.fillWidth: true
                            text: jobDoneSheet.head
                                  ? (jobDoneSheet.head.amount + " "
                                     + (jobDoneSheet.head.asset === "token" ? "tok" : "LEZ"))
                                  : ""
                            color: root.textSecondary; font.family: root.faceFont; font.pixelSize: 11
                        }
                    }
                }

                // Outcome detail card.
                Rectangle {
                    Layout.fillWidth: true; radius: 10
                    color: root.inputBg; border.color: root.borderColor
                    implicitHeight: jobDoneDetail.implicitHeight + 20
                    ColumnLayout {
                        id: jobDoneDetail
                        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 10 }
                        spacing: 6
                        Text {
                            Layout.fillWidth: true
                            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                            font.family: root.faceFont; font.pixelSize: 12
                            color: jobDoneSheet.head && jobDoneSheet.head.state === "error"
                                   ? root.errorRed : root.textPrimary
                            text: !jobDoneSheet.head ? ""
                                : jobDoneSheet.head.state === "error"
                                    ? (jobDoneSheet.head.error.length > 0 ? jobDoneSheet.head.error : "Failed")
                                    : "Sent to L2 - awaiting L1 confirmation"
                        }
                        Text {
                            Layout.fillWidth: true
                            visible: text.length > 0
                            elide: Text.ElideMiddle
                            font.family: root.monoFont; font.pixelSize: 10
                            color: root.textDisabled
                            text: (jobDoneSheet.head && jobDoneSheet.head.state !== "error"
                                   && jobDoneSheet.head.txId.length > 0)
                                  ? jobDoneSheet.head.txId : ""
                        }
                    }
                }

                Rectangle {
                    id: jobDoneDismissBtn
                    Layout.fillWidth: true; height: 38; radius: 10
                    color: jobDoneDismissMa.pressed ? root.brandRedPressed
                         : jobDoneDismissMa.containsMouse ? root.brandRedHover : root.brandRed
                    Behavior on color { ColorAnimation { duration: root.motionQuick } }
                    Text { anchors.centerIn: parent; text: "Done"
                           color: root.textPrimary; font.family: root.faceFont; font.pixelSize: 13; font.bold: true }
                    MouseArea {
                        id: jobDoneDismissMa
                        anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                        onClicked: root.advanceJobDone()
                    }
                }
            }
        }

        // FIFO queue of finished-job summaries (head shown first).
        ListModel { id: jobDoneModel }
    }

    // ── Zone-offline modal (blocking) ──────────────────────────────────────────
    // Raised whenever a user-triggered sequencer op is attempted while the zone is
    // unreachable, or an op fails with a connection-class error - an op must NEVER
    // fail silently, not even on Tor zones where passive-refresh noise is deliberately
    // suppressed. Mirrors connectSheet/jobDoneSheet; higher z so it stacks above the
    // job-done sheet when an async job died because the zone is down.
    Rectangle {
        id: zoneOfflineSheet
        z: 320
        anchors.fill: parent
        visible: root.zoneOfflineOpen
        color: "transparent"   // tint is a child below, so the backdrop blur reads through
        // Glassmorphism: blur the screen content behind the sheet (appBody is a sibling - no recursion).
        MultiEffect { anchors.fill: parent; source: appBody; blurEnabled: true; blur: 0.85; autoPaddingEnabled: false }
        // Dark scrim tint ABOVE the blur - lightened so the frosted backdrop stays visible.
        Rectangle { anchors.fill: parent; color: Qt.rgba(0, 0, 0, 0.40) }
        MouseArea { anchors.fill: parent; hoverEnabled: true; preventStealing: true
            onClicked: {} onPressed: {} onWheel: {} }   // block input behind the modal

        // What to do, per zone kind + failure state. seqProblemBody() carries the
        // reason-specific local-sequencer advice (incl. binary-missing / tor-missing).
        function hintText() {
            if (root.zoneOfflineMismatch) return root.seqProblemBody()
            if (root.seqMode === "local-standalone") {
                if (root.seqProblem !== "") return root.seqProblemBody()
                return "The local sequencer is still starting - give it a few seconds, then retry."
            }
            if (root.seqMode === "local-l1-tor") {
                if (root.seqProblem !== "") return root.seqProblemBody()
                return "The Tor tunnel to this zone isn't up yet. Wait for the connect bar to "
                     + "finish, check your network, or switch zone."
            }
            return "Check your network connection - or the zone may be down. Retry, or switch zone."
        }

        Rectangle {
            anchors.centerIn: parent
            width: Math.min(root.width - 40, 380)
            height: Math.min(root.height - 40, zoneOffCol.implicitHeight + 32)
            radius: root.rSheet
            color: root.surface2; border.color: root.borderStrong; border.width: 1
            // Deeper elevation for the floating modal (autoPadding stops the shadow clipping).
            layer.enabled: true
            layer.effect: MultiEffect {
                shadowEnabled: true; autoPaddingEnabled: true
                shadowColor: "#000000"; shadowVerticalOffset: 8; shadowBlur: 0.6; shadowOpacity: 0.35
            }
            // sheet entrance - scale + fade
            opacity: zoneOfflineSheet.visible ? 1 : 0
            scale: zoneOfflineSheet.visible ? 1 : 0.92
            Behavior on opacity { NumberAnimation { duration: root.motionStandard; easing.type: Easing.OutCubic } }
            Behavior on scale   { NumberAnimation { duration: root.motionStandard; easing.type: Easing.OutCubic } }
            // hairline silver inner rim
            Rectangle { anchors.fill: parent; radius: parent.radius; color: "transparent"
                border.color: root.accentTint10; border.width: 1 }

            ColumnLayout {
                id: zoneOffCol
                anchors { left: parent.left; right: parent.right; top: parent.top; margins: 16 }
                spacing: 12

                RowLayout {
                    Layout.fillWidth: true; spacing: 10
                    Rectangle {
                        Layout.preferredWidth: 38; Layout.preferredHeight: 38; radius: 19
                        color: "transparent"; border.width: 1; border.color: root.errorRed
                        Text { anchors.centerIn: parent
                            text: root.zoneOfflineMismatch ? "≠" : "⚡"
                            color: root.errorRed; font.pixelSize: 17; font.bold: true }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true; spacing: 1
                        Text { Layout.fillWidth: true
                            text: root.zoneOfflineMismatch ? "Zone build mismatch" : "Zone connection offline"
                            color: root.textPrimary; font.family: root.faceFont
                            font.pixelSize: 15; font.bold: true; elide: Text.ElideRight }
                        Text { Layout.fillWidth: true
                            visible: root.zoneOfflineOp.length > 0
                            text: root.zoneOfflineOp + (root.zoneOfflineMismatch
                                  ? " can't run against this zone" : " needs a zone connection")
                            color: root.textSecondary; font.family: root.faceFont; font.pixelSize: 11
                            elide: Text.ElideRight }
                    }
                }

                // The zone in question: display name, endpoint, and what to do about it.
                Rectangle {
                    Layout.fillWidth: true; radius: 10
                    color: root.inputBg; border.color: root.borderColor
                    implicitHeight: zoneOffZoneCol.implicitHeight + 20
                    ColumnLayout {
                        id: zoneOffZoneCol
                        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 10 }
                        spacing: 4
                        RowLayout {
                            Layout.fillWidth: true; spacing: 6
                            Rectangle { Layout.preferredWidth: 8; Layout.preferredHeight: 8; radius: 4
                                Layout.alignment: Qt.AlignVCenter
                                color: root.seqStatus === "running"  ? root.greenBright
                                     : root.seqStatus === "starting" ? root.connectGray : root.errorRed }
                            Text { Layout.fillWidth: true; text: root.zoneName(root.network)
                                color: root.textPrimary; font.family: root.faceFont; font.pixelSize: 12
                                font.bold: true; elide: Text.ElideRight }
                        }
                        Text { Layout.fillWidth: true; visible: text.length > 0
                            text: root.zoneEndpointDesc()
                            color: root.textDisabled; font.family: root.monoFont; font.pixelSize: 9
                            elide: Text.ElideMiddle }
                        Text { Layout.fillWidth: true
                            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                            font.family: root.faceFont; font.pixelSize: 10; color: root.textSecondary
                            text: zoneOfflineSheet.hintText() }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true; spacing: 8
                    Rectangle {   // Retry - re-checks health, closes on success
                        Layout.fillWidth: true; Layout.preferredHeight: 38; radius: 10
                        color: root.zoneRetryBusy ? root.brandRedPressed
                             : zoneOffRetryMa.pressed ? root.brandRedPressed
                             : zoneOffRetryMa.containsMouse ? root.brandRedHover : root.brandRed
                        Behavior on color { ColorAnimation { duration: root.motionQuick } }
                        Text { anchors.centerIn: parent
                            text: root.zoneRetryBusy ? "Checking…" : "Retry"
                            color: root.textPrimary; font.family: root.faceFont; font.pixelSize: 13; font.bold: true }
                        MouseArea { id: zoneOffRetryMa; anchors.fill: parent; hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            enabled: !root.zoneRetryBusy
                            onClicked: root.retryZoneHealth() }
                    }
                    Rectangle {   // Close - the error toast (if any) stays for copying
                        Layout.preferredWidth: 96; Layout.preferredHeight: 38; radius: 10
                        color: "transparent"; border.color: root.borderStrong; border.width: 1
                        Text { anchors.centerIn: parent; text: "Close"
                            color: root.textSecondary; font.family: root.faceFont; font.pixelSize: 12 }
                        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                            onClicked: { zoneRetryTimer.stop(); root.zoneRetryBusy = false; root.zoneOfflineOpen = false } }
                    }
                }
            }
        }
    }

    // ── Auto-select first account on initial load only ────────────────────────
    Connections {
        target: accountModel
        function onCountChanged() {
            // Only act when nothing is selected yet (first load)
            if (root.selectedFromId.length === 0 && accountModel.count > 0) {
                root.selectedFromId      = accountModel.get(0).id
                root.selectedFromType    = accountModel.get(0).type
                root.selectedFromBalance = accountModel.get(0).balance
                root.refreshTokens()
            }
        }
    }
}
