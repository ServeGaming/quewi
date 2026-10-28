// Shared licensing helpers for the activate/check edge functions.
// No Supabase or network imports here, so it can be unit-tested offline.
//
// Token (docs/dev/licensing.md):
//   base64url_nopad(payload JSON UTF-8 bytes) + "." + base64url_nopad(Ed25519 signature)
// The signature is over the raw UTF-8 JSON bytes (the bytes that are
// base64url-encoded for the first part), NOT over the base64url text.

export interface LicensePayload {
  v: 1;
  lic: string; // license uuid
  act: string; // activation uuid
  key: string; // display tail only, e.g. "QW-…-ABCD"
  name: string; // license label (theatre name)
  mh: string; // machine hash, 64 lowercase hex
  iat: number; // unix seconds
  exp: number; // unix seconds
}

// ── base64url ───────────────────────────────────────────────────────────────

export function base64urlEncode(bytes: Uint8Array): string {
  let bin = "";
  for (let i = 0; i < bytes.length; i++) bin += String.fromCharCode(bytes[i]);
  return btoa(bin).replace(/\+/g, "-").replace(/\//g, "_").replace(/=+$/, "");
}

export function base64urlDecode(text: string): Uint8Array<ArrayBuffer> {
  if (!/^[A-Za-z0-9_-]*$/.test(text)) throw new Error("bad base64url");
  let b64 = text.replace(/-/g, "+").replace(/_/g, "/");
  while (b64.length % 4) b64 += "=";
  const bin = atob(b64);
  const out = new Uint8Array(bin.length);
  for (let i = 0; i < bin.length; i++) out[i] = bin.charCodeAt(i);
  return out;
}

export function toHex(bytes: Uint8Array): string {
  return Array.from(bytes, (b) => b.toString(16).padStart(2, "0")).join("");
}

// ── keys ────────────────────────────────────────────────────────────────────
// Keys look like QW-XXXX-XXXX-XXXX-XXXX, body in Crockford base32 (no I L O U).
// Normalising: uppercase, drop dashes/spaces/whitespace; a bare 16-char body
// gets the QW prefix; in the body the look-alikes O→0 and I/L→1 are mapped
// (they can't occur in a real key, so this only forgives typos).

const KEY_BODY_RE = /^[0-9A-HJKMNP-TV-Z]{16}$/;

export function normaliseKey(input: string): string {
  let k = String(input ?? "").toUpperCase().replace(/[\s\-_–—]/g, "");
  if (k.length === 16) k = "QW" + k;
  if (k.length !== 18 || !k.startsWith("QW")) return k;
  const body = k.slice(2).replace(/O/g, "0").replace(/[IL]/g, "1");
  return "QW" + body;
}

export function isValidKey(normalised: string): boolean {
  return normalised.length === 18 && normalised.startsWith("QW") &&
    KEY_BODY_RE.test(normalised.slice(2));
}

export function formatKey(normalised: string): string {
  const b = normalised.slice(2);
  return `QW-${b.slice(0, 4)}-${b.slice(4, 8)}-${b.slice(8, 12)}-${b.slice(12, 16)}`;
}

/** What goes in the token's "key" field: only the last 4 characters. */
export function keyTail(normalised: string): string {
  return `QW-…-${normalised.slice(-4)}`;
}

export const MACHINE_HASH_RE = /^[0-9a-f]{64}$/;

// ── Ed25519 ─────────────────────────────────────────────────────────────────

export function pemToDer(pem: string): Uint8Array<ArrayBuffer> {
  const body = pem
    .replace(/-----BEGIN [^-]+-----/, "")
    .replace(/-----END [^-]+-----/, "")
    .replace(/\s+/g, "");
  if (!body) throw new Error("empty PEM");
  const bin = atob(body);
  const out = new Uint8Array(bin.length);
  for (let i = 0; i < bin.length; i++) out[i] = bin.charCodeAt(i);
  return out;
}

export function derToPem(der: Uint8Array, label = "PRIVATE KEY"): string {
  let bin = "";
  for (let i = 0; i < der.length; i++) bin += String.fromCharCode(der[i]);
  const b64 = btoa(bin).match(/.{1,64}/g)!.join("\n");
  return `-----BEGIN ${label}-----\n${b64}\n-----END ${label}-----\n`;
}

export interface SigningKeys {
  privateKey: CryptoKey;
  publicKey: CryptoKey;
  publicRaw: Uint8Array<ArrayBuffer>; // 32 bytes — what quewi embeds
}

/** Imports a PKCS#8 PEM Ed25519 private key and derives its public key. */
export async function importSigningKey(pem: string): Promise<SigningKeys> {
  const der = pemToDer(pem);
  const privateKey = await crypto.subtle.importKey(
    "pkcs8",
    der,
    { name: "Ed25519" },
    true,
    ["sign"],
  );
  const jwk = await crypto.subtle.exportKey("jwk", privateKey);
  if (!jwk.x) throw new Error("could not derive public key");
  const publicRaw = base64urlDecode(jwk.x);
  const publicKey = await crypto.subtle.importKey(
    "raw",
    publicRaw,
    { name: "Ed25519" },
    true,
    ["verify"],
  );
  return { privateKey, publicKey, publicRaw };
}

let cachedKeys: Promise<SigningKeys> | null = null;

/** Keys from the LICENSE_SIGNING_KEY secret (cached per function instance). */
export function signingKeysFromEnv(): Promise<SigningKeys> {
  if (!cachedKeys) {
    const pem = Deno.env.get("LICENSE_SIGNING_KEY");
    if (!pem) return Promise.reject(new Error("LICENSE_SIGNING_KEY is not set"));
    cachedKeys = importSigningKey(pem);
    cachedKeys.catch(() => (cachedKeys = null));
  }
  return cachedKeys;
}

export async function signToken(
  payload: LicensePayload,
  privateKey: CryptoKey,
): Promise<string> {
  const json = new TextEncoder().encode(JSON.stringify(payload));
  const sig = new Uint8Array(
    await crypto.subtle.sign({ name: "Ed25519" }, privateKey, json),
  );
  return base64urlEncode(json) + "." + base64urlEncode(sig);
}

/** Returns the payload if the token's signature is good, else null. */
export async function verifyToken(
  token: string,
  publicKey: CryptoKey,
): Promise<LicensePayload | null> {
  if (typeof token !== "string" || token.length > 4096) return null;
  const parts = token.split(".");
  if (parts.length !== 2) return null;
  let json: Uint8Array<ArrayBuffer>, sig: Uint8Array<ArrayBuffer>;
  try {
    json = base64urlDecode(parts[0]);
    sig = base64urlDecode(parts[1]);
  } catch {
    return null;
  }
  if (sig.length !== 64) return null;
  const ok = await crypto.subtle.verify({ name: "Ed25519" }, publicKey, sig, json);
  if (!ok) return null;
  try {
    const p = JSON.parse(new TextDecoder("utf-8", { fatal: true }).decode(json));
    if (
      p && p.v === 1 && typeof p.lic === "string" && typeof p.act === "string" &&
      typeof p.mh === "string" && Number.isInteger(p.iat) && Number.isInteger(p.exp)
    ) return p as LicensePayload;
  } catch {
    // fall through
  }
  return null;
}

export function unixSeconds(d: Date | string | number = Date.now()): number {
  const ms = d instanceof Date ? d.getTime() : typeof d === "string" ? Date.parse(d) : d;
  return Math.floor(ms / 1000);
}
