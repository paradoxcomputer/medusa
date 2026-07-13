// Type declarations for @paradoxcomputer/medusa-connect (v0.2.0, the shipped synchronous wrapper).
// Mirrors medusa-connect.js: a thin, QML-importable wrapper over Basecamp's `logos.callModule` bridge.
// Connect + writes are user-approved IN THE WALLET, so they are 2-phase: a *Request returns
// { requestId }, then you poll status(requestId) (drive it from a QML Timer) until it resolves.

export type Permission = "accounts" | "send" | "shield" | "deshield" | "private" | "zone";

export interface MedusaOptions {
  /** Human-readable app name shown in the wallet's approval sheet. */
  appName?: string;
  /** Optional `data:` URI icon shown in the approval sheet. */
  icon?: string;
  /** Optional origin string recorded with the connection. */
  origin?: string;
  /** Target wallet module id. Defaults to "medusa_core". */
  module?: string;
  /** The Basecamp bridge, typically `(m, f, a) => logos.callModule(m, f, a)`. Required. */
  call: (moduleId: string, method: string, args: unknown[]) => string;
}

export interface ConnectResult {
  requestId?: string;
  error?: string;
}

export interface ActionRequestResult {
  requestId?: string;
  error?: string;
}

export interface StatusResult {
  status?: "pending" | "approved" | "rejected";
  /** Present when an APPROVED connect request resolves. */
  sessionId?: string;
  /** Present when an APPROVED action request resolves. */
  jobId?: string;
  /** Present when an APPROVED zone request resolves, the active/added zone id. */
  zoneId?: string;
  /** Set when rejected, includes "approval timed out" if the request expired (TTL). */
  error?: string;
}

export interface ZoneRequest {
  /** The sequencer endpoint, an https URL or a Tor `.onion` address. */
  sequencer: string;
  /** True if `sequencer` is a Tor `.onion` (defaults to auto-detect from the address). */
  tor?: boolean;
  /** Optional display name for the zone in the wallet. */
  label?: string;
}

export interface SessionInfo {
  sessionId?: string;
  app?: unknown;
  /** Empty unless the "accounts" permission was granted. */
  accounts?: string[];
  granted?: Permission[];
  /** The LIVE active zone (may differ from the connect-time zone if the user switched). */
  zone?: string;
  /** The session's pinned zone: set at connect approval, re-pinned when the wallet
   *  approves this session's own requestZone. Actions are rejected while the live
   *  zone differs from it. */
  zoneAtConnect?: string;
  active?: boolean;
  error?: string;
}

export interface AccountsResult { accounts?: string[]; error?: string; }
export interface BalanceResult { balance?: number | string; error?: string; [k: string]: unknown; }
export interface TokenHolding {
  definitionId: string;
  ticker: string;
  /** ATA balance + vault balance combined (what the wallet can actually spend). */
  balance: string;
  /** Portion in the owner's associated token account, NOT shieldable on rc5. */
  ataBalance?: string;
  /** Portion in the wallet's direct vault holding, the shieldable part. */
  vaultBalance?: string;
}
export interface JobResult {
  jobId?: string;
  state?: "running" | "done" | "error";
  phase?: string;
  /** On-chain tx hash, present once state === "done". */
  txId?: string;
  error?: string;
  [k: string]: unknown;
}

export interface Action {
  from: string;
  to?: string;
  /** Whole numbers only, LEZ has no decimals. */
  amount: string;
  asset?: "native" | "token";
  /** REQUIRED when `asset === "token"` (send, shield and deshield alike), the wallet
   *  rejects token actions without it; the SDK pre-validates client-side. */
  definitionId?: string;
  /** Auto-derived from the from/to prefixes when omitted. */
  op?: "send" | "shield" | "deshield" | "private";
  /** Foreign private recipient (private → someone else's private account). */
  toNpk?: string;
  toVpk?: string;
  toIdentifier?: string;
}

export declare class Medusa {
  constructor(opts: MedusaOptions);
  appName: string;
  icon: string;
  origin: string;
  module: string;
  /** True if `a` is a whole, non-negative integer (LEZ has no decimals). */
  isWholeAmount(a: string | number): boolean;
  /** Submit a connect request; the wallet pops its approval sheet. Then poll status(requestId). */
  connect(perms?: Permission[]): ConnectResult;
  /** Poll a connect- or action-request: "pending" | "approved" (sessionId | jobId) | "rejected". */
  status(requestId: string): StatusResult;
  /** Session details once connected. */
  session(sessionId: string): SessionInfo;
  /** Granted account ids for the session (needs the "accounts" permission; else empty). */
  getAccounts(sessionId: string): AccountsResult;
  /** Public on-chain balance of an account. */
  getBalance(sessionId: string, accountId: string): BalanceResult;
  /** Token holdings of an account. */
  getTokens(sessionId: string, accountId: string): TokenHolding[] | { error: string };
  /** Resolve a jobId from an approved action; poll until state !== "running" for the txId. */
  getJob(sessionId: string, jobId: string): JobResult;
  /** True while the session is still live; poll to detect disconnect. */
  isConnected(sessionId: string): boolean;
  /** Submit a transfer for approval. Op auto-derived from the from/to prefixes when omitted. */
  send(sessionId: string, action: Action): ActionRequestResult;
  /** send() with op pinned to "shield" (public → private). Token asset: requires
   *  definitionId, and on LEZ v0.2.0-rc5 only DIRECT-owned holdings (e.g. a token the
   *  user minted, or the wallet's vault) can source it, ATA balances cannot shield. */
  shield(sessionId: string, action: Action): ActionRequestResult;
  /** send() with op pinned to "deshield" (private → public). Token asset: requires
   *  definitionId (routes the tokens into the recipient owner's ATA). */
  deshield(sessionId: string, action: Action): ActionRequestResult;
  /** send() with op pinned to "private" (private → private). */
  privateSend(sessionId: string, action: Action): ActionRequestResult;
  /** Ask the wallet to switch to a sequencer/zone (needs the "zone" permission; user-approved
   *  in the wallet). Poll status(requestId) -> "approved" { zoneId } | "rejected". */
  requestZone(sessionId: string, zone: ZoneRequest): ActionRequestResult;
  /** Revoke the session. */
  disconnect(sessionId: string): unknown;
}

/** Create a Medusa connector bound to the Basecamp bridge. */
export declare function create(opts: MedusaOptions): Medusa;
