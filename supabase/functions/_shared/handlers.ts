// The activate / check logic, written against a small Store interface so it
// can be tested without a database. index.ts files plug in the Supabase store.

import { json, readJsonBody } from "./http.ts";
import {
  isValidKey,
  keyTail,
  type LicensePayload,
  MACHINE_HASH_RE,
  normaliseKey,
  signToken,
  type SigningKeys,
  unixSeconds,
  verifyToken,
} from "./license.ts";

export interface LicenseRow {
  id: string;
  key: string;
  label: string | null;
  revoked: boolean;
}

export interface ActivationRow {
  id: string;
  license_id: string;
  machine_hash: string;
  machine_name: string | null;
  app_version: string | null;
  activated_at: string;
  expires_at: string;
  last_seen_at: string | null;
  revoked: boolean;
}

export type ClaimResult =
  | { result: "existing" | "created"; activation: ActivationRow }
  | { result: "full" | "missing" }
  | { result: "expired"; expires_at: string };

export interface Store {
  findLicenseByKey(key: string): Promise<LicenseRow | null>;
  findLicenseById(id: string): Promise<LicenseRow | null>;
  findActivation(licenseId: string, machineHash: string): Promise<ActivationRow | null>;
  findActivationById(id: string): Promise<ActivationRow | null>;
  claimActivation(
    licenseId: string,
    machineHash: string,
    machineName: string | null,
    appVersion: string | null,
  ): Promise<ClaimResult>;
  /** Sets last_seen_at = now, plus machine_name/app_version when given. */
  touchActivation(
    id: string,
    fields: { machine_name?: string | null; app_version?: string | null },
  ): Promise<void>;
}

export interface Deps {
  store: Store;
  keys: () => Promise<SigningKeys>;
  now?: () => number; // ms
}

const UUID_RE = /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/i;

function dateText(unix: number): string {
  // e.g. "Tue, 10 Nov 2026" — unambiguous for any locale.
  return new Date(unix * 1000).toUTCString().slice(0, 16);
}

function optText(v: unknown, max: number): string | null | undefined {
  if (v === undefined || v === null) return null;
  if (typeof v !== "string") return undefined; // invalid
  return v.trim().slice(0, max) || null;
}

const MSG = {
  unknown:
    "That key wasn't recognised. Check it's typed exactly as it was sent to you " +
    "(it looks like QW-XXXX-XXXX-XXXX-XXXX).",
  revoked:
    "This key has been switched off. Please contact whoever gave you the key.",
  expired: (exp: number) =>
    `This key's trial ended on ${dateText(exp)}. Please contact whoever gave you ` +
    "the key if you need more time.",
  inUse:
    "This key is already in use on another computer. Each key works on one " +
    "computer — ask whoever gave you the key to move it to this computer, or " +
    "for a new key.",
  server:
    "The licence server had a problem. Please try again in a few minutes.",
};

// ── activate ────────────────────────────────────────────────────────────────

