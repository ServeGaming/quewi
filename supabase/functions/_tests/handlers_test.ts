// Endpoint behaviour against an in-memory Store that mirrors the SQL
// (quewi_claim_activation) semantics.
import { assert, assertEquals } from "jsr:@std/assert@1";
import {
  type ActivationRow,
  type ClaimResult,
  type Deps,
  handleActivate,
  handleCheck,
  type LicenseRow,
  type Store,
} from "../_shared/handlers.ts";
import { importSigningKey, signToken, verifyToken, type SigningKeys } from "../_shared/license.ts";
import { throwawayPem } from "./keys.ts";

const DAY = 86400_000;

interface Lic extends LicenseRow {
  duration_days: number;
  max_machines: number;
  next_expires_at: string | null;
}

class MemStore implements Store {
  licenses: Lic[] = [];
  activations: ActivationRow[] = [];
  constructor(public now: () => number) {}
  addLicense(key: string, extra: Partial<Lic> = {}): Lic {
    const l: Lic = {
      id: crypto.randomUUID(), key, label: "Little Theatre", revoked: false,
      duration_days: 14, max_machines: 1, next_expires_at: null, ...extra,
    };
    this.licenses.push(l);
    return l;
  }
  findLicenseByKey(key: string) {
    return Promise.resolve(this.licenses.find((l) => l.key === key) ?? null);
  }
  findLicenseById(id: string) {
    return Promise.resolve(this.licenses.find((l) => l.id === id) ?? null);
  }
  findActivation(licenseId: string, mh: string) {
    return Promise.resolve(
      this.activations.find((a) => a.license_id === licenseId && a.machine_hash === mh) ?? null,
    );
  }
  findActivationById(id: string) {
    return Promise.resolve(this.activations.find((a) => a.id === id) ?? null);
  }
  claimActivation(licenseId: string, mh: string, name: string | null, ver: string | null): Promise<ClaimResult> {
    const lic = this.licenses.find((l) => l.id === licenseId);
    if (!lic) return Promise.resolve({ result: "missing" });
    const existing = this.activations.find((a) => a.license_id === licenseId && a.machine_hash === mh);
    if (existing) return Promise.resolve({ result: "existing", activation: existing });
    if (this.activations.filter((a) => a.license_id === licenseId).length >= lic.max_machines) {
      return Promise.resolve({ result: "full" });
    }
    const ends = lic.next_expires_at
      ? Date.parse(lic.next_expires_at)
      : this.now() + lic.duration_days * DAY;
    if (ends <= this.now()) {
      return Promise.resolve({ result: "expired", expires_at: new Date(ends).toISOString() });
    }
    const act: ActivationRow = {
      id: crypto.randomUUID(), license_id: licenseId, machine_hash: mh, machine_name: name,
      app_version: ver, activated_at: new Date(this.now()).toISOString(),
      expires_at: new Date(ends).toISOString(), last_seen_at: new Date(this.now()).toISOString(),
      revoked: false,
    };
    this.activations.push(act);
    lic.next_expires_at = null;
    return Promise.resolve({ result: "created", activation: act });
  }
  touchActivation(id: string, fields: { machine_name?: string | null; app_version?: string | null }) {
    const a = this.activations.find((x) => x.id === id)!;
    a.last_seen_at = new Date(this.now()).toISOString();
    if (fields.machine_name !== undefined) a.machine_name = fields.machine_name;
    if (fields.app_version !== undefined) a.app_version = fields.app_version;
    return Promise.resolve();
  }
}

const KEY = "QWABCDEFGHJKMNPQRS";
const MH1 = "1".repeat(64);
const MH2 = "2".repeat(64);

async function setup() {
  let t = Date.UTC(2026, 9, 28, 12, 0, 0);
  const clock = { get: () => t, advance: (ms: number) => (t += ms) };
  const store = new MemStore(clock.get);
  const keys: SigningKeys = await importSigningKey((await throwawayPem()).pem);
  const deps: Deps = { store, keys: () => Promise.resolve(keys), now: clock.get };
  return { clock, store, keys, deps };
}

function post(body: unknown): Request {
  return new Request("http://x/fn", {
    method: "POST",
    body: typeof body === "string" ? body : JSON.stringify(body),
  });
}

