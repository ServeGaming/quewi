// POST /activate {key, machineHash, machineName, appVersion}
//   200 {token, exp, serverTime}
//   400 {error:"bad request", message}
//   403 {error:"revoked", message} | {error:"expired", exp, message}
//   404 {error:"unknown key", message}
//   409 {error:"used on another computer", message}
//   500 {error:"server error", message}
// See docs/dev/licensing.md and supabase/README.md.

import { handleActivate } from "../_shared/handlers.ts";
import { json } from "../_shared/http.ts";
import { signingKeysFromEnv } from "../_shared/license.ts";
import { noteAttempt, supabaseStore } from "../_shared/store.ts";

// 20 tries an hour per address is plenty for a theatre typing a key; past
// that it's someone hammering the endpoint.
const PER_HOUR = 20;

async function who(req: Request): Promise<string> {
  const ip = (req.headers.get("x-forwarded-for") ?? "").split(",")[0].trim() || "unknown";
  // Keep a hash, not the address.
  const digest = await crypto.subtle.digest("SHA-256", new TextEncoder().encode("quewi-attempt:" + ip));
  return Array.from(new Uint8Array(digest), (b) => b.toString(16).padStart(2, "0")).join("");
}

Deno.serve(async (req) => {
  if (req.method === "POST") {
    try {
      if (await noteAttempt(await who(req)) > PER_HOUR) {
        return json({
          error: "too many attempts",
          message: "Too many tries from this network. Wait an hour and try again.",
        }, 429);
      }
    } catch (e) {
      // The limiter mustn't lock everyone out if it fails; log and go on.
      console.error("rate limit check failed", e);
    }
  }
  return handleActivate(req, { store: supabaseStore, keys: signingKeysFromEnv });
});
