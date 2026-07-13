// Connect with Medusa - SDK (JS, QML-importable wrapper over the Basecamp `logos.callModule` bridge).
//
// Mental model: window.ethereum / WalletConnect, but for the Medusa wallet inside Basecamp.
// Keys never cross the bridge - the SDK only ever sees account ids + results, and every connect
// and every write is approved by the user IN THE WALLET UI.
//
// Usage from a Basecamp ui_qml module:
//
//   import "medusa-connect.js" as MedusaConnect
//   property var medusa: MedusaConnect.create({
//       appName: "Tip Jar",
//       icon:    "data:image/png;base64,…",
//       call:    function(m, f, a) { return logos.callModule(m, f, a) }   // the bridge, captured here
//   })
//
// Connect + writes are user-approved in the wallet, so they're 2-phase: a *Request returns
// { requestId } immediately, then you POLL status(requestId) (drive it from a QML Timer) until
// status !== "pending". Reads (session) are instant.
//
//   var req = medusa.connect(["accounts","send"])          // -> { requestId }
//   // … QML Timer polls medusa.status(req.requestId) until "approved" -> { sessionId } …
//   var s   = medusa.session(sessionId)                    // -> { accounts, granted, zone, … }
//   var act = medusa.send(sessionId, { from, to, amount: "1", asset: "native" })  // -> { requestId }
//   // … poll medusa.status(act.requestId) -> "approved" { jobId } | "rejected" { error } …

function create(opts) { return new Medusa(opts); }

function _parse(raw) {
    // The bridge returns a JSON string (sometimes double-encoded). Normalise to an object.
    try {
        var t = JSON.parse(raw);
        if (typeof t === "string") { try { return JSON.parse(t); } catch (e) { return t; } }
        return t;
    } catch (e) { return { error: "bridge returned non-JSON: " + String(raw) }; }
}

function Medusa(opts) {
    opts = opts || {};
    this.appName = opts.appName || "Unknown app";
    this.icon    = opts.icon   || "";
    this.origin  = opts.origin || "";
    this.module  = opts.module || "medusa_core";
    this._call   = opts.call;   // function(moduleId, method, argsArray) -> JSON string
    if (typeof this._call !== "function")
        throw new Error("Medusa: opts.call (the logos.callModule bridge) is required");
}

Medusa.prototype._invoke = function (method, args) {
    return _parse(this._call(this.module, method, args || []));
};

// LEZ has no decimals - reject non-whole amounts client-side (the wallet re-validates too).
Medusa.prototype.isWholeAmount = function (a) { return /^[0-9]+$/.test(String(a)); };

// ── connect ────────────────────────────────────────────────────────────────
// Submit a connect request. perms ⊆ ["accounts","send","shield","deshield","private","zone"].
// Returns { requestId } | { error }. The wallet then pops its Connect sheet for the user.
Medusa.prototype.connect = function (perms) {
    var app = JSON.stringify({ appName: this.appName, icon: this.icon, origin: this.origin });
    return this._invoke("connectRequest", [app, JSON.stringify(perms || ["accounts"])]);
};

// The single polling primitive for BOTH connect- and action-requests:
//   pending  -> { status: "pending" }
//   approved -> { status: "approved", sessionId }   (for a connect request)
//            -> { status: "approved", jobId }        (for an action request)
//            -> { status: "approved", zoneId }       (for a zone request)
//   rejected -> { status: "rejected", error }
Medusa.prototype.status = function (requestId) { return this._invoke("actionStatus", [requestId]); };

// Session details once connected: { sessionId, app, accounts, granted, zone, active }.
Medusa.prototype.session = function (sessionId) { return this._invoke("sessionInfo", [sessionId]); };

