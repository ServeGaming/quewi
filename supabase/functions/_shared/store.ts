// Supabase-backed Store for the handlers, using the service role (bypasses
// RLS). SUPABASE_URL and SUPABASE_SERVICE_ROLE_KEY are provided by Supabase.

import { createClient, type SupabaseClient } from "npm:@supabase/supabase-js@2";
import type { ActivationRow, ClaimResult, LicenseRow, Store } from "./handlers.ts";

const LICENSE_COLS = "id, key, label, revoked";

let client: SupabaseClient | null = null;
function db(): SupabaseClient {
  if (!client) {
    const url = Deno.env.get("SUPABASE_URL");
    const key = Deno.env.get("SUPABASE_SERVICE_ROLE_KEY");
    if (!url || !key) throw new Error("SUPABASE_URL / SUPABASE_SERVICE_ROLE_KEY not set");
    client = createClient(url, key, {
      auth: { persistSession: false, autoRefreshToken: false },
    });
  }
  return client;
}

function check<T>(res: { data: T; error: unknown }): T {
  if (res.error) throw res.error;
  return res.data;
}

export const supabaseStore: Store = {
  async findLicenseByKey(key) {
    return check(
      await db().from("licenses").select(LICENSE_COLS).eq("key", key).maybeSingle(),
    ) as LicenseRow | null;
  },
  async findLicenseById(id) {
    return check(
      await db().from("licenses").select(LICENSE_COLS).eq("id", id).maybeSingle(),
    ) as LicenseRow | null;
  },
  async findActivation(licenseId, machineHash) {
    return check(
      await db().from("activations").select("*")
        .eq("license_id", licenseId).eq("machine_hash", machineHash).maybeSingle(),
    ) as ActivationRow | null;
  },
  async findActivationById(id) {
    return check(
      await db().from("activations").select("*").eq("id", id).maybeSingle(),
    ) as ActivationRow | null;
  },
  async claimActivation(licenseId, machineHash, machineName, appVersion) {
    return check(
      await db().rpc("quewi_claim_activation", {
        p_license_id: licenseId,
        p_machine_hash: machineHash,
        p_machine_name: machineName,
        p_app_version: appVersion,
      }),
    ) as ClaimResult;
  },
  async touchActivation(id, fields) {
    const update: Record<string, unknown> = { last_seen_at: new Date().toISOString() };
    if (fields.machine_name !== undefined) update.machine_name = fields.machine_name;
    if (fields.app_version !== undefined) update.app_version = fields.app_version;
    check(await db().from("activations").update(update).eq("id", id));
  },
};
