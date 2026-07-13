// Deterministic test of the Connect-with-Medusa SDK against the EXACT JSON shapes the
// wallet's connect API returns (read out of WalletPlugin.cpp). Runs in node:  node medusa-connect.test.js
// This catches SDK logic/parsing bugs without needing Basecamp. (It does NOT exercise the real
// cross-module bridge - for that the wallet .so must expose the methods; see install.sh preflight.)
const sdk = require("./medusa-connect.js");

let fail = 0;
function ok(cond, msg) { console.log((cond ? "  ok  " : "  FAIL") + "  " + msg); if (!cond) fail++; }

// A mock `logos.callModule` mirroring the wallet's behavior, not just its shapes: perms are
// filtered at connectRequest ("at least one permission is required" when empty), every approved
// connect MINTS A NEW session with its perms frozen there (no in-place upgrades - the real
// wallet never mutates an old session's grants), action/zone verbs are gated per-session, and
// an approved zone switch flips the live zone + re-pins ONLY the requesting session. Approval
// is simulated after 2 polls (pending → pending → approved).
function mockBridge() {
    const poll = {};        // requestId -> times polled
    const kKnownPerms = ["accounts", "send", "shield", "deshield", "private", "zone"];
    const sessions = {};    // sessionId -> { perms, zone /* pinned at mint, re-pinned on own zone approval */ }
    let connectSeq = 0;     // "req-connect-<n>" ; approval mints "sess-<n>"
    const reqPerms = {};    // requestId -> filtered perms (they live on the REQUEST, like the wallet)
    let zoneSess = "";      // the session whose zone request is pending
    let activeZone = "diaphani";   // the wallet's LIVE zone (netId())
    return function (mod, method, args) {
        if (mod !== "medusa_core") return JSON.stringify({ error: "unknown module" });
        switch (method) {
            case "connectRequest": {
                const app = JSON.parse(args[0]);
                if (!app.appName) return JSON.stringify({ error: "appName is required" });
                const filtered = JSON.parse(args[1]).filter(function (p) { return kKnownPerms.indexOf(p) >= 0; });
                if (filtered.length === 0) return JSON.stringify({ error: "at least one permission is required" });
                connectSeq++;
                reqPerms["req-connect-" + connectSeq] = filtered;
                return JSON.stringify({ requestId: "req-connect-" + connectSeq });
            }
            case "requestAction": {
                const s = sessions[args[0]];
                if (!s) return JSON.stringify({ error: "no such session" });
                const a = JSON.parse(args[1]);
                const op = a.op || "send";
                if (s.perms.indexOf(op) < 0) return JSON.stringify({ error: "permission not granted: " + op });
                if (!/^[0-9]+$/.test(a.amount)) return JSON.stringify({ error: "amounts are whole numbers - no decimals" });
                return JSON.stringify({ requestId: "req-action-1" });
            }
            case "requestZone": {
                const s = sessions[args[0]];
                if (!s) return JSON.stringify({ error: "no such session" });
                if (s.perms.indexOf("zone") < 0) return JSON.stringify({ error: "permission not granted: zone" });
                const z = JSON.parse(args[1]);
                if (!z.sequencer) return JSON.stringify({ error: "sequencer is required" });
                zoneSess = args[0];
                return JSON.stringify({ requestId: "req-zone-1" });
            }
            case "actionStatus": {
                const id = args[0];
                poll[id] = (poll[id] || 0) + 1;
                if (poll[id] < 3) return JSON.stringify({ requestId: id, status: "pending" });
                if (id.indexOf("req-connect-") === 0) {
                    const sid = "sess-" + id.slice("req-connect-".length);
                    if (!sessions[sid]) sessions[sid] = { perms: reqPerms[id] || [], zone: activeZone };
                    return JSON.stringify({ requestId: id, status: "approved", sessionId: sid });
                }
                if (id === "req-zone-1") {
                    activeZone = "z-paradox-computer";                      // the wallet switched…
                    if (sessions[zoneSess]) sessions[zoneSess].zone = activeZone;  // …and re-pinned the requester
                    return JSON.stringify({ requestId: id, status: "approved", zoneId: "z-paradox-computer" });
                }
                return JSON.stringify({ requestId: id, status: "approved", jobId: "job-7" });
            }
            case "sessionInfo": {
                const s = sessions[args[0]];
                if (!s) return JSON.stringify({ error: "no such session" });
                return JSON.stringify({ sessionId: args[0], app: { appName: "Tip Jar" },
                    accounts: s.perms.indexOf("accounts") >= 0
                        ? ["Public/9Jxd2psBrnE5zyBA2Z95v7BguaPwZWvdKXYViZmRjFv8"] : [],
                    granted: s.perms, zone: activeZone, zoneAtConnect: s.zone, active: true });
            }
            case "revokeSession":
                return JSON.stringify({ ok: true });
            default:
                return JSON.stringify({ error: "invalid response" });   // mirrors the bridge's method-not-found
        }
    };
}

