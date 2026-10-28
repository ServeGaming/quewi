// POST /activate {key, machineHash, machineName, appVersion}
//   200 {token, exp, serverTime}
//   400 {error:"bad request", message}
//   403 {error:"revoked", message} | {error:"expired", exp, message}
//   404 {error:"unknown key", message}
//   409 {error:"used on another computer", message}
//   500 {error:"server error", message}
// See docs/dev/licensing.md and supabase/README.md.

import { handleActivate } from "../_shared/handlers.ts";
import { signingKeysFromEnv } from "../_shared/license.ts";
import { supabaseStore } from "../_shared/store.ts";

Deno.serve((req) => handleActivate(req, { store: supabaseStore, keys: signingKeysFromEnv }));
