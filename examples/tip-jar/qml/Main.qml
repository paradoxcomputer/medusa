// Tip Jar - a ~Connect with Medusa~ SDK demo (Basecamp ui_qml consumer module).
//
// Demonstrates the whole dApp flow against the Medusa wallet, keys never leaving it:
//   1. pick a CHAIN            -> the two zones this demo's recipient exists on (see zoneTable)
//   2. "Connect with Medusa"   -> wallet pops a Connect sheet (user picks accounts + approves)
//   3. show the connected account + the zone the WALLET is actually on
//   4. "Align zone"            -> requestZone: wallet pops a Zone sheet and, if approved,
//                                 switches its sequencer to the chain picked in step 1 (à la
//                                 wallet_addEthereumChain) - so the tip lands where we expect
//   5. "Send a 1 LEZ tip"      -> wallet pops an Action sheet (user approves the exact tx), and
//                                 THEN we follow the on-chain job to a terminal state before
//                                 saying anything about success. "approved" is not "sent".
// All sensitive steps happen in the WALLET UI; this module only ever sees account ids + results.
//
// The chain picker is a Repeater, and a delegate is a nested component: without this pragma every
// `root.…` inside it is an unqualified lookup (18 qmllint warnings, and a silent binding failure
// if the delegate is ever reparented). Bound + `required property` is the documented fix and
// needs Qt 6.5; Basecamp runs 6.9.
pragma ComponentBehavior: Bound
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "medusa-connect.js" as MedusaConnect