const medusa = sdk.create({ appName: "Tip Jar", icon: "", call: mockBridge() });

// 1) connect
const c = medusa.connect(["accounts", "send"]);
ok(c && c.requestId === "req-connect-1", "connect() returns a requestId (" + JSON.stringify(c) + ")");

// 1b) a connect whose perms ALL filter out is refused (wallet-side error passes through)
const cEmpty = medusa.connect(["bogus-perm"]);
ok(cEmpty && /at least one permission/.test(cEmpty.error || ""),
    "connect() with only unknown perms → wallet error (" + JSON.stringify(cEmpty) + ")");

// 2) poll connect status: pending, pending, approved
let st = medusa.status("req-connect-1"); ok(st.status === "pending", "status #1 pending");
st = medusa.status("req-connect-1");     ok(st.status === "pending", "status #2 pending");
st = medusa.status("req-connect-1");     ok(st.status === "approved" && st.sessionId === "sess-1",
    "status #3 approved with sessionId (" + JSON.stringify(st) + ")");

// 3) session info
const s = medusa.session("sess-1");
ok(s && s.accounts && s.accounts[0].indexOf("Public/") === 0, "session() exposes a Public account");
ok(s.zone === "diaphani", "session() reports the zone");
ok(medusa.session("sess-none").error === "no such session", "session() on an unknown id → error");

// 4) tip (whole amount) -> action requestId
const a = medusa.send("sess-1", { from: s.accounts[0], to: "Public/A6qf", amount: "1", asset: "native" });
ok(a && a.requestId === "req-action-1", "send() returns an action requestId (" + JSON.stringify(a) + ")");

// 5) poll action -> approved with jobId
let as = medusa.status("req-action-1"); ok(as.status === "pending", "action status pending");
as = medusa.status("req-action-1");     ok(as.status === "pending", "action status pending");
as = medusa.status("req-action-1");     ok(as.status === "approved" && as.jobId === "job-7",
    "action approved with jobId (" + JSON.stringify(as) + ")");

// 6) client-side decimal rejection (no bridge call)
const bad = medusa.send("sess-1", { from: s.accounts[0], to: "Public/A6qf", amount: "1.5" });
ok(bad && bad.error && /decimal/.test(bad.error), "send() rejects decimals client-side");

// 6b2) a token action without definitionId is refused client-side (no bridge call) -
//      wallet-side it would only fail AFTER the user approves
const noDef = medusa.send("sess-1", { from: s.accounts[0], to: "Public/A6qf", amount: "1", asset: "token" });
ok(noDef && /definitionId is required/.test(noDef.error || ""),
    "token action without definitionId rejected client-side");

// 6b3) …but token PRIVATE transfers are exempt (the note carries its own definition):
//      the request REACHES the wallet (which then gates on the perm), proving the
//      client-side definitionId guard did not fire.
const privTok = medusa.privateSend("sess-1", { from: "Private/A", to: "Private/B", amount: "1", asset: "token" });
ok(privTok && /permission not granted: private/.test(privTok.error || ""),
    "token private transfer passes the definitionId guard (" + JSON.stringify(privTok) + ")");

