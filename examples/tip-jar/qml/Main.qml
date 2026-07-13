// Tip Jar - a ~Connect with Medusa~ SDK demo (Basecamp ui_qml consumer module).
//
// Demonstrates the whole dApp flow against the Medusa wallet, keys never leaving it:
//   1. "Connect with Medusa"  -> wallet pops a Connect sheet (user picks accounts + approves)
//   2. show the connected account + zone
//   3. "Align zone"            -> requestZone: wallet pops a Zone sheet and, if approved,
//                                 switches its sequencer to this dApp's chain (à la
//                                 wallet_addEthereumChain) - so the tip lands where we expect
//   4. "Send a 1 LEZ tip"      -> wallet pops an Action sheet (user approves the exact tx)
// All sensitive steps happen in the WALLET UI; this module only ever sees account ids + results.
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "medusa-connect.js" as MedusaConnect

Rectangle {
    id: root
    anchors.fill: parent
    color: "#0E0E10"

    // Demo recipient - a real account on the Paradox Computer testnet zone (channel 8888,
    // sequencer below). The zone-align step exists precisely so this id means something:
    // tipping it from a wallet parked on another zone would settle on the wrong chain.
    readonly property string tipTo:     "Public/CbgR6tj5kWx5oziiFptM7jMvrQeYY3Mzaao6ciuhSr2r"
    readonly property string tipAmount: "1"
    // The chain this dApp lives on; offered to the wallet via requestZone after connect.
    readonly property var    preferredZone: ({ sequencer: "https://seq-testnet.paradox.computer",
                                               tor: false, label: "Paradox Computer" })

    readonly property color panel:   "#17171C"
    readonly property color border_: "#2A2A32"
    readonly property color crimson: "#E0314C"
    readonly property color green:   "#3E8E58"
    readonly property color text1:   "#F4F4F6"
    readonly property color text2:   "#AEB0BA"

    property var    medusa:     null
    property string phase:      "idle"   // idle | connecting | connected | tipping | switching
    property string sessionId:  ""
    property string account:    ""
    property string zone:       ""
    property string statusMsg:  ""
    property bool   isError:    false
    property string pendingReq: ""

    function bridgeReady() { return (typeof logos !== "undefined") && logos && !!logos.callModule }
    function setStatus(m, err) { root.statusMsg = m; root.isError = !!err }
    function shortId(id) {
        if (!id) return ""
        var slash = id.indexOf("/")
        var head = slash >= 0 ? id.substring(0, slash + 1) : ""
        var body = slash >= 0 ? id.substring(slash + 1) : id
        return head + (body.length > 14 ? body.substring(0, 6) + "…" + body.substring(body.length - 6) : body)
    }

    Component.onCompleted: {
        if (!bridgeReady()) { setStatus("Run this inside Logos Basecamp - no module bridge here.", true); return }
        medusa = MedusaConnect.create({
            appName: "Tip Jar",
            icon:    "",
            call:    function (m, f, a) { return logos.callModule(m, f, a) }
        })
        setStatus("Not connected. Tap “Connect with Medusa”.", false)
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
                root.zone = (s && s.zone) ? s.zone : ""
                root.account = (s && s.accounts && s.accounts.length > 0) ? s.accounts[0] : ""
                if (!root.account) {
                    root.medusa.disconnect(root.sessionId); root.sessionId = ""; root.phase = "idle"
                    root.setStatus("Connected, but no account was exposed - reconnect and pick one.", true)
                    return
                }
                root.phase = "connected"
                root.setStatus("Connected" + (root.zone ? " on " + root.zone : "") + ".", false)
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
    function doTip() {
        if (!medusa || !sessionId || !account) return
        var r = medusa.send(sessionId, { from: account, to: root.tipTo, amount: root.tipAmount, asset: "native" })
        if (!r || r.error) { setStatus("Tip request failed: " + (r ? r.error : "no response"), true); return }
        root.pendingReq = r.requestId
        root.phase = "tipping"
        setStatus("Open the Medusa wallet and approve the tip. Waiting…", false)
        actionPoll.tries = 0; actionPoll.start()
    }
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
                stop(); root.phase = "connected"
                root.setStatus("Tip sent - " + root.tipAmount + " LEZ → " + root.shortId(root.tipTo)
                               + "  (job " + (st.jobId || "?") + ")", false)
            } else if (st.status === "rejected") {
                stop(); root.phase = "connected"
                root.setStatus("Tip rejected" + (st.error ? ": " + st.error : "") + ".", true)
            } else {
                stop(); root.phase = "connected"
                root.setStatus("Tip failed: " + (st.error || "unexpected reply") + ".", true)
            }
        }
    }

    // ── zone align (requestZone) ─────────────────────────────────────────────
    function doSwitchZone() {
        if (!medusa || !sessionId) return
        var r = medusa.requestZone(sessionId, root.preferredZone)
        if (!r || r.error) { setStatus("Zone request failed: " + (r ? r.error : "no response"), true); return }
        root.pendingReq = r.requestId
        root.phase = "switching"
        setStatus("Open the Medusa wallet and approve the sequencer switch. Waiting…", false)
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
                // The wallet switched AND re-pinned this session - re-read it for the live zone.
                var s = root.medusa.session(root.sessionId)
                root.zone = (s && s.zone) ? s.zone : (st.zoneId || "")
                root.setStatus("Wallet aligned to " + (root.preferredZone.label || st.zoneId)
                               + " (zone " + (st.zoneId || "?") + ") - ready to tip.", false)
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
        sessionId = ""; account = ""; zone = ""; phase = "idle"
        setStatus("Disconnected.", false)
    }

    // Stop waiting on an approval. A connect-cancel returns to idle; a tip/zone-cancel returns
    // to the connected state. NOTE: cancel only stops OUR polling - the abandoned request stays
    // approvable in the wallet for ~5 min (its TTL); approving it there still executes it.
    function doCancel() {
        connectPoll.stop(); actionPoll.stop(); zonePoll.stop()
        root.phase = (root.sessionId && root.account) ? "connected" : "idle"
        setStatus(root.phase === "connected" ? "Cancelled - still connected." : "Cancelled.", false)
    }

    // ── UI ─────────────────────────────────────────────────────────────────────
    ColumnLayout {
        anchors.centerIn: parent
        width: Math.min(parent.width - 48, 420)
        spacing: 18

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

        // connected account card
        Rectangle {
            Layout.fillWidth: true; radius: 10; visible: root.phase !== "idle" && root.account !== ""
            color: root.panel; border.color: root.border_; border.width: 1
            implicitHeight: acctCol.implicitHeight + 24
            ColumnLayout {
                id: acctCol; anchors.fill: parent; anchors.margins: 12; spacing: 4
                Text { text: "Connected account"; color: root.text2; font.pixelSize: 11 }
                Text { text: root.shortId(root.account); color: root.text1; font.pixelSize: 15; font.family: "monospace" }
                Text { text: root.zone ? ("zone: " + root.zone) : ""; color: root.text2; font.pixelSize: 11; visible: root.zone !== "" }
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

        // waiting-for-approval: spinner + Cancel (no auto-fail - we wait for the wallet)
        ColumnLayout {
            Layout.fillWidth: true; spacing: 8
            visible: root.phase === "connecting" || root.phase === "tipping" || root.phase === "switching"
            BusyIndicator { running: parent.visible; Layout.alignment: Qt.AlignHCenter }
            Button {
                Layout.fillWidth: true; implicitHeight: 36
                background: Rectangle { radius: 10; color: "transparent"; border.color: root.border_; border.width: 1 }
                contentItem: Text {
                    text: "Cancel"; color: root.text2; font.pixelSize: 13
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
                background: Rectangle { radius: 10; color: "transparent"; border.color: root.crimson; border.width: 1 }
                contentItem: Text {
                    text: "Align zone → " + root.preferredZone.label
                    color: root.crimson; font.pixelSize: 13; font.bold: true
                    horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                }
                onClicked: root.doSwitchZone()
            }
            Button {
                Layout.fillWidth: true; implicitHeight: 46
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
