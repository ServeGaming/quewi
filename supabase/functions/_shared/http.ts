// CORS + JSON response helpers. quewi itself doesn't need CORS; it's here so a
// browser page (e.g. the admin panel, or a test page) can call the functions.

export const corsHeaders: Record<string, string> = {
  "Access-Control-Allow-Origin": "*",
  "Access-Control-Allow-Headers": "authorization, x-client-info, apikey, content-type",
  "Access-Control-Allow-Methods": "POST, OPTIONS",
};

export function json(body: unknown, status = 200): Response {
  return new Response(JSON.stringify(body), {
    status,
    headers: { ...corsHeaders, "Content-Type": "application/json; charset=utf-8" },
  });
}

/** Handles OPTIONS / wrong method / bad JSON. Returns a body or a Response to send. */
export async function readJsonBody(
  req: Request,
): Promise<Record<string, unknown> | Response> {
  if (req.method === "OPTIONS") return new Response("ok", { headers: corsHeaders });
  if (req.method !== "POST") {
    return json({ error: "method not allowed", message: "Use POST." }, 405);
  }
  try {
    const text = await req.text();
    if (text.length > 16384) {
      return json({ error: "bad request", message: "Request is too large." }, 400);
    }
    const body = JSON.parse(text);
    if (!body || typeof body !== "object" || Array.isArray(body)) throw new Error();
    return body as Record<string, unknown>;
  } catch {
    return json({ error: "bad request", message: "The request wasn't valid JSON." }, 400);
  }
}