async function activate(deps: Deps, key: string, mh: string, extra: Record<string, unknown> = {}) {
  const r = await handleActivate(post({ key, machineHash: mh, machineName: "FOH-PC", appVersion: "1.3.0", ...extra }), deps);
  return { status: r.status, body: await r.json() };
}
async function check(deps: Deps, token: unknown) {
  const r = await handleCheck(post({ token }), deps);
  return { status: r.status, body: await r.json() };
}

Deno.test("activate: happy path, token contents, 14 days from activation", async () => {
  const { store, keys, deps, clock } = await setup();
  const lic = store.addLicense(KEY);
  const r = await activate(deps, "qw-abcd-efgh-jkmn-pqrs", MH1);
  assertEquals(r.status, 200);
  const p = (await verifyToken(r.body.token, keys.publicKey))!;
  assert(p);
  const nowS = Math.floor(clock.get() / 1000);
  assertEquals(p.v, 1);
  assertEquals(p.lic, lic.id);
  assertEquals(p.act, store.activations[0].id);
  assertEquals(p.key, "QW-…-PQRS");
  assertEquals(p.name, "Little Theatre");
  assertEquals(p.mh, MH1);
  assertEquals(p.iat, nowS);
  assertEquals(p.exp, nowS + 14 * 86400);
  assertEquals(r.body.exp, p.exp);
  assertEquals(r.body.serverTime, nowS);
  assertEquals(Object.keys(p), ["v", "lic", "act", "key", "name", "mh", "iat", "exp"]);
});

Deno.test("activate: same machine again keeps the same exp; another machine is refused", async () => {
  const { store, keys, deps, clock } = await setup();
  store.addLicense(KEY);
  const first = await activate(deps, KEY, MH1);
  clock.advance(5 * DAY);
  const again = await activate(deps, KEY, MH1, { appVersion: "1.4.0" });
  assertEquals(again.status, 200);
  const p1 = (await verifyToken(first.body.token, keys.publicKey))!;
  const p2 = (await verifyToken(again.body.token, keys.publicKey))!;
  assertEquals(p2.exp, p1.exp);
  assertEquals(p2.act, p1.act);
  assertEquals(store.activations[0].app_version, "1.4.0");

  const other = await activate(deps, KEY, MH2);
  assertEquals(other.status, 409);
  assertEquals(other.body.error, "used on another computer");
  assert(other.body.message.length > 10);
});

Deno.test("activate: admin extension is picked up by re-activation", async () => {
  const { store, keys, deps } = await setup();
  store.addLicense(KEY);
  const first = await activate(deps, KEY, MH1);
  const later = new Date(Date.parse(store.activations[0].expires_at) + 7 * DAY).toISOString();
  store.activations[0].expires_at = later;
  const c = await check(deps, first.body.token);
  assertEquals(c.body.status, "ok");
  assertEquals(c.body.exp, Math.floor(Date.parse(later) / 1000));
  const again = await activate(deps, KEY, MH1);
  assertEquals((await verifyToken(again.body.token, keys.publicKey))!.exp, c.body.exp);
});

Deno.test("activate: errors", async () => {
  const { store, deps, clock } = await setup();
  const lic = store.addLicense(KEY);

  let r = await activate(deps, "QW-ZZZZ-ZZZZ-ZZZZ-ZZZZ", MH1);
  assertEquals([r.status, r.body.error], [404, "unknown key"]);
  r = await activate(deps, "not a key", MH1);
  assertEquals([r.status, r.body.error], [404, "unknown key"]);
  r = await activate(deps, KEY, "ABC");
  assertEquals(r.status, 400);
  r = await activate(deps, KEY, MH1.toUpperCase().replace(/1/g, "A"));
  assertEquals(r.status, 400); // uppercase hex rejected
  r = await activate(deps, "", MH1);
  assertEquals(r.status, 400);
  assertEquals((await handleActivate(post("{nope"), deps)).status, 400);
  assertEquals((await handleActivate(new Request("http://x", { method: "GET" }), deps)).status, 405);
  assertEquals((await handleActivate(new Request("http://x", { method: "OPTIONS" }), deps)).status, 200);

  lic.revoked = true;
  r = await activate(deps, KEY, MH1);
  assertEquals([r.status, r.body.error], [403, "revoked"]);
  lic.revoked = false;

  r = await activate(deps, KEY, MH1);
  assertEquals(r.status, 200);
  store.activations[0].revoked = true;
  r = await activate(deps, KEY, MH1);
  assertEquals([r.status, r.body.error], [403, "revoked"]);
  store.activations[0].revoked = false;

  clock.advance(15 * DAY);
  r = await activate(deps, KEY, MH1);
  assertEquals([r.status, r.body.error], [403, "expired"]);
  assertEquals(r.body.exp, Math.floor(Date.parse(store.activations[0].expires_at) / 1000));
  assert(/trial ended on/.test(r.body.message));
});

