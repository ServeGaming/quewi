// Generates the Ed25519 license-signing keypair.
//
//   deno run --allow-write=<path> supabase/tools/gen-signing-key.ts <path/to/private.pem>
//
// Writes the PKCS#8 PEM private key to <path> (refuses to overwrite) and
// prints ONLY the public key: 64 hex chars and a C++ array literal for quewi.
// The private key is never printed. Then, e.g.:
//
//   supabase secrets set LICENSE_SIGNING_KEY="$(cat <path>)"
//
// …and store/delete <path> safely. Rotating the key invalidates every issued
// license (quewi verifies with the embedded public key).

import { derToPem, toHex } from "../functions/_shared/license.ts";

const out = Deno.args[0];
if (!out || Deno.args.length !== 1) {
  console.error("usage: gen-signing-key.ts <path to write private PEM>");
  Deno.exit(2);
}

const pair = await crypto.subtle.generateKey({ name: "Ed25519" }, true, ["sign", "verify"]) as CryptoKeyPair;
const pkcs8 = new Uint8Array(await crypto.subtle.exportKey("pkcs8", pair.privateKey));
const pub = new Uint8Array(await crypto.subtle.exportKey("raw", pair.publicKey));

try {
  await Deno.writeTextFile(out, derToPem(pkcs8), { createNew: true, mode: 0o600 });
} catch (e) {
  console.error(`could not write ${out}: ${e instanceof Error ? e.message : e}`);
  Deno.exit(1);
}

const hex = toHex(pub);
const bytes = Array.from(pub, (b) => "0x" + b.toString(16).padStart(2, "0"));
const rows: string[] = [];
for (let i = 0; i < bytes.length; i += 8) rows.push("    " + bytes.slice(i, i + 8).join(", ") + ",");

console.log(hex);
console.log(
  "static constexpr std::array<std::uint8_t, 32> kLicensePublicKey = {\n" +
    rows.join("\n") + "\n};",
);
