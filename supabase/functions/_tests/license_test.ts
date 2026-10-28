// deno test supabase/functions/_tests/
import {
  assert,
  assertEquals,
  assertNotEquals,
} from "jsr:@std/assert@1";
import {
  base64urlDecode,
  base64urlEncode,
  derToPem,
  formatKey,
  importSigningKey,
  isValidKey,
  keyTail,
  type LicensePayload,
  normaliseKey,
  signToken,
  toHex,
  verifyToken,
} from "../_shared/license.ts";
import { throwawayPem } from "./keys.ts";


const payload: LicensePayload = {
  v: 1,
  lic: "11111111-1111-4111-8111-111111111111",
  act: "22222222-2222-4222-8222-222222222222",
  key: "QW-…-ABCD",
  name: "Little Theatre — Ünïcode",
  mh: "a".repeat(64),
  iat: 1761600000,
  exp: 1762809600,
};

Deno.test("base64url round-trips all byte values, no padding", () => {
  const bytes = new Uint8Array(256).map((_, i) => i);
  for (let n = 0; n <= 256; n += 1) {
    const enc = base64urlEncode(bytes.subarray(0, n));
    assert(!/[+/=]/.test(enc));
    assertEquals(base64urlDecode(enc), bytes.subarray(0, n));
  }
});

Deno.test("key normalisation", () => {
  assertEquals(normaliseKey("qw-abcd-efgh-jkmn-pqrs"), "QWABCDEFGHJKMNPQRS");
  assertEquals(normaliseKey("  QW ABCD EFGH JKMN PQRS \n"), "QWABCDEFGHJKMNPQRS");
  assertEquals(normaliseKey("ABCD-EFGH-JKMN-PQRS"), "QWABCDEFGHJKMNPQRS"); // no prefix
  assertEquals(normaliseKey("QW-0OIL-1234-5678-9ABC"), "QW0011123456789ABC"); // look-alikes
  assert(isValidKey(normaliseKey("qw-abcd-efgh-jkmn-pqrs")));
  assert(!isValidKey(normaliseKey("QW-ABCD-EFGH-JKMN-PQRU"))); // U not allowed
  assert(!isValidKey(normaliseKey("QW-ABCD")));
  assert(!isValidKey(normaliseKey("XX-ABCD-EFGH-JKMN-PQRS")));
  assertEquals(formatKey("QWABCDEFGHJKMNPQRS"), "QW-ABCD-EFGH-JKMN-PQRS");
  assertEquals(keyTail("QWABCDEFGHJKMNPQRS"), "QW-…-PQRS");
});

Deno.test("PEM import derives the matching public key", async () => {
  const { pem, publicRaw } = await throwawayPem();
  const keys = await importSigningKey(pem);
  assertEquals(keys.publicRaw.length, 32);
  assertEquals(toHex(keys.publicRaw), toHex(publicRaw));
});

Deno.test("token: format, signature over raw JSON bytes, verifies", async () => {
  const { pem, publicRaw } = await throwawayPem();
  const keys = await importSigningKey(pem);
  const token = await signToken(payload, keys.privateKey);

  const [a, b] = token.split(".");
  assert(/^[A-Za-z0-9_-]+$/.test(a) && /^[A-Za-z0-9_-]+$/.test(b));
  const jsonBytes = base64urlDecode(a);
  const sig = base64urlDecode(b);
  assertEquals(sig.length, 64);
  assertEquals(JSON.parse(new TextDecoder().decode(jsonBytes)), payload);

  // Independent verification with only the raw 32-byte public key, over the
  // decoded JSON bytes — exactly what quewi does with Monocypher.
  const pub = await crypto.subtle.importKey("raw", publicRaw, { name: "Ed25519" }, false, ["verify"]);
  assert(await crypto.subtle.verify({ name: "Ed25519" }, pub, sig, jsonBytes));
  // …and NOT over the base64url text.
  assert(!(await crypto.subtle.verify({ name: "Ed25519" }, pub, sig, new TextEncoder().encode(a))));

  assertEquals(await verifyToken(token, keys.publicKey), payload);
});

Deno.test("token: tampered payload, signature, or wrong key fails", async () => {
  const { pem } = await throwawayPem();
  const keys = await importSigningKey(pem);
  const token = await signToken(payload, keys.privateKey);
  const [, sig] = token.split(".");

  const evil = { ...payload, exp: payload.exp + 86400 * 365 };
  const tampered = base64urlEncode(new TextEncoder().encode(JSON.stringify(evil))) + "." + sig;
  assertEquals(await verifyToken(tampered, keys.publicKey), null);

  const sigBytes = base64urlDecode(sig);
  sigBytes[10] ^= 1;
  const badSig = token.split(".")[0] + "." + base64urlEncode(sigBytes);
  assertEquals(await verifyToken(badSig, keys.publicKey), null);

  const other = await importSigningKey((await throwawayPem()).pem);
  assertEquals(await verifyToken(token, other.publicKey), null);

  for (const junk of ["", ".", "abc", "a.b.c", "!!!.???", token + "x"]) {
    assertEquals(await verifyToken(junk, keys.publicKey), null, junk);
  }
  assertNotEquals(await verifyToken(token, keys.publicKey), null);
});
