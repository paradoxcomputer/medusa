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
    property string activeTab:           "tokens"    // tokens | activity
    property string network:             "diaphani"  // active zone id (default: Paradox Computer · Tor)
    property var    zones:               []          // [{id,name,kind,endpoint,tor,builtin}]
    property bool   addZoneOpen:         false       // add/edit-zone form in the Zones screen
    property string editingZoneId:       ""          // non-empty → the form edits this zone
    property string renamingAcctId:      ""          // non-empty → that account row is being renamed
    property bool   cliFound:            false
    property string seqMode:             "local"     // local | hosted
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
    // Onboarding state machine: loading | new | plaintext | locked | backup | ready
    property string walletState:   "loading"
    property bool   revealMnemonic: false
    property bool   revealKey:      false
    property string exportedMnemonic: ""
    property string exportedKey:      ""
    property string secBusy:          ""      // non-empty = an op label in flight

    // ── Helpers ───────────────────────────────────────────────────────────────
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
            if (!st.exists) {
                root.walletState = "new"; root.walletLocked = false
                accountModel.clear(); refreshAccountBuckets(); return
            }
            if (st.encrypted && !st.unlocked) {
                root.walletState = "locked"; root.walletLocked = true
                accountModel.clear(); refreshAccountBuckets(); return
            }
            if (!st.encrypted) {
                // Wallet exists but isn't encrypted - require a password before use.
                root.walletState = "plaintext"; root.walletLocked = false
                accountModel.clear(); refreshAccountBuckets(); return
            }
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
            var r = callModuleParse(logos.callModule("medusa_core", "startSendToken",
                        [root.selectedFromId, to, root.sendTokenDef, amount]))
            if (!r || r.error) { logActivity("Token send failed: " + (r && r.error ? r.error : "unknown"), true); return }
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
        var r = callModuleParse(
            logos.callModule("medusa_core", "startSendTransfer", [from, to, amount])
        )
        if (!r || r.error) {
            logActivity("Transfer failed: " + (r && r.error ? r.error : "unknown"), true)
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
        var acctId = root.selectedFromId
        if (!acctId && accountModel.count > 0) acctId = accountModel.get(0).id
        if (!acctId) { root.logActivity("No accounts - create one first", true); return }
        var r = callModuleParse(logos.callModule("medusa_core", "startFaucet", [acctId]))
        if (!r || r.error) { root.logActivity("Faucet failed: " + (r && r.error ? r.error : "unknown"), true); return }
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
        var r = root.callModuleParse(logos.callModule("medusa_core",
            "approveAction", [req.requestId]))
        if (r && r.error)        { root.logActivity("Action failed: " + r.error, true); return }
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
        var r = callModuleParse(logos.callModule("medusa_core", "approveZone", [requestId]))
        if (!r || r.error || r.status !== "approved") {
            root.logActivity("Sequencer switch failed: "
                             + ((r && (r.error || r.status)) || "no response"), true)
            root.pollConnRequests()
            return
        }
        // The wallet is now on the requested zone - mirror switchZone(): re-select + refresh
        // so the network label, accounts and balances don't linger on the old zone.
        root.network = r.zoneId
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
        var amt = root.privAmount.trim()
        if (!root.privFromValid || amt.length === 0) return

        var from = root.selectedFromId
        var method, args
        if (root.privMode === "shield") {
            if (root.privToId.length === 0) { root.logActivity("Pick a private destination account", true); return }
            method = "startShield";   args = [root.privAsset, from, root.privToId, amt]
            if (root.privAsset === "token") args.push(root.privTokenDef)   // token shield needs the def
        } else if (root.privMode === "deshield") {
            if (root.privToId.length === 0) { root.logActivity("Pick a public destination account", true); return }
            method = "startDeshield"; args = [root.privAsset, from, root.privToId, amt]
            if (root.privAsset === "token") args.push(root.privTokenDef)   // def routes to the recipient's ATA
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
                method = "startPrivateTransfer"; args = [root.privAsset, from, root.privToId, amt]
            }
        }

        root.privBusy = true
        var r = callModuleParse(logos.callModule("medusa_core", method, args))
        root.privBusy = false

        if (!r || r.error) {
            root.logActivity(opLabel(root.privMode === "transfer" ? "private" : root.privMode)
                             + " failed: " + (r && r.error ? r.error : "unknown"), true)
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
            root.logActivity(opLabel(j.op) + " failed: " + (j.error || "unknown"), true)
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

    // ── Security & backup helpers ──────────────────────────────────────────────
    function doUnlock(pw) {
        if (typeof logos === "undefined" || !logos.callModule) return
        runBusy("Unlocking", function() {
            var r = callModuleParse(logos.callModule("medusa_core", "unlock", [pw]))
            if (r && r.error) { root.logActivity("Unlock failed: " + r.error, true); return }
            root.walletLocked = false
            root.logActivity("Wallet unlocked", false)
            root.refreshAccounts()
        })
    }

    function doResetWallet() {
        if (typeof logos === "undefined" || !logos.callModule) return
        var r = callModuleParse(logos.callModule("medusa_core", "resetWallet", []))
        if (!r || r.error) { root.logActivity("Reset failed: " + (r && r.error ? r.error : "unknown"), true); return }
        root.walletLocked = false
        root.resetArmed = false
        root.walletState = "loading"          // force re-routing → "new" after the wipe
        root.exportedMnemonic = ""; root.exportedKey = ""
        root.logActivity("Wallet erased - starting fresh", false)
        root.refreshAccounts()
    }

    function doCreateEncrypted(pw) {
        if (typeof logos === "undefined" || !logos.callModule) return
        if (!pw || pw.length === 0) { root.logActivity("Choose a password first", true); return }
        runBusy("Creating", function() {
            var r = callModuleParse(logos.callModule("medusa_core", "createEncryptedWallet", [pw]))
            if (!r || r.error) { root.logActivity("Create failed: " + (r && r.error ? r.error : "unknown"), true); return }
            root.walletLocked = false
            root.logActivity("Encrypted wallet created", false)
            if (r.mnemonic) {
                root.exportedMnemonic = r.mnemonic; root.revealMnemonic = true
                root.walletState = "backup"          // show the phrase before entering the wallet
            } else {
                root.walletState = "ready"; root.refreshAccounts()
            }
        })
    }

    function finishBackup() {
        root.walletState = "ready"
        root.revealMnemonic = false
        root.refreshAccounts()
    }

    function doExportMnemonic() {
        if (typeof logos === "undefined" || !logos.callModule) return
        runBusy("Exporting", function() {
            var r = callModuleParse(logos.callModule("medusa_core", "exportMnemonic", []))
            if (!r || r.error) { root.logActivity("Reveal phrase: " + (r && r.error ? r.error : "failed"), true); return }
            root.exportedMnemonic = r.mnemonic || ""
            root.revealMnemonic = true
        })
    }

    function doExportKey() {
        if (typeof logos === "undefined" || !logos.callModule) return
        if (root.selectedFromId.length === 0) { root.logActivity("Select an account first", true); return }
        if (root.selectedFromType !== "public") { root.logActivity("Key export is for public accounts", true); return }
        runBusy("Exporting", function() {
            var r = callModuleParse(logos.callModule("medusa_core", "exportKey", [root.selectedFromId]))
            if (!r || r.error) { root.logActivity("Export key: " + (r && r.error ? r.error : "failed"), true); return }
            root.exportedKey = r.privateKey || ""
            root.revealKey = true
        })
    }

    function doRestore(phrase, pw, depth) {
        if (typeof logos === "undefined" || !logos.callModule) return
        if (!phrase || phrase.trim().split(/\s+/).length < 12) { root.logActivity("Enter a valid recovery phrase", true); return }
        runBusy("Restoring", function() {
            var r = callModuleParse(logos.callModule("medusa_core", "restoreWallet", [phrase.trim(), pw, depth]))
            if (!r || r.error) { root.logActivity("Restore failed: " + (r && r.error ? r.error : "unknown"), true); return }
            root.walletLocked = false
            root.logActivity("Wallet restored from recovery phrase", false)
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

    Component.onCompleted: {
        if (typeof logos === "undefined" || !logos.callModule) return
        var cfg = callModuleParse(logos.callModule("medusa_core", "getConfig", []))
        if (cfg && cfg.cliPathEff) cliPathField.text = cfg.cliPathEff
        var scfg = callModuleParse(logos.callModule("medusa_core", "getSequencerConfig", []))
        if (scfg) {
            root.seqPort = scfg.port || 3071
            if (scfg.network) root.network = scfg.network
        }
        root.refreshStatus()
        root.refreshSeqStatus()
        root.refreshZones()
        root.refreshAccounts()
        root.refreshWhitelist()
        root.checkForUpdate()
        if (!root.cliFound) root.screen = "settings"
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

        // ── Security & Backup panel ─────────────────────────────────────────────
        Rectangle {
            Layout.fillWidth: true
            // Backup/export screen - only once the wallet is unlocked; create/unlock
            // are handled by the dedicated onboarding screen.
            visible: root.screen === "security" && root.walletState === "ready"
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

                // ── Locked: unlock ──
                ColumnLayout {
                    visible: root.walletLocked
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

                    // Escape hatch - forgotten password / not your wallet → start over.
                    RowLayout {
                        Layout.fillWidth: true; spacing: 6
                        Text { font.family: root.faceFont;
                            text: "Forgot the password, or it isn't your wallet?"
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
                ColumnLayout {
                    visible: !root.walletLocked
                    Layout.fillWidth: true; spacing: 8

                    Text { font.family: root.faceFont; text: "SECURITY & BACKUP"; color: root.brandRed; font.pixelSize: 9; font.bold: true; font.letterSpacing: 1.2 }
                    Text { font.family: root.faceFont;
                        text: "Never share your recovery phrase or private keys - anyone with them controls your funds."
                        color: root.textDisabled; font.pixelSize: 9; wrapMode: Text.WordWrap; Layout.fillWidth: true
                    }

                    // Create encrypted wallet (set a password) - for a fresh wallet
                    RowLayout {
                        Layout.fillWidth: true; spacing: 6
                        Rectangle {
                            Layout.fillWidth: true; height: 26; color: root.inputBg; border.color: newPwField.activeFocus ? root.accentOrange : root.borderColor; radius: 8
                            TextInput { font.family: root.faceFont;
                                id: newPwField
                                anchors { fill: parent; leftMargin: 6; rightMargin: 6 }
                                verticalAlignment: TextInput.AlignVCenter; echoMode: TextInput.Password
                                color: root.textPrimary; font.pixelSize: 10; clip: true
                                Text { font.family: root.faceFont;
                                    anchors.fill: parent; verticalAlignment: Text.AlignVCenter
                                    text: parent.text.length === 0 ? "set a password (new encrypted wallet)" : ""
                                    color: root.textDisabled; font.pixelSize: 10
                                }
                            }
                        }
                        Rectangle {
                            Layout.preferredWidth: 70; height: 26; radius: 10
                            color: secCreateMa.pressed ? root.brandRedPressed
                                 : secCreateMa.containsMouse ? root.brandRedHover : root.brandRed
                            border.color: root.brandRed
                            Behavior on color { ColorAnimation { duration: root.motionQuick } }
                            Text { font.family: root.faceFont; anchors.centerIn: parent; text: root.secBusy === "Creating" ? "…" : "Create"; color: root.textPrimary; font.pixelSize: 10 }
                            MouseArea { id: secCreateMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: root.doCreateEncrypted(newPwField.text) }
                        }
                    }

                    Rectangle { Layout.fillWidth: true; height: 1; color: root.borderColor }

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

                Text { font.family: root.faceFont; text: "Wallet CLI path"; color: root.textSecondary; font.pixelSize: 10 }
                Rectangle {
                    Layout.fillWidth: true; height: 26; color: root.inputBg
                    border.color: root.borderColor; radius: 8
                    TextInput { font.family: root.faceFont;
                        id: cliPathField
                        anchors { fill: parent; leftMargin: 6; rightMargin: 6 }
                        verticalAlignment: TextInput.AlignVCenter
                        color: root.textPrimary; font.pixelSize: 11; clip: true
                        Text { font.family: root.faceFont;
                            anchors.fill: parent; verticalAlignment: Text.AlignVCenter
                            text: parent.text.length === 0 ? "~/.local/bin/wallet" : ""
                            color: root.textDisabled; font.pixelSize: 11;                        }
                    }
                }
                Text { font.family: root.faceFont
                    text: "The network connection is configured per-zone - switch or add zones from the network selector at the top."
                    color: root.textDisabled; font.pixelSize: 9; wrapMode: Text.WordWrap; Layout.fillWidth: true }

                RowLayout {
                    Layout.fillWidth: true
                    Item { Layout.fillWidth: true }
                    Rectangle {
                        width: 56; height: 24; radius: 10; color: "transparent"; border.color: root.accentOrange
                        Text { font.family: root.faceFont; anchors.centerIn: parent; text: "Save"; color: root.accentOrange; font.pixelSize: 11 }
                        MouseArea {
                            anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                logos.callModule("medusa_core", "setCliPath", [cliPathField.text])
                                root.screen = "main"
                                root.refreshStatus()
                                root.refreshSeqStatus()
                            }
                        }
                    }
                }
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
                                        if (wasActive) { root.selectedFromId = ""; root.selectedTokens = []; root.refreshSeqStatus(); netReloadTimer.restart() }
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
                                else if (editedId && root.network === editedId) { root.refreshSeqStatus(); netReloadTimer.restart() }
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
                    text: root.walletState === "locked"    ? "Welcome back"
                        : root.walletState === "plaintext" ? "Secure your wallet"
                        : root.walletState === "backup"    ? "Back up your recovery phrase"
                        : "Create your wallet"
                    font.pixelSize: 15; font.bold: true; color: root.textPrimary
                }
                Text { font.family: root.faceFont;
                    Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap
                    color: root.textSecondary; font.pixelSize: 11
                    text: root.walletState === "locked"    ? "Enter your password to unlock."
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
                Rectangle {
                    visible: root.walletState === "new" || root.walletState === "plaintext"
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
                    visible: (root.walletState === "new" || root.walletState === "plaintext")
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
                            if (root.walletState === "locked") root.doUnlock(onbPw.text)
                            else root.doCreateEncrypted(onbPw.text)
                        }
                    }
                }

                // locked: escape hatch
                Rectangle {
                    visible: root.walletState === "locked"
                    Layout.alignment: Qt.AlignHCenter; Layout.preferredWidth: 170; height: 22; radius: 10
                    color: "transparent"; border.color: root.borderColor
                    Text { font.family: root.faceFont;
                        anchors.centerIn: parent
                        text: root.resetArmed ? "Tap again to erase wallet" : "Forgot password? Reset"
                        color: root.errorRed; font.pixelSize: 9
                    }
                    MouseArea {
                        anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                        onClicked: { if (root.resetArmed) root.doResetWallet(); else root.resetArmed = true }
                    }
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

                    Rectangle {
                        id: confirmBtn
                        Layout.fillWidth: true; height: 36; radius: 10
                        property bool canSend: root.selectedFromId.length > 0
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

                        // Confirm
                        Rectangle {
                            id: privConfirmBtn
                            Layout.fillWidth: true; height: 36; radius: 10
                            // de-anonymizing modes (deshield, foreign transfer) require an explicit ack
                            readonly property bool needsAck:
                                root.privMode === "deshield" || (root.privMode === "transfer" && root.privToMode === "foreign")
                            property bool canConfirm:
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
    Rectangle {
        id: toastCard
        z: 200
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
                        Text {
                            Layout.fillWidth: true
                            text: connectSheet.req && connectSheet.req.app
                                  ? (connectSheet.req.app.appName || "An app") : "An app"
                            color: root.textPrimary; font.family: root.faceFont
                            font.pixelSize: 15; font.bold: true; elide: Text.ElideRight
                        }
                        Text {
                            Layout.fillWidth: true
                            text: "wants to connect to your wallet"
                            color: root.textSecondary; font.family: root.faceFont; font.pixelSize: 11
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
            height: actionCol.implicitHeight + 32
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

                Text {
                    Layout.fillWidth: true
                    text: root.connActionHint(actionSheet.req)
                    color: root.textSecondary; font.family: root.faceFont
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
                        color: actionApproveMa.pressed ? root.brandRedPressed
                             : actionApproveMa.containsMouse ? root.brandRedHover : root.brandRed
                        Behavior on color { ColorAnimation { duration: root.motionQuick } }
                        Text { anchors.centerIn: parent; text: "Approve"
                               color: root.textPrimary; font.family: root.faceFont; font.pixelSize: 13; font.bold: true }
                        MouseArea {
                            id: actionApproveMa
                            anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
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
            height: zoneCol.implicitHeight + 32
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
                        Text {
                            Layout.fillWidth: true
                            text: (zoneSheet.req ? (zoneSheet.req.appName || "An app") : "An app")
                                  + " wants to switch your wallet's sequencer"
                            color: root.textPrimary; font.family: root.faceFont
                            font.pixelSize: 15; font.bold: true
                            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                        }
                        Text {
                            Layout.fillWidth: true
                            text: "Review the requested zone before approving"
                            color: root.textSecondary; font.family: root.faceFont; font.pixelSize: 11
                        }
                    }
                }

                // Requested zone detail card.
                Rectangle {
                    Layout.fillWidth: true; radius: 10
                    color: root.inputBg; border.color: root.borderColor
                    implicitHeight: zoneDetailCol.implicitHeight + 20
                    ColumnLayout {
                        id: zoneDetailCol
                        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 10 }
                        spacing: 6
                        RowLayout {
                            Layout.fillWidth: true
                            Text { text: "Label"; color: root.textSecondary; font.family: root.faceFont; font.pixelSize: 11 }
                            Item { Layout.fillWidth: true }
                            Text { text: zoneSheet.req ? (zoneSheet.req.label || "-") : ""
                                   color: root.textPrimary; font.family: root.faceFont; font.pixelSize: 11; font.bold: true
                                   elide: Text.ElideRight; Layout.maximumWidth: 200 }
                        }
                        Text { text: "Sequencer"; color: root.textSecondary; font.family: root.faceFont; font.pixelSize: 11 }
                        Text {
                            Layout.fillWidth: true
                            text: zoneSheet.req ? (zoneSheet.req.sequencer || "") : ""
                            color: root.textPrimary; font.family: root.monoFont; font.pixelSize: 11
                            elide: Text.ElideMiddle
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
                    }
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
                        color: zoneApproveMa.pressed ? root.brandRedPressed
                             : zoneApproveMa.containsMouse ? root.brandRedHover : root.brandRed
                        Behavior on color { ColorAnimation { duration: root.motionQuick } }
                        Text { anchors.centerIn: parent; text: "Approve"
                               color: root.textPrimary; font.family: root.faceFont; font.pixelSize: 13; font.bold: true }
                        MouseArea {
                            id: zoneApproveMa
                            anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
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
