# Medusa

A privacy wallet for [Logos](https://logos.co) **Basecamp** - and **Connect with Medusa**, the SDK that lets any Basecamp app request payments and private transfers through it.

Medusa is a Basecamp **module**: a self-custody wallet for a Logos execution zone (LEZ). It does **public** transfers, **private** transfers (shield · deshield · private), and tokens - and your keys never leave the wallet. **Connect with Medusa** is the dApp side: a tiny SDK so another Basecamp module can connect to the wallet and have the user approve a transfer - think "Connect Wallet" / WalletConnect, but for Basecamp.

> Status: **v0.4.0** (testnet, LEZ v0.2.2). Licensed **GPL-3.0** (see `LICENSE`).
>
> `medusa_core` and `medusa_ui` share an ABI: install them **as a pair**, never one alone. A wallet
> only talks to a zone running the **same engine generation** - program ids are a hash of the
> program binaries, so a v0.2.2 wallet cannot transact on a v0.2.0 or rc5 zone and says so instead
> of showing zeros. Read **Security posture** below before putting anything you care about in it.

---

## What's in here

| Path | What it is |
|------|------------|
| `module/` | The Basecamp module - C++ backend (`medusa_core`) + the QML wallet UI |
| `wallet/` | The wallet core (upstream zone @ `v0.2.4`, the v0.2.2 program generation, + Medusa patches) + `build.sh` |
| `sdk/` | **Connect with Medusa** - the JS SDK (`@paradoxcomputer/medusa-connect`) |
| `examples/tip-jar/` | A small, runnable demo module that uses the SDK |

---

## Install a release (no build tools)

Grab the assets for your platform from the [releases page](https://github.com/paradoxcomputer/medusa/releases)
and install them **as a pair**:

| Asset | What it is |
|-------|------------|
| `medusa_core-<platform>.lgx` | the wallet backend, with the LEZ engine, sequencer and Tor bundled inside |
| `medusa_ui-<platform>.lgx` | the wallet UI |
| `tip_jar-<platform>.lgx` | optional: the Connect demo dApp |

Install them from Basecamp (Apps → install a package), or unpack the
`medusa-wallet-<version>-<platform>.tar.gz` bundle and run its `install.sh`, which copies both
halves into the Basecamp data directory and needs no Nix, Rust or Tor. Basecamp has **no hot
reload**: close and reopen it after installing. Everything below is for building from source
instead.

**On macOS**, the darwin build is Apple Silicon only and is **ad-hoc signed, not notarized**: macOS
attaches a quarantine flag to anything downloaded, and Gatekeeper then refuses to load the plugin.
Clear it on the downloaded files before installing:

```bash
xattr -dr com.apple.quarantine medusa_core-darwin-arm64.lgx medusa_ui-darwin-arm64.lgx
```

macOS also has no bundled Tor (the wallet falls back to a system one, `brew install tor`), so the
Paradox · Tor zone needs that installed. Intel Macs are not built.

## Quickstart (build from source)

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
Besides `devnet`, Medusa ships three remote zones:

| Zone | Endpoint | Notes |
|------|----------|-------|
| **Paradox Computer** (clearnet) | `https://seq-testnet.paradox.computer/` | the default zone |
| **Paradox Computer** (Tor) | `paradoxj4xy6orxue7y7qsk4rxutzme6patcpo65liw22jlmlpxlncyd.onion` | the same sequencer, reached over Tor |
| **Logos public testnet** | `https://testnet.lez.logos.co/` | the official logos-co zone; third-party, can move |

All three addresses are compiled in, and each is overridable at runtime. An override is used
exactly as written, so point the wallet at your own sequencer with:
```bash
echo "<host>.onion"            > ~/.config/medusa-sequencer.onion   # "Paradox Computer · Tor"
echo "https://<your-seq-host>" > ~/.config/medusa-clearnet.url       # overrides the clearnet default
```
A bare `.onion` is reached on port 80; append `:<port>` if your hidden service publishes another.
The wallet shows each zone's address next to its name. Check it before sending anything large:
that display is the check that catches a wallet someone has repointed.
Or **add your own** zone (any LEZ sequencer - clearnet URL or a Tor `.onion`) from the wallet's zone
settings. A `.onion` address is routed over Tor; anything else connects directly. Adding, editing,
removing or switching a zone needs the wallet **unlocked**: see below.

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

- `wallet/build.sh` rebuilds the wallet from a **pinned** upstream clone (a 40-character commit SHA, not a movable tag) + `wallet/patches-v022/*.patch` - no machine-local checkout. It also builds the Tor forwarder (from the pinned Diaphani repo) used only by the optional Paradox · Tor zone.
- `module/scripts/install-dev.sh` builds the module via Nix and stages everything. Flags: `--qml-only` (re-copy just the UI, instant) · `--launch` (restart Basecamp).

## Security posture

Read this before trusting it with anything but testnet play-money.

- **Your password is the capability.** It is typed at unlock, held by the wallet UI, and presented
  on every operation that spends, exports a key, or changes which sequencer the wallet talks to.
  There is no per-transaction prompt, and no other module can produce that password.
- **Anything running as your user is inside the boundary.** Basecamp does not currently isolate
  modules from each other by capability, so the defence is the password, not the platform. A
  hostile program running as you cannot take your keys through this module, but it can still touch
  your files directly: it can rewrite `~/.config/medusa-clearnet.url`, and it can replace the
  binaries the wallet runs. **Check the endpoint shown next to the zone name before sending
  anything large.**
- **Changing zones requires an unlocked wallet.** Adding, editing, removing and switching zones all
  need the session password, because repointing the wallet at someone else's sequencer is what lets
  them see, censor and mis-report everything. On a fresh install (no wallet yet) they are free,
  which is what onboarding needs.
- **A legacy plaintext store is not protected and the wallet says so.** Its keys are readable by
  anything at your uid, so every gated verb refuses; **Security & Backup → set a password**
  migrates it in place, keeping your accounts.
- **The wallet auto-locks on idle**, and a running job can only postpone that a bounded number of
  times, so a job that never finishes cannot hold your session open.
- **Releases are signed, catalogs are not verified end to end.** Each `.lgx` carries an Ed25519
  `manifest.sig`, but Basecamp does not yet enforce a signature policy on the install path, and the
  GitHub release assets are unsigned. Verify what you install.

## Known limitations (testnet)

- **Private accounts are one-shot recipients** - a second private output to the same private
  account is rejected by the chain, so the wallet only offers fresh private accounts as
  shield/private destinations.
- **Real proofs are slow on CPU** - 20-40+ minutes per private transfer on a real-proof zone on
  a busy machine; `devnet` uses fast dev-mode proofs.
- **L1 settlement can lag** on an under-provisioned node - the sequencer accepts a transfer (L2) quickly, but L1 finalization depends on node capacity.
- **App hand-off is manual** - until Basecamp adds `logos.openApp`, dApp users switch to the Medusa wallet to approve, then back.

## License

[GPL-3.0](LICENSE).