Deno.test("activate: move to new computer keeps the end date", async () => {
  const { store, keys, deps, clock } = await setup();
  const lic = store.addLicense(KEY);
  const first = await activate(deps, KEY, MH1);
  const exp1 = first.body.exp;
  clock.advance(3 * DAY);
  // What the panel does: remember the end date, delete the activation.
  lic.next_expires_at = store.activations[0].expires_at;
  store.activations = [];
  const moved = await activate(deps, KEY, MH2);
  assertEquals(moved.status, 200);
  assertEquals((await verifyToken(moved.body.token, keys.publicKey))!.exp, exp1);
  assertEquals(lic.next_expires_at, null);
  // Old machine's token now checks as revoked.
  assertEquals((await check(deps, first.body.token)).body.status, "revoked");

  // A move whose end date already passed → expired, nothing created.
  lic.next_expires_at = new Date(clock.get() - DAY).toISOString();
  store.activations = [];
  const late = await activate(deps, KEY, MH1);
  assertEquals([late.status, late.body.error], [403, "expired"]);
  assertEquals(store.activations.length, 0);
});

Deno.test("check: ok / revoked / expired / invalid, last_seen, serverTime", async () => {
  const { store, keys, deps, clock } = await setup();
  const lic = store.addLicense(KEY);
  const { body } = await activate(deps, KEY, MH1);
  const token = body.token;

  clock.advance(DAY);
  let c = await check(deps, token);
  assertEquals(c.status, 200);
  assertEquals(c.body, { status: "ok", exp: body.exp, serverTime: Math.floor(clock.get() / 1000) });
  assertEquals(store.activations[0].last_seen_at, new Date(clock.get()).toISOString());

  store.activations[0].revoked = true;
  assertEquals((await check(deps, token)).body.status, "revoked");
  store.activations[0].revoked = false;
  lic.revoked = true;
  assertEquals((await check(deps, token)).body.status, "revoked");
  lic.revoked = false;

  clock.advance(14 * DAY);
  c = await check(deps, token);
  assertEquals(c.body.status, "expired");
  assertEquals(c.body.exp, body.exp);

  for (const bad of ["", "abc.def", 42, null, token.slice(0, -2) + "AA"]) {
    c = await check(deps, bad);
    assertEquals(c.body.status, "invalid", String(bad));
    assertEquals(typeof c.body.serverTime, "number");
  }
  const r = await handleCheck(post("{bad json"), deps);
  assertEquals(r.status, 400);
  assertEquals(typeof (await r.json()).serverTime, "number");

  // A validly signed token whose machine hash doesn't match the activation.
  const p = (await verifyToken(token, keys.publicKey))!;
  const forged = await signToken({ ...p, mh: MH2 }, keys.privateKey);
  assertEquals((await check(deps, forged)).body.status, "invalid");
});

Deno.test("server error when the signing key is missing", async () => {
  const { store, deps } = await setup();
  store.addLicense(KEY);
  const broken: Deps = { ...deps, keys: () => Promise.reject(new Error("LICENSE_SIGNING_KEY is not set")) };
  const orig = console.error;
  console.error = () => {};
  try {
    const r = await activate(broken, KEY, MH1);
    assertEquals([r.status, r.body.error], [500, "server error"]);
    const c = await handleCheck(post({ token: "a.b" }), broken);
    assertEquals(c.status, 500);
  } finally {
    console.error = orig;
  }
});