Rectangle {
    id: root
    anchors.fill: parent
    color: "#0E0E10"

    // Demo recipient. A real, INITIALISED account that exists on BOTH selectable zones under the
    // SAME id: an account id derives from its key, so one key exported on one chain and imported
    // on the other yields one id on both. It was registered under the authenticated-transfer
    // program (`auth-transfer init`) on each zone, which is not optional: a PRISTINE account
    // (balance 0, nonce 0, all-zero program_owner) is dropped at block-build time, the sequencer
    // says nothing, and the sender's wallet just polls until it reports "not included" ~8 blocks
    // later. Provenance, keystore path and the per-zone on-chain proof:
    // medusa-recovery-2026-07-31/tipjar-recipient.md (created 2026-07-31).
    readonly property string tipTo:     "Public/6LRot16tmr3Syh3HA1czPeSp153aSEYLHBsRMArnhtyV"
    readonly property string tipAmount: "1"

    // THE ONLY endpoint table in this dApp, and it is a FALLBACK. `id` is the wallet's own
    // built-in zone id; refreshZones() looks each id up through the wallet's getZones() and
    // prefers the endpoint + name the wallet reports, so the dApp follows the wallet rather
    // than asserting where a zone lives. These literals are what we use when getZones is
    // unavailable (a wallet too old to have it) or does not list the id - both zones run LEZ
    // v0.2.0 with identical program ids, which is why the same recipient works on either.
    readonly property var zoneTable: [
        { id: "paradox-clearnet", label: "Paradox Computer",
          sequencer: "https://seq-testnet.paradox.computer/", tor: false },
        { id: "logos-testnet",    label: "Logos public testnet",
          sequencer: "https://testnet.lez.logos.co/",         tor: false }
    ]

    readonly property color panel:   "#17171C"
    readonly property color border_: "#2A2A32"
    readonly property color crimson: "#E0314C"
    readonly property color green:   "#3E8E58"
    readonly property color amber:   "#D6A03B"
    readonly property color text1:   "#F4F4F6"
    readonly property color text2:   "#AEB0BA"

    property var    medusa:     null
    // idle | connecting | connected | switching | tipping | settling
    //   tipping  = waiting for the user to approve in the wallet
    //   settling = approved, following the wallet's background job to a terminal state
    property string phase:      "idle"
    property string sessionId:  ""
    property string account:    ""
    property string statusMsg:  ""
    property bool   isError:    false
    property string pendingReq: ""
    property string jobId:      ""

    // ── zone state ──────────────────────────────────────────────────────────────
    property var    zones:         root.zoneTable  // refreshZones() replaces this with the wallet's view
    property int    zoneIdx:       0               // which of `zones` the user picked
    property var    walletZone:    null            // {id,name,endpoint} the wallet is LIVE on, null = unknown
    property string zoneAtConnect: ""              // session.zoneAtConnect - what approveAction enforces
    property string zoneLive:      ""              // session.zone - the wallet's active zone id right now

    readonly property var selectedZone:
        root.zones[Math.max(0, Math.min(root.zoneIdx, root.zones.length - 1))]
    readonly property bool busy: root.phase === "connecting" || root.phase === "switching"
                              || root.phase === "tipping"    || root.phase === "settling"

    function bridgeReady() { return (typeof logos !== "undefined") && logos && !!logos.callModule }
    function setStatus(m, err) { root.statusMsg = m; root.isError = !!err }
    function shortId(id) {
        if (!id) return ""
        var slash = id.indexOf("/")
        var head = slash >= 0 ? id.substring(0, slash + 1) : ""
        var body = slash >= 0 ? id.substring(slash + 1) : id
        return head + (body.length > 14 ? body.substring(0, 6) + "…" + body.substring(body.length - 6) : body)
    }

    // Call a wallet verb the SDK has no named wrapper for (getZones). It goes through the SDK's
    // OWN bridge invoker, so it inherits the module id and the double-encoded-JSON normalising
    // that every other call gets, and the vendored medusa-connect.js stays byte-identical to
    // sdk/medusa-connect.js (install.sh enforces that with cmp). A named Medusa.prototype.getZones
    // would be nicer, but adding one is an SDK change and this dApp does not get to make those.
    function walletCall(method, args) {
        if (!root.medusa) return { error: "no wallet" }
        try { return root.medusa._invoke(method, args || []) }
        catch (e) { return { error: String(e) } }
    }

    // Compare two sequencer endpoints the way the wallet's approveZone does: trailing slashes and
    // case are not a different chain. (approveZone normalises with QUrl::StripTrailingSlash; both
    // entries in zoneTable carry an explicit https scheme, so this is the same comparison.)
    function epKey(ep) {
        var t = String(ep || "").trim()
        return t === "" ? "" : t.replace(/\/+$/, "").toLowerCase()
    }

    // Re-read the wallet's zone list: {zones:[{id,name,kind,endpoint,tor,builtin}], active}.
    // Fills `zones` (ids/endpoints/names as the WALLET knows them) and `walletZone` (the live one).
    function refreshZones() {
        var z = root.walletCall("getZones", [])
        if (!z || z.error || !z.zones || !z.zones.length) return false
        var merged = []
        for (var i = 0; i < root.zoneTable.length; i++) {
            var t = root.zoneTable[i]
            var m = { id: t.id, label: t.label, sequencer: t.sequencer, tor: t.tor }
            for (var k = 0; k < z.zones.length; k++) {
                var w = z.zones[k]
                if (w.id !== t.id) continue
                if (w.endpoint) m.sequencer = w.endpoint
                if (w.name)     m.label     = w.name
                m.tor = !!w.tor
                break
            }
            merged.push(m)
        }
        // Only ASSIGN on a real change. `zones` is the chain picker's Repeater model, and handing
        // a Repeater a fresh array destroys and rebuilds every delegate; this runs on a 4s timer,
        // so an unconditional assignment would tear the buttons out from under the user's finger
        // four times a minute.
        if (JSON.stringify(merged) !== JSON.stringify(root.zones))
            root.zones = merged

        var active = null
        for (var n = 0; n < z.zones.length; n++)
            if (z.zones[n].id === z.active) { active = z.zones[n]; break }
        var live = active
            ? { id: active.id, name: active.name || active.id, endpoint: active.endpoint || "" }
            : { id: z.active || "", name: z.active || "unknown", endpoint: "" }
        if (JSON.stringify(live) !== JSON.stringify(root.walletZone))
            root.walletZone = live
        return true
    }

    // Pull the session's two zone facts: `zone` is the wallet's LIVE active zone, `zoneAtConnect`
    // is the one this session is pinned to. approveAction rejects an action when they differ.
    function refreshSessionZone() {
        if (!root.medusa || !root.sessionId) return
        var s = root.medusa.session(root.sessionId)
        if (!s || s.error) return
        root.zoneLive      = s.zone || ""
        root.zoneAtConnect = s.zoneAtConnect || ""
    }

    // Is the wallet on the chain the user picked? Unknown (getZones unavailable) is NOT "yes" -
    // callers must distinguish "no" from "cannot tell" via zoneUnknown().
    function zoneAligned() {
        if (!root.walletZone) return false
        if (root.walletZone.id === root.selectedZone.id) return true
        var a = root.epKey(root.walletZone.endpoint)
        return a !== "" && a === root.epKey(root.selectedZone.sequencer)
    }
    // The wallet did not tell us where it is. Every shipped medusa_core exposes getZones, so this
    // is the "somehow older/other wallet" case: warn, but never hard-block the demo on it.
    function zoneUnknown() { return !root.walletZone }
    function walletZoneLabel() {
        if (!root.walletZone) return "an unknown chain"
        return root.walletZone.name || root.walletZone.id || "an unknown chain"
    }

    Component.onCompleted: {
        if (!bridgeReady()) { setStatus("Run this inside Logos Basecamp - no module bridge here.", true); return }
        medusa = MedusaConnect.create({
            appName: "Tip Jar",
            icon:    "",
            call:    function (m, f, a) { return logos.callModule(m, f, a) }
        })
        refreshZones()
        // Default the picker to whatever the wallet is already on, when that is one of ours -
        // the least surprising choice, and it makes "Align zone" a no-op in the common case.
        if (root.walletZone)
            for (var i = 0; i < root.zones.length; i++)
                if (root.zones[i].id === root.walletZone.id) { root.zoneIdx = i; break }
        setStatus("Pick a chain, then tap “Connect with Medusa”.", false)
    }

    // ── connect ──────────────────────────────────────────────────────────────
    function doConnect() {
        if (!medusa) return
        // Tipping to a PUBLIC address is a "send" from a public account, or a "deshield" from a
        // private one (private→public). Request BOTH so the tip works whichever account the user
        // exposes. (No "shield"/"private" - the tip recipient is public.) "zone" lets the dApp
        // offer a sequencer switch so wallet + dApp share a chain before the tip.
        var r = medusa.connect(["accounts", "send", "deshield", "zone"])
        // connectRequest error == the wallet MODULE isn't there (not installed / too old). The
        // module is always loaded when present, so this is the "wallet unavailable" case.
        if (!r || r.error) {
            setStatus("Medusa wallet not available" + (r && r.error ? " (" + r.error + ")" : "")
                      + ". Install/enable the Medusa wallet, then try again.", true)
            return
        }
        // Request accepted + pending. The approval SHEET lives in the wallet's own view, so the
        // user must OPEN the Medusa wallet to approve. Don't fail - prompt and keep waiting.
        root.pendingReq = r.requestId
        root.phase = "connecting"
        setStatus("Open the Medusa wallet and approve the connection. Waiting…", false)
        connectPoll.tries = 0; connectPoll.start()
    }
    Timer {
        id: connectPoll; interval: 800; repeat: true; property int tries: 0
        onTriggered: {
            tries++
            // Re-assert the prompt periodically; never hard-fail on "wallet not open yet" - wait
            // (with a Cancel) until the user opens the wallet + approves, or gives up explicitly.
            if (tries % 12 === 0)
                root.setStatus("Still waiting - open the Medusa wallet to approve the connection.", false)
            if (tries > 2000) { stop(); root.phase = "idle"
                root.setStatus("Stopped waiting. Open the Medusa wallet first, then Connect again.", true); return }
            var st = root.medusa.status(root.pendingReq)
            if (!st || st.status === "pending") return
            if (st.status === "approved") {
                stop()
                root.sessionId = st.sessionId
                var s = root.medusa.session(root.sessionId)
                root.account = (s && s.accounts && s.accounts.length > 0) ? s.accounts[0] : ""
                if (!root.account) {
                    root.medusa.disconnect(root.sessionId); root.sessionId = ""; root.phase = "idle"
                    root.setStatus("Connected, but no account was exposed - reconnect and pick one.", true)
                    return
                }
                root.phase = "connected"
                root.refreshZones()
                root.refreshSessionZone()
                if (root.zoneAligned())
                    root.setStatus("Connected on " + root.walletZoneLabel() + ". Ready to tip.", false)
                else
                    root.setStatus("Connected, but the wallet is on " + root.walletZoneLabel()
                                   + " and you picked " + root.selectedZone.label
                                   + ". Tap “Align zone” so the tip lands on the chain you chose.", true)
            } else if (st.status === "rejected") {
                stop(); root.phase = "idle"
                root.setStatus("Connection rejected" + (st.error ? ": " + st.error : "") + ".", true)
            } else {
                // error-shaped reply with no status (e.g. wallet reloaded, request map lost)
                stop(); root.phase = "idle"
                root.setStatus("Connection failed: " + (st.error || "unexpected reply") + ".", true)
            }
        }
    }

    // ── tip ──────────────────────────────────────────────────────────────────
    // Why a tip can't be sent right now, or "" if it can. Both cases below are ones the wallet
    // WILL refuse or mis-settle, so refusing here (before the user is prompted) beats a sheet
    // that leads nowhere.
    function tipBlockedReason() {
        if (!root.sessionId || !root.account) return "Not connected."
        root.refreshSessionZone()
        if (root.zoneLive && root.zoneAtConnect && root.zoneLive !== root.zoneAtConnect)
            return "The wallet moved to another chain since you connected, so it will refuse this "
                 + "action (\"active zone changed since connect\"). Tap “Align zone”, or reconnect."
        if (root.walletZone && !root.zoneAligned())
            return "The wallet is on " + root.walletZoneLabel() + ", not " + root.selectedZone.label
                 + " - the tip would settle on the wrong chain. Tap “Align zone” first."
        return ""
    }

    function doTip() {
        if (!medusa || !sessionId || !account) return
        refreshZones()
        var blocked = tipBlockedReason()
        if (blocked) { setStatus(blocked, true); return }
        var r = medusa.send(sessionId, { from: account, to: root.tipTo, amount: root.tipAmount, asset: "native" })
        if (!r || r.error) { setStatus("Tip request failed: " + (r ? r.error : "no response"), true); return }
        root.pendingReq = r.requestId
        root.jobId = ""
        root.phase = "tipping"
        setStatus("Open the Medusa wallet and approve the tip. Waiting…", false)
        actionPoll.tries = 0; actionPoll.start()
    }
    // Phase 1 of a tip: wait for the USER. "approved" here means the wallet accepted the request
    // and started a background job - it says NOTHING about the chain. Hand over to jobPoll.
    Timer {
        id: actionPoll; interval: 800; repeat: true; property int tries: 0
        onTriggered: {
            tries++
            if (tries % 12 === 0)
                root.setStatus("Still waiting - open the Medusa wallet to approve the tip.", false)
            if (tries > 2000) { stop(); root.phase = "connected"
                root.setStatus("Stopped waiting. Open the Medusa wallet to approve, then tip again.", true); return }
            var st = root.medusa.status(root.pendingReq)
            if (!st || st.status === "pending") return
            if (st.status === "approved") {
                stop()
                root.jobId = st.jobId || ""
                if (!root.jobId) {
                    // Approved with nothing to follow. We cannot see the chain from here, so we
                    // must not claim it settled.
                    root.phase = "connected"
                    root.setStatus("The wallet approved the tip but returned no job to track, so "
                                   + "whether it reached the chain is unknown - check the wallet.", true)
                    return
                }
                root.phase = "settling"
                root.setStatus("Approved. Waiting for the transfer to settle on "
                               + root.walletZoneLabel() + "…", false)
                jobPoll.tries = 0; jobPoll.misses = 0; jobPoll.start()
            } else if (st.status === "rejected") {
                stop(); root.phase = "connected"
                root.setStatus("Tip rejected" + (st.error ? ": " + st.error : "") + ".", true)
            } else {
                stop(); root.phase = "connected"
                root.setStatus("Tip failed: " + (st.error || "unexpected reply") + ".", true)
            }
        }
    }
    // Phase 2 of a tip: wait for the CHAIN. getJob returns
    //   {jobId,op,asset,from,to,amount,state,phase,elapsedMs}      while state == "running"
    //   + {result, txId}                                          when state == "done"
    //   + {result, error, reason?, rawError?}                      when state == "error"
    //   {error:"unknown jobId: …"} (no `state`)                    if the module lost the job
    // state is exactly running | done | error and phase is exactly queued | processing | sent
    // (WalletPlugin.h Job, WalletPlugin.cpp getJob/startPrivacyJob/onJobFinished). Nothing else
    // is invented here: an unrecognised state is reported verbatim as unconfirmed.
    Timer {
        id: jobPoll; interval: 1000; repeat: true
        property int tries: 0
        property int misses: 0    // consecutive replies with no `state` (tolerate a bridge hiccup)
        onTriggered: {
            tries++
            var j = root.medusa.getJob(root.sessionId, root.jobId)
            if (!j || !j.state) {
                misses++
                if (misses < 4) return
                stop(); root.phase = "connected"
                root.setStatus("Lost track of the tip job ("
                               + ((j && j.error) ? j.error : "no state reported")
                               + "). It may or may not have settled - check the wallet's activity.", true)
                return
            }
            misses = 0
            if (j.state === "running") {
                var secs = Math.round((j.elapsedMs || tries * jobPoll.interval) / 1000)
                var what = j.phase === "sent"   ? "submitted to the sequencer, waiting for a block"
                         : j.phase === "queued" ? "queued behind another wallet job"
                         :                        "being built and signed in the wallet"
                // Say the slow part out loud. A transfer the chain drops is only DECLARED dead
                // after the wallet has watched 8 blocks, which is about 8 minutes on a 60s-block
                // zone - so a long wait here is normal and is not yet either outcome.
                root.setStatus("Tip " + what + "… " + secs + "s elapsed. The wallet watches up to "
                               + "8 blocks (about 8 minutes on a 60s-block zone) before it calls a "
                               + "transfer not-included, so this can take a while. Cancel stops "
                               + "watching, not the transfer.", false)
                if (tries > 1800) {   // 30 min - past every honest settle time on these zones
                    stop(); root.phase = "connected"
                    root.setStatus("Still running after 30 minutes, so this dApp stopped watching. "
                                   + "Job " + root.jobId + " is still live in the wallet - the tip "
                                   + "is UNCONFIRMED, check it there.", true)
                }
                return
            }
            stop(); root.phase = "connected"
            if (j.state === "done") {
                root.setStatus("Tip sent: " + root.tipAmount + " LEZ → " + root.shortId(root.tipTo)
                               + " on " + root.walletZoneLabel()
                               + (j.txId ? "  ·  tx " + root.shortId(j.txId)
                                         : "  ·  (settled, but the wallet reported no tx id)"), false)
            } else if (j.state === "error") {
                // Do NOT promise the funds are safe. "not included within N blocks" means the tx
                // WAS submitted and the wallet gave up watching, so it can still land later. The
                // only thing this state proves is that the transfer did not complete.
                root.setStatus("Tip FAILED: " + (j.error || "no reason reported")
                               + (j.reason ? "  [" + j.reason + "]" : "")
                               + ". The transfer did not complete.", true)
            } else {
                root.setStatus("The tip job ended in an unrecognised state “" + j.state
                               + "”. Treat the tip as unconfirmed and check the wallet.", true)
            }
        }
    }

    // ── zone align (requestZone) ─────────────────────────────────────────────
    function doAlignZone() {
        if (!medusa || !sessionId) return
        var z = root.selectedZone
        var r = medusa.requestZone(sessionId, { sequencer: z.sequencer, tor: !!z.tor, label: z.label })
        if (!r || r.error) { setStatus("Zone request failed: " + (r ? r.error : "no response"), true); return }
        root.pendingReq = r.requestId
        root.phase = "switching"
        setStatus("Open the Medusa wallet and approve the switch to " + z.label + ". Waiting…", false)
        zonePoll.tries = 0; zonePoll.start()
    }
    Timer {
        id: zonePoll; interval: 800; repeat: true; property int tries: 0
        onTriggered: {
            tries++
            if (tries % 12 === 0)
                root.setStatus("Still waiting - open the Medusa wallet to approve the switch.", false)
            if (tries > 2000) { stop(); root.phase = "connected"
                root.setStatus("Stopped waiting. Open the Medusa wallet to approve, then align again.", true); return }
            var st = root.medusa.status(root.pendingReq)
            if (!st || st.status === "pending") return
            if (st.status === "approved") {
                stop(); root.phase = "connected"
                // The wallet switched AND re-pinned this session - re-read both for the live zone.
                root.refreshZones()
                root.refreshSessionZone()
                if (root.zoneAligned())
                    root.setStatus("Wallet aligned to " + root.walletZoneLabel()
                                   + " (zone " + (st.zoneId || root.selectedZone.id) + ") - ready to tip.", false)
                else
                    root.setStatus("The wallet reported a switch to zone " + (st.zoneId || "?")
                                   + ", but it now reads as " + root.walletZoneLabel()
                                   + " rather than " + root.selectedZone.label + ".", true)
            } else if (st.status === "rejected") {
                stop(); root.phase = "connected"
                root.setStatus("Zone switch rejected" + (st.error ? ": " + st.error : "") + ".", true)
            } else {
                stop(); root.phase = "connected"
                root.setStatus("Zone switch failed: " + (st.error || "unexpected reply") + ".", true)
            }
        }
    }

    function doDisconnect() {
        if (medusa && sessionId) medusa.disconnect(sessionId)
        sessionId = ""; account = ""; zoneLive = ""; zoneAtConnect = ""; jobId = ""; phase = "idle"
        setStatus("Disconnected.", false)
    }

    // Stop waiting. A connect-cancel returns to idle; a tip/zone-cancel returns to the connected
    // state. NOTE: cancel only stops OUR polling.
    //   - waiting for approval: the abandoned request stays approvable in the wallet for ~15 min
    //     (its TTL, WalletPlugin.h kReqTtlMs); approving it there still executes it. Past the TTL,
    //     status() flips to rejected "approval timed out".
    //   - settling: the on-chain job KEEPS RUNNING in the wallet. We just stop reporting on it, so
    //     the honest word for the tip afterwards is "unconfirmed", not "cancelled".
    function doCancel() {
        var wasSettling = (root.phase === "settling")
        connectPoll.stop(); actionPoll.stop(); zonePoll.stop(); jobPoll.stop()
        root.phase = (root.sessionId && root.account) ? "connected" : "idle"
        if (wasSettling)
            setStatus("Stopped watching job " + root.jobId + ". The transfer is still running in "
                      + "the wallet - the tip is UNCONFIRMED, not cancelled.", true)
        else
            setStatus(root.phase === "connected" ? "Cancelled - still connected." : "Cancelled.", false)
    }

    // While connected and idle, keep the "wallet is on …" line honest: the user can switch zones
    // in the wallet at any moment and that silently invalidates this session's actions.
    Timer {
        id: zoneWatch
        interval: 4000; repeat: true; running: root.phase === "connected"
        onTriggered: { root.refreshZones(); root.refreshSessionZone() }
    }

    // ── UI ─────────────────────────────────────────────────────────────────────
    ColumnLayout {
        anchors.centerIn: parent
        width: Math.min(parent.width - 48, 460)
        spacing: 16

        Text {
            text: "🫙  Tip Jar"
            color: root.text1; font.pixelSize: 26; font.bold: true
            Layout.alignment: Qt.AlignHCenter
        }
        Text {
            text: "A “Connect with Medusa” SDK demo"
            color: root.text2; font.pixelSize: 13
            Layout.alignment: Qt.AlignHCenter
        }

        // chain picker - pick BEFORE connecting; changing it after connect just means the
        // wallet needs re-aligning, which is exactly what requestZone is for.
        ColumnLayout {
            Layout.fillWidth: true; spacing: 6
            Text { text: "Chain"; color: root.text2; font.pixelSize: 11 }
            RowLayout {
                Layout.fillWidth: true; spacing: 8
                Repeater {
                    model: root.zones
                    delegate: Button {
                        id: zoneBtn
                        required property int index
                        required property var modelData
                        readonly property bool picked: root.zoneIdx === zoneBtn.index
                        Layout.fillWidth: true
                        implicitHeight: 40
                        enabled: !root.busy
                        opacity: enabled ? 1.0 : 0.5
                        background: Rectangle {
                            radius: 10
                            color: zoneBtn.picked ? "#20202A" : "transparent"
                            border.width: zoneBtn.picked ? 2 : 1
                            border.color: zoneBtn.picked ? root.crimson : root.border_
                        }
                        contentItem: Text {
                            text: zoneBtn.modelData.label
                            color: zoneBtn.picked ? root.text1 : root.text2
                            font.pixelSize: 12; font.bold: zoneBtn.picked
                            elide: Text.ElideRight
                            horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                        }
                        onClicked: {
                            root.zoneIdx = zoneBtn.index
                            root.refreshZones()
                            if (root.phase === "connected" && !root.zoneAligned())
                                root.setStatus("Picked " + root.selectedZone.label + ", but the wallet is on "
                                               + root.walletZoneLabel() + ". Tap “Align zone”.", true)
                            else if (root.phase === "connected")
                                root.setStatus("Ready to tip on " + root.walletZoneLabel() + ".", false)
                        }
                    }
                }
            }
            Text {
                Layout.fillWidth: true
                text: root.selectedZone.sequencer
                color: root.text2; font.pixelSize: 10; font.family: "monospace"
                elide: Text.ElideMiddle
            }
        }

        // status line
        Rectangle {
            Layout.fillWidth: true; radius: 10
            color: root.panel; border.color: root.border_; border.width: 1
            implicitHeight: stTxt.implicitHeight + 24
            Text {
                id: stTxt; anchors.fill: parent; anchors.margins: 12
                text: root.statusMsg; wrapMode: Text.WordWrap
                color: root.isError ? "#FB7185" : root.text2; font.pixelSize: 13
                verticalAlignment: Text.AlignVCenter
            }
        }

        // connected account card - and, next to it, WHERE A TIP WOULD ACTUALLY GO. The tip
        // settles wherever the WALLET is pointed, not where this dApp wishes it were, and the
        // wallet's own approval sheet shows that same network + endpoint. Never contradict it.
        Rectangle {
            Layout.fillWidth: true; radius: 10; visible: root.phase !== "idle" && root.account !== ""
            color: root.panel; border.color: root.border_; border.width: 1
            implicitHeight: acctCol.implicitHeight + 24
            ColumnLayout {
                id: acctCol; anchors.fill: parent; anchors.margins: 12; spacing: 4
                Text { text: "Connected account"; color: root.text2; font.pixelSize: 11 }
                Text {
                    Layout.fillWidth: true
                    text: root.shortId(root.account); color: root.text1
                    font.pixelSize: 15; font.family: "monospace"; elide: Text.ElideMiddle
                }
                Text {
                    Layout.fillWidth: true
                    text: "Tip settles on: " + root.walletZoneLabel()
                          + (root.walletZone && root.walletZone.endpoint
                             ? "  ·  " + root.walletZone.endpoint : "")
                    color: root.zoneAligned() ? root.text2 : root.amber
                    font.pixelSize: 11; wrapMode: Text.WordWrap
                }
                Text {
                    Layout.fillWidth: true
                    visible: !root.zoneAligned()
                    text: root.zoneUnknown()
                          ? "The wallet did not report its active zone, so where this tip settles "
                            + "cannot be confirmed from here - check the approval sheet."
                          : "You picked " + root.selectedZone.label + " - align the wallet first."
                    color: root.amber; font.pixelSize: 11; wrapMode: Text.WordWrap
                }
                Text {
                    Layout.fillWidth: true
                    text: "Recipient: " + root.shortId(root.tipTo)
                    color: root.text2; font.pixelSize: 11; font.family: "monospace"; elide: Text.ElideMiddle
                }
            }
        }

        // Connect with Medusa - branded silver button (the Medusa mark + brushed-silver finish)
        Button {
            id: connectBtn
            Layout.fillWidth: true; visible: root.phase === "idle"; enabled: root.bridgeReady()
            opacity: enabled ? 1.0 : 0.5
            implicitHeight: 48
            background: Rectangle {
                radius: 11
                border.width: 1
                border.color: connectBtn.down ? "#7E828B" : "#9A9EA7"
                // metallic silver: bright top highlight -> silver -> darker base, dips when pressed
                gradient: Gradient {
                    GradientStop { position: 0.0;  color: connectBtn.down ? "#C4C7CE" : "#F6F7F9" }
                    GradientStop { position: 0.45; color: connectBtn.down ? "#AEB2BA" : "#DDE0E5" }
                    GradientStop { position: 1.0;  color: connectBtn.down ? "#9398A1" : "#B6BAC2" }
                }
            }
            contentItem: Item {
                Row {
                    anchors.centerIn: parent
                    spacing: 10
                    Image {
                        source: "medusa-logo.png"
                        sourceSize.width: 30; sourceSize.height: 30
                        width: 28; height: 28
                        fillMode: Image.PreserveAspectFit
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Text {
                        text: "Connect with Medusa"
                        color: "#23262D"; font.pixelSize: 15; font.bold: true
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
            }
            onClicked: root.doConnect()
        }

        // waiting: spinner + Cancel (no auto-fail - we wait for the wallet, then for the chain)
        ColumnLayout {
            Layout.fillWidth: true; spacing: 8
            visible: root.busy
            BusyIndicator { running: parent.visible; Layout.alignment: Qt.AlignHCenter }
            Button {
                Layout.fillWidth: true; implicitHeight: 36
                background: Rectangle { radius: 10; color: "transparent"; border.color: root.border_; border.width: 1 }
                contentItem: Text {
                    text: root.phase === "settling" ? "Stop watching" : "Cancel"
                    color: root.text2; font.pixelSize: 13
                    horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                }
                onClicked: root.doCancel()
            }
        }

        // Align zone + Tip + Disconnect (connected)
        ColumnLayout {
            Layout.fillWidth: true; spacing: 10; visible: root.phase === "connected"
            Button {
                Layout.fillWidth: true; implicitHeight: 40
                background: Rectangle {
                    radius: 10
                    color: root.zoneAligned() ? "transparent" : "#2A1B1F"
                    border.color: root.crimson; border.width: root.zoneAligned() ? 1 : 2
                }
                contentItem: Text {
                    text: (root.zoneAligned() ? "Re-align zone → " : "Align zone → ") + root.selectedZone.label
                    color: root.crimson; font.pixelSize: 13; font.bold: true
                    elide: Text.ElideRight
                    horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                }
                onClicked: root.doAlignZone()
            }
            Button {
                Layout.fillWidth: true; implicitHeight: 46
                // Blocked only on a KNOWN mismatch. If the wallet never told us where it is we
                // still allow the tip (with the caveat shown above): tipBlockedReason() applies
                // the same rule, so the button and the preflight can never disagree.
                enabled: root.zoneAligned() || root.zoneUnknown()
                opacity: enabled ? 1.0 : 0.45
                background: Rectangle { radius: 10; color: root.green }
                contentItem: Text {
                    text: "Send a " + root.tipAmount + " LEZ tip"; color: "white"; font.pixelSize: 15; font.bold: true
                    horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                }
                onClicked: root.doTip()
            }
            Button {
                Layout.fillWidth: true; implicitHeight: 38
                background: Rectangle { radius: 10; color: "transparent"; border.color: root.border_; border.width: 1 }
                contentItem: Text {
                    text: "Disconnect"; color: root.text2; font.pixelSize: 13
                    horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                }
                onClicked: root.doDisconnect()
            }
        }
    }
}