export async function handleActivate(req: Request, deps: Deps): Promise<Response> {
  const now = deps.now ?? Date.now;
  const body = await readJsonBody(req);
  if (body instanceof Response) return body;

  const rawKey = body.key;
  const machineHash = body.machineHash;
  const machineName = optText(body.machineName, 200);
  const appVersion = optText(body.appVersion, 50);

  if (typeof rawKey !== "string" || rawKey.trim() === "") {
    return json({ error: "bad request", message: "No key was entered." }, 400);
  }
  if (typeof machineHash !== "string" || !MACHINE_HASH_RE.test(machineHash)) {
    return json({
      error: "bad request",
      message: "machineHash must be 64 lowercase hex characters.",
    }, 400);
  }
  if (machineName === undefined || appVersion === undefined) {
    return json({
      error: "bad request",
      message: "machineName and appVersion must be text.",
    }, 400);
  }

  const key = normaliseKey(rawKey);
  if (!isValidKey(key)) return json({ error: "unknown key", message: MSG.unknown }, 404);

  try {
    const lic = await deps.store.findLicenseByKey(key);
    if (!lic) return json({ error: "unknown key", message: MSG.unknown }, 404);
    if (lic.revoked) return json({ error: "revoked", message: MSG.revoked }, 403);

    let act = await deps.store.findActivation(lic.id, machineHash);
    let isNew = false;
    if (!act) {
      const claim = await deps.store.claimActivation(lic.id, machineHash, machineName, appVersion);
      switch (claim.result) {
        case "missing":
          return json({ error: "unknown key", message: MSG.unknown }, 404);
        case "full":
          return json({ error: "used on another computer", message: MSG.inUse }, 409);
        case "expired": {
          const exp = unixSeconds(claim.expires_at);
          return json({ error: "expired", exp, message: MSG.expired(exp) }, 403);
        }
        case "existing": // raced with another request from this same machine
          act = claim.activation;
          break;
        case "created":
          act = claim.activation;
          isNew = true;
          break;
      }
    }
    if (!act) return json({ error: "server error", message: MSG.server }, 500);

    // Same machine again (reinstall, settings wiped, or the admin extended the
    // end date): re-sign with the CURRENT expires_at from the database.
    if (act.revoked) return json({ error: "revoked", message: MSG.revoked }, 403);
    const exp = unixSeconds(act.expires_at);
    const nowS = unixSeconds(now());
    if (exp <= nowS) {
      return json({ error: "expired", exp, message: MSG.expired(exp) }, 403);
    }
    if (!isNew) {
      await deps.store.touchActivation(act.id, {
        machine_name: machineName ?? act.machine_name,
        app_version: appVersion ?? act.app_version,
      });
    }

    const payload: LicensePayload = {
      v: 1,
      lic: lic.id,
      act: act.id,
      key: keyTail(key),
      name: lic.label ?? "",
      mh: machineHash,
      iat: nowS,
      exp,
    };
    const { privateKey } = await deps.keys();
    const token = await signToken(payload, privateKey);
    return json({ token, exp, serverTime: nowS });
  } catch (e) {
    console.error("activate failed:", e);
    return json({ error: "server error", message: MSG.server }, 500);
  }
}

// ── check ───────────────────────────────────────────────────────────────────

export async function handleCheck(req: Request, deps: Deps): Promise<Response> {
  const now = deps.now ?? Date.now;
  const serverTime = () => unixSeconds(now());
  const body = await readJsonBody(req);
  if (body instanceof Response) {
    if (body.status !== 400) return body; // OPTIONS / 405
    return json({ status: "invalid", exp: null, serverTime: serverTime(), error: "bad request" }, 400);
  }
  const invalid = () => json({ status: "invalid", exp: null, serverTime: serverTime() });

  try {
    const { publicKey } = await deps.keys();
    const p = await verifyToken(typeof body.token === "string" ? body.token : "", publicKey);
    if (!p || !UUID_RE.test(p.act) || !UUID_RE.test(p.lic)) return invalid();

    const act = await deps.store.findActivationById(p.act);
    // Deleted (e.g. "Move to new computer" / key deleted) → this machine is off.
    if (!act) return json({ status: "revoked", exp: null, serverTime: serverTime() });
    if (act.license_id !== p.lic || act.machine_hash !== p.mh) return invalid();

    await deps.store.touchActivation(act.id, {});
    const lic = await deps.store.findLicenseById(act.license_id);
    const exp = unixSeconds(act.expires_at);
    let status: "ok" | "revoked" | "expired";
    if (!lic || lic.revoked || act.revoked) status = "revoked";
    else if (exp <= serverTime()) status = "expired";
    else status = "ok";
    return json({ status, exp, serverTime: serverTime() });
  } catch (e) {
    console.error("check failed:", e);
    return json({ error: "server error", message: MSG.server, serverTime: serverTime() }, 500);
  }
}
