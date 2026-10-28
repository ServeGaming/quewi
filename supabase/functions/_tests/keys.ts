// Throwaway Ed25519 keys for tests.
import { derToPem } from "../_shared/license.ts";

export async function throwawayPem(): Promise<{ pem: string; publicRaw: Uint8Array<ArrayBuffer> }> {
  const pair = await crypto.subtle.generateKey({ name: "Ed25519" }, true, [
    "sign",
    "verify",
  ]) as CryptoKeyPair;
  const der = new Uint8Array(await crypto.subtle.exportKey("pkcs8", pair.privateKey));
  const publicRaw = new Uint8Array(await crypto.subtle.exportKey("raw", pair.publicKey));
  return { pem: derToPem(der), publicRaw };
}
