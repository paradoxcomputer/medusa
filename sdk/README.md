# Connect with Medusa

dApp SDK for the **Medusa** privacy wallet on [Logos](https://logos.co) Basecamp. Your Basecamp
module asks the wallet to connect, pay, shield/deshield, transfer privately, or switch chains,
**keys never leave the wallet, and the user approves every connect and every write in the wallet
UI**. The SDK rides Basecamp's `logos.callModule` bridge (no network API): synchronous calls,
poll-based approvals.

## Install

```bash
npm install @paradoxcomputer/medusa-connect
# make the SDK importable from your QML view (and the logo, for the branded button):
cp node_modules/@paradoxcomputer/medusa-connect/medusa-connect.js  qml/
cp node_modules/@paradoxcomputer/medusa-connect/medusa-logo.png    qml/
```

Declare the wallet dependency in your module's `manifest.json`:

```json
{ "name": "my-app", "type": "ui_qml", "view": "qml/Main.qml", "dependencies": ["medusa_core"] }
```

## Use

```qml
import "medusa-connect.js" as MedusaConnect

property var medusa: MedusaConnect.create({
    appName: "My App",
    call: function (m, f, a) { return logos.callModule(m, f, a) }   // capture the bridge
})

// 1) Connect: the wallet pops an approval sheet
var r = medusa.connect(["accounts", "send"])            // -> { requestId }
//    poll medusa.status(r.requestId) from a Timer until "approved" -> { sessionId }
var s = medusa.session(sessionId)                       // -> { accounts, granted, zone }

// 2) Align chains (optional, "zone" permission), the wallet switches its sequencer to
//    your chain on user approval, and re-pins the session to it
var z = medusa.requestZone(sessionId, { sequencer: "https://seq.example.com", label: "My Zone" })
//    poll for "approved" -> { zoneId }

// 3) Pay: the wallet pops an action sheet
var a = medusa.send(sessionId, {
    from: s.accounts[0], to: "Public/…recipient…", amount: "5", asset: "native"
})                                                      // -> { requestId } ; poll for { jobId }

// 4) SETTLE. "approved" means the user approved it, NOT that it happened: the transfer is
//    still only a job inside the wallet at that point, and it can still fail. Poll the JOB
//    until it leaves "running", and treat ONLY state === "done" as sent.
//    Stopping at "approved" is the single most common integration bug with this SDK.
var j = medusa.getJob(sessionId, jobId)
if (j.error)                 { /* the job id is unknown to the wallet */ }
else if (j.state === "running") { /* keep polling, from a QML Timer */ }
else if (j.state === "done")    { /* SENT. j.txId is the on-chain hash */ }
else                            { /* failed: j.error says why, j.reason may carry a code */ }
```

Proofs are generated locally, so a shield, deshield or private transfer can sit in `running`
for many minutes on CPU. Keep polling, and never report success on a timeout: a job you stopped
watching is a job that may still land.

## API

| Method | Purpose |
| --- | --- |
| `create(opts)` | Make a client. `opts = { appName, icon?, origin?, call }`. |
| `connect(perms)` | Request a session. Returns `{ requestId }`; poll `status()`. |
| `status(requestId)` | Poll any request: `pending` / `approved` (+`sessionId` \| `jobId` \| `zoneId`) / `rejected` (+`error`). |
| `session(sessionId)` | Session info: `accounts` (if granted), `granted`, live `zone`, `zoneAtConnect`. |
| `getAccounts(sessionId)` | Just the granted account ids. The one read that is really session-scoped. |
| `getBalance(sessionId, accountId)` | Returns `{ ok, output }`, **not** `{ balance }`: `output` is the CLI's raw `account get` text, whose account JSON line carries `balance`. |
| `getTokens(sessionId, accountId)` | Token holdings: `[{ definitionId, ticker, balance, ataBalance, vaultBalance }]`. |
| `send(sessionId, action)` | Request a transfer. `action = { from, to, amount, asset?, definitionId?, op?, toNpk?, toVpk?, toIdentifier? }`. `definitionId` is **required** when `asset:"token"` (SDK pre-validates). |
| `shield` / `deshield` / `privateSend` | Aliases that set `op` for you. |
| `requestZone(sessionId, zone)` | Ask the wallet to switch sequencer/zone. `zone = { sequencer, tor?, label? }`. |
| `getJob(sessionId, jobId)` | Track an approved action's job. **Poll until `state !== "running"`.** Only `state:"done"` means it settled (`txId` is the hash); any other terminal state is a failure carrying `error`. An approval alone has moved nothing. |
| `disconnect(sessionId)` | Revoke the session. |

**Permissions:** `accounts` · `send` · `shield` · `deshield` · `private` · `zone`, request only
what you need. Amounts are whole numbers (LEZ has no decimals). Every call returns a plain object;
errors come back as `{ error }`, nothing throws.

**What `sessionId` does and does not do.** It is load-bearing on `session`, `getAccounts`,
`send`/`shield`/`deshield`/`privateSend`, `requestZone` and `disconnect`: the wallet checks the
session exists, checks the permission, checks the account is one the user exposed, and pins the
zone. It is **not** load-bearing on `getBalance`, `getTokens` and `getJob`: the SDK accepts the
argument and discards it, and the wallet-side verbs take no session, so they answer any module in
the Basecamp regardless of whether it ever connected. Treat those three as public reads, not as
proof a session is live (use `isConnected(sessionId)` for that), and do not assume the `accounts`
grant limits what another module can look up. Closing that gap needs a wallet-side change, not an
SDK change.

Pending approvals expire after **15 minutes**; `status(requestId)` then returns
`{ status: "rejected", error: "approval timed out" }`. Poll for at least that long before
giving up.

Actions are pinned to the session's zone: if the wallet's active zone changed since connect, the
wallet rejects the action ("reconnect"), unless the change came from *your* approved
`requestZone`, which re-pins the session.

**Token privacy on LEZ v0.2.0 (protocol limits, verified on-chain):** a token *shield* can
only be sourced from a **direct-owned holding** (e.g. a token the user minted, or the wallet's
vault), balances in regular associated token accounts (ATAs) cannot shield; `getTokens` splits
the two as `ataBalance`/`vaultBalance`. Private accounts are **one-shot recipients**: a second
private output to the same private account is rejected on-chain, always shield/transfer to a
fresh private account.

See `examples/tip-jar/` in the Medusa repo for a complete, runnable module (branded connect
button, connect → align zone → tip).

## License

GPL-3.0-only. © Paradox Computer.