// ── reads ────────────────────────────────────────────────────────────────────
// Granted account ids for the session (needs the "accounts" permission - without it the wallet
// returns an empty list). Returns { accounts: [...] } | { error }.
Medusa.prototype.getAccounts = function (sessionId) {
    var s = this.session(sessionId);
    if (s && s.error) return s;
    return { accounts: (s && s.accounts) || [] };
};
// Public on-chain LEZ balance of an account → { balance, … } | { error }.
Medusa.prototype.getBalance = function (sessionId, accountId) { return this._invoke("getBalance", [accountId]); };
// Token holdings of an account → [{ definitionId, ticker, balance }] | { error }.
Medusa.prototype.getTokens = function (sessionId, accountId) { return this._invoke("getTokens", [accountId]); };
// Resolve a jobId from an approved action → { state, txId?, error?, … }. Poll until
// state !== "running" to surface the on-chain txId (the poll-style "awaitJob").
Medusa.prototype.getJob = function (sessionId, jobId) { return this._invoke("getJob", [jobId]); };
// True while the session is still live (the user hasn't disconnected/revoked). Poll to detect
// disconnect - session() returns { error } once revoked.
Medusa.prototype.isConnected = function (sessionId) {
    var s = this.session(sessionId);
    return !!(s && !s.error && s.active);
};

// ── writes ───────────────────────────────────────────────────────────────────
// Submit a transfer for approval. `op` is auto-derived from the from/to prefixes when omitted:
//   Public→Public = send · Public→Private = shield · Private→Public = deshield · Private→Private = private
// action = { from, to, amount, asset?: "native"|"token", definitionId?, op?, toNpk?, toVpk?, toIdentifier? }
// definitionId is REQUIRED whenever asset === "token" (send/shield/deshield alike).
// Returns { requestId } | { error }. Poll status(requestId) for the result (jobId once approved).
Medusa.prototype.send = function (sessionId, action) {
    action = action || {};
    if (!this.isWholeAmount(action.amount))
        return { error: "amounts are whole numbers - no decimals" };
    // Token send/shield/deshield REQUIRE the definition id (the wallet rejects them at
    // approval otherwise - fail here instead, before the user is even prompted). Private
    // transfers are exempt: the private note carries its own definition.
    var isPrivateOp = action.op === "private"
        || (!action.op && String(action.from || "").indexOf("Private/") === 0
            && (String(action.to || "").indexOf("Private/") === 0 || !action.to));
    if (action.asset === "token" && !action.definitionId && !isPrivateOp)
        return { error: "definitionId is required for token actions" };
    return this._invoke("requestAction", [sessionId, JSON.stringify(action)]);
};
// convenience aliases (explicit mode; all route through requestAction's op auto-detect)
Medusa.prototype.shield      = function (sid, a) { a = a || {}; a.op = "shield";   return this.send(sid, a); };
Medusa.prototype.deshield    = function (sid, a) { a = a || {}; a.op = "deshield"; return this.send(sid, a); };
Medusa.prototype.privateSend = function (sid, a) { a = a || {}; a.op = "private";  return this.send(sid, a); };

// ── zone (sequencer) ─────────────────────────────────────────────────────────
// Ask the wallet to switch to a sequencer/zone so the dApp and wallet share a chain
// (think wallet_addEthereumChain). Needs the "zone" permission; user-approved in the
// wallet via a zone-approval sheet. zone = { sequencer: "<https URL or .onion>",
// tor?: bool, label?: "<display name>" }. Returns { requestId } | { error }; poll
// status(requestId) -> "approved" { zoneId } | "rejected".
Medusa.prototype.requestZone = function (sessionId, zone) {
    zone = zone || {};
    if (!zone.sequencer) return { error: "zone.sequencer (a URL or .onion) is required" };
    return this._invoke("requestZone", [sessionId, JSON.stringify(zone)]);
};

// ── teardown ───────────────────────────────────────────────────────────────
Medusa.prototype.disconnect = function (sessionId) { return this._invoke("revokeSession", [sessionId]); };

// CommonJS export for the node test harness - `typeof module` is "undefined" in the QML JS
// engine, so this block is inert when imported into a Basecamp ui_qml module.
if (typeof module !== "undefined" && module.exports) module.exports = { create: create, Medusa: Medusa, _parse: _parse };
