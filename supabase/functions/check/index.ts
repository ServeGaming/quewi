// POST /check {token}
//   200 {status:"ok"|"revoked"|"expired"|"invalid", exp, serverTime}
//       exp = the activation's expires_at in the database (unix seconds), which
//       may be later than the token's if the admin extended it — quewi then
//       calls /activate again with its stored key to get a fresh token.
//       exp is null when status is "invalid", or "revoked" because the
//       activation was deleted (key moved to another computer / deleted).
//   400 {status:"invalid", exp:null, serverTime, error:"bad request"}
//   500 {error:"server error", message, serverTime}
// Also sets the activation's last_seen_at.

import { handleCheck } from "../_shared/handlers.ts";
import { signingKeysFromEnv } from "../_shared/license.ts";
import { supabaseStore } from "../_shared/store.ts";

Deno.serve((req) => handleCheck(req, { store: supabaseStore, keys: signingKeysFromEnv }));
