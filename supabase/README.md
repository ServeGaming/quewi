# quewi licensing server (Supabase project "Quewi", `mdvoeinmfjcyedvuffwq`)

Design: `docs/dev/licensing.md`. Use the Supabase **CLI logged into the quewi
account** — never the Pawn Tiger account / MCP connector.

```
config.toml                         project_id + verify_jwt=false for both functions
migrations/20261028000000_licensing.sql   tables, RLS, is_quewi_admin(), quewi_claim_activation()
functions/_shared/license.ts        token format, Ed25519 sign/verify, base64url, key normalisation
functions/_shared/handlers.ts       activate/check logic (against a Store interface)
functions/_shared/store.ts          Supabase service-role Store
functions/_shared/http.ts           CORS + JSON helpers
functions/activate, functions/check edge functions
functions/_tests/                   deno tests (offline; throwaway keys)
tools/gen-signing-key.ts            makes the signing keypair
```

## Endpoints

`POST /functions/v1/activate` `{key, machineHash, machineName?, appVersion?}`

| status | body |
|---|---|
| 200 | `{token, exp, serverTime}` |
| 400 | `{error:"bad request", message}` — missing key, machineHash not 64 lowercase hex, bad JSON |
| 403 | `{error:"revoked", message}` · `{error:"expired", exp, message}` |
| 404 | `{error:"unknown key", message}` |
| 409 | `{error:"used on another computer", message}` |
| 500 | `{error:"server error", message}` |

Same machine again → a fresh token with the activation's **current**
`expires_at` from the database (so admin extensions are picked up by
re-activating). `message` is plain English for showing to the theatre.

`POST /functions/v1/check` `{token}` → 200 `{status, exp, serverTime}` where
status is `ok | revoked | expired | invalid`; `exp` is the database
`expires_at` (unix s), `null` when invalid or when the activation was deleted
(moved / key deleted → `revoked`). Bad JSON → 400 `{status:"invalid", exp:null,
serverTime, error}`. Updates `last_seen_at`.

## Deploy (lead / Matthew)

```sh
deno run --allow-write=./license-signing.pem supabase/tools/gen-signing-key.ts ./license-signing.pem
#   prints the public key (hex + C++ array) for quewi; never prints the private key
supabase link --project-ref mdvoeinmfjcyedvuffwq
supabase db push
supabase secrets set LICENSE_SIGNING_KEY="$(cat ./license-signing.pem)"
supabase functions deploy activate
supabase functions deploy check
# then store license-signing.pem somewhere safe and delete it from disk
```

Dashboard → Authentication: create Matthew's user (matthewcooper2008@gmail.com)
first, confirm the email, then **turn off "Allow new users to sign up"**.

## Tests

```sh
cd supabase/functions && deno test _tests/
deno check --node-modules-dir=none activate/index.ts check/index.ts   # type-check incl. supabase-js
```

(`--node-modules-dir=none` is needed on Matthew's PC because a stray
`C:\Users\matth\package.json` makes Deno look for a node_modules folder.)
