# Medusa

A privacy wallet for [Logos](https://logos.co) **Basecamp** - and **Connect with Medusa**, the SDK that lets any Basecamp app request payments and private transfers through it.

Medusa is a Basecamp **module**: a self-custody wallet for a Logos execution zone (LEZ). It does **public** transfers, **private** transfers (shield · deshield · private), and tokens - and your keys never leave the wallet. **Connect with Medusa** is the dApp side: a tiny SDK so another Basecamp module can connect to the wallet and have the user approve a transfer - think "Connect Wallet" / WalletConnect, but for Basecamp.

> Status: **v0.2.0** (testnet, LEZ v0.2.0-rc5). Licensed **GPL-3.0** (see `LICENSE`).

---

## What's in here

| Path | What it is |
|------|------------|
| `module/` | The Basecamp module - C++ backend (`medusa_core`) + the QML wallet UI |
| `wallet/` | The wallet core (upstream zone @ `v0.2.0-rc5` + Medusa patches) + `build.sh` |
| `sdk/` | **Connect with Medusa** - the JS SDK (`@paradoxcomputer/medusa-connect`) |
| `examples/tip-jar/` | A small, runnable demo module that uses the SDK |

---

## Quickstart

**You'll need:** [Nix](https://nixos.org/download) (with flakes enabled), **Rust** (`cargo`), **Logos Basecamp** (the app that hosts modules), and **`tor`** on your `PATH` (the installer bundles it).

### 1 · Build the wallet core
```bash
bash wallet/build.sh
```
Clones the pinned upstream zone, applies the Medusa patches, and builds the `wallet` + sequencers. (First run takes a few minutes. Already have a checkout? `LEZ_SRC=<path> bash wallet/build.sh`.)

### 2 · Build & install the module
```bash
bash module/scripts/install-dev.sh --launch
```
Builds the `medusa_core` plugin via Nix, installs it + the QML UI into Basecamp, stages the wallet CLI + sequencer + Tor into `~/.local/bin`, and restarts Basecamp.

### 3 · Create your wallet
In Basecamp, open **Medusa** → **Create wallet** → set a password (it encrypts your keys on disk - and **write down the 24-word recovery phrase**). You start on the **`devnet`** zone: a self-contained local sequencer that needs no external network or accounts.

### 4 · Get funds & transact
In the wallet:
1. **New account** → a `Public/…` address.
2. **Claim faucet** → 150 LEZ (devnet).
3. **Send** (public → public), or **Shield** (public → private), **Deshield** (private → public), **Private** (private → private).

Private transfers generate a zero-knowledge proof **locally** - fast on `devnet` (dev-mode), minutes on a real-proof zone. That's the whole loop, fully local.

### 5 · Other zones (optional)
Besides `devnet`, Medusa has built-in **Paradox Computer** zones (thin clients of a shared testnet L1). No address is baked into the build - you supply the operator endpoint at runtime:
```bash
echo "<sequencer-host>.onion"  > ~/.config/medusa-sequencer.onion   # "Paradox Computer · Tor"
echo "https://<your-seq-host>" > ~/.config/medusa-clearnet.url       # "Paradox Computer · clearnet"
```
Or **add your own** zone (any LEZ sequencer - clearnet URL or a Tor `.onion`) from the wallet's zone settings.

---

## Connect with Medusa (build a dApp)

Let another Basecamp module connect to the wallet and request transfers. **Keys never leave the wallet, and every connect and every write is approved by the user in the wallet UI.** It rides Basecamp's `logos.callModule` bridge (not a network API), so your module and Medusa run side by side in Basecamp.

**Install:**
```bash
npm install @paradoxcomputer/medusa-connect
# make the SDK importable from your QML view (and the logo, for the branded button):
cp node_modules/@paradoxcomputer/medusa-connect/medusa-connect.js  qml/
cp node_modules/@paradoxcomputer/medusa-connect/medusa-logo.png    qml/
```

**Declare the dependency** in your module's `manifest.json`:
```json
{ "name": "my-app", "type": "ui_qml", "view": "qml/Main.qml", "dependencies": ["medusa_core"] }
```

**Use it** from your QML (approvals are async, so connect/writes return a `requestId` you poll):
```qml
import "medusa-connect.js" as MedusaConnect

property var medusa: MedusaConnect.create({
    appName: "My App",
    call: function (m, f, a) { return logos.callModule(m, f, a) }   // capture the bridge
})

// 1) Connect - the wallet pops an approval sheet
var r = medusa.connect(["accounts", "send"])            // -> { requestId }
//    poll medusa.status(r.requestId) from a Timer until "approved" -> { sessionId }
var s = medusa.session(sessionId)                       // -> { accounts, granted, zone }

// 2) Align chains (optional, needs the "zone" permission) - the wallet pops a zone sheet
//    and, if the user approves, switches its sequencer to YOUR chain (à la
//    wallet_addEthereumChain). The session is re-pinned to the new zone on approval.
var z = medusa.requestZone(sessionId, {
    sequencer: "https://seq.example.com", label: "My Zone"
})                                                      // -> { requestId } ; poll for { zoneId }

// 3) Pay - the wallet pops an action sheet
var a = medusa.send(sessionId, {
    from: s.accounts[0], to: "Public/…recipient…", amount: "5", asset: "native"
})                                                      // -> { requestId } ; poll for { jobId }
```

Permissions: `accounts · send · shield · deshield · private · zone` - request only what you need. Amounts are whole numbers (LEZ has no decimals). See **`examples/tip-jar/`** for a complete, runnable module.

> Until Basecamp gains a one-tap app hand-off, the user manually opens the Medusa wallet to approve, then returns - your module should show an "open Medusa to approve" prompt while polling (the Tip Jar does).

---

## How it's built (reproducibility)

- `wallet/build.sh` rebuilds the wallet from a **pinned** upstream clone + `wallet/patches-rc5/*.patch` - no machine-local checkout. It also builds the Tor forwarder (from the pinned Diaphani repo) used only by the optional Paradox · Tor zone.
- `module/scripts/install-dev.sh` builds the module via Nix and stages everything. Flags: `--qml-only` (re-copy just the UI, instant) · `--launch` (restart Basecamp).

## Known limitations (testnet)

- **Token shielding is issuer-only on LEZ v0.2.0-rc5** - a token shield must be sourced from a
  direct-owned holding (e.g. a token you minted). Tokens in regular (ATA) balances cannot be
  shielded, and non-issuers cannot obtain a direct holding on this protocol version (verified
  on-chain; needs an upstream fix). Native LEZ shielding is unaffected.
- **Private accounts are one-shot recipients** - a second private output to the same private
  account is rejected by the chain, so the wallet only offers fresh private accounts as
  shield/private destinations.
- **Real proofs are slow on CPU** - 20-40+ minutes per private transfer on a real-proof zone on
  a busy machine; `devnet` uses fast dev-mode proofs.
- **L1 settlement can lag** on an under-provisioned node - the sequencer accepts a transfer (L2) quickly, but L1 finalization depends on node capacity.
- **App hand-off is manual** - until Basecamp adds `logos.openApp`, dApp users switch to the Medusa wallet to approve, then back.

## License

[GPL-3.0](LICENSE).