// 6b) an action verb outside the granted perms is refused (op gate, like the wallet's permForOp)
const noPerm = medusa.deshield("sess-1", { from: s.accounts[0], to: "Public/A6qf", amount: "1" });
ok(noPerm && /permission not granted: deshield/.test(noPerm.error || ""),
    "deshield() without the perm → wallet error (" + JSON.stringify(noPerm) + ")");

// 7) wallet error passthrough (a wallet { error } is returned verbatim)
const errMedusa = sdk.create({ appName: "X", icon: "", call: function () { return JSON.stringify({ error: "boom" }); } });
const e = errMedusa.connect(["accounts"]);
ok(e && e.error === "boom", "wallet error surfaces as { error } (" + JSON.stringify(e) + ")");

// 7b) a non-JSON / method-not-found bridge reply degrades to a clear error (not a crash)
const rawMedusa = sdk.create({ appName: "X", icon: "", call: function () { return "invalid response"; } });
const r2 = rawMedusa.connect(["accounts"]);
ok(r2 && r2.error && /non-JSON/.test(r2.error), "non-JSON bridge reply → { error } (" + JSON.stringify(r2) + ")");

// 8) requestZone without the "zone" permission → the wallet's perm error passes through.
//    (Perms are FROZEN per session - to gain "zone" a dApp must connect again and use the NEW
//    session; the wallet never upgrades an existing session's grants.)
const zDenied = medusa.requestZone("sess-1", { sequencer: "https://seq-testnet.paradox.computer" });
ok(zDenied && /permission not granted: zone/.test(zDenied.error || ""),
    "requestZone without the zone perm → wallet error (" + JSON.stringify(zDenied) + ")");

// 8b) client-side sequencer requirement (no bridge call)
const zBad = medusa.requestZone("sess-1", { label: "nowhere" });
ok(zBad && /sequencer/.test(zBad.error || ""), "requestZone without sequencer rejected client-side");

// 9) connect a SECOND session WITH the zone permission → requestZone: pending → approved { zoneId }
const c2 = medusa.connect(["accounts", "send", "zone"]);
ok(c2 && c2.requestId === "req-connect-2", "second connect (with zone perm) returns a requestId");
let st2;
st2 = medusa.status(c2.requestId); st2 = medusa.status(c2.requestId); st2 = medusa.status(c2.requestId);
ok(st2.status === "approved" && st2.sessionId === "sess-2", "zone-perm connect approved as a NEW session");
const z = medusa.requestZone("sess-2",
    { sequencer: "https://seq-testnet.paradox.computer", tor: false, label: "Paradox Computer" });
ok(z && z.requestId === "req-zone-1", "requestZone() returns a requestId (" + JSON.stringify(z) + ")");
let zs = medusa.status("req-zone-1"); ok(zs.status === "pending", "zone status #1 pending");
zs = medusa.status("req-zone-1");     ok(zs.status === "pending", "zone status #2 pending");
zs = medusa.status("req-zone-1");
ok(zs.status === "approved" && zs.zoneId === "z-paradox-computer",
    "zone approved with zoneId (" + JSON.stringify(zs) + ")");

// 9b) post-switch pinning semantics: the requesting session is re-pinned to the new zone;
//     the OLDER session keeps its connect-time pin and now shows live-vs-pinned drift.
const s2 = medusa.session("sess-2");
ok(s2 && s2.granted && s2.granted.indexOf("zone") >= 0, "session() reports the zone grant");
ok(s2.zone === "z-paradox-computer" && s2.zoneAtConnect === "z-paradox-computer",
    "requesting session re-pinned to the new zone (" + s2.zone + "/" + s2.zoneAtConnect + ")");
const s1after = medusa.session("sess-1");
ok(s1after.zone === "z-paradox-computer" && s1after.zoneAtConnect === "diaphani",
    "other session keeps its old pin → drift visible (live " + s1after.zone + " vs pinned " + s1after.zoneAtConnect + ")");

// 10) disconnect
ok(medusa.disconnect("sess-1").ok === true, "disconnect() ok");

console.log(fail === 0 ? "\nALL PASS" : "\n" + fail + " FAILED");
process.exit(fail === 0 ? 0 : 1);
