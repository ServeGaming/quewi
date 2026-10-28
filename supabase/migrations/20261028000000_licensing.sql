-- quewi licensing — see docs/dev/licensing.md
--
-- licenses     one row per trial key handed to a theatre
-- activations  one row per computer a key has been entered on
-- admin_emails who may use the admin panel (seeded here; not writable via the API)
--
-- RLS is on for all three. anon gets nothing. An authenticated user gets full
-- access to licenses/activations only if is_quewi_admin() — their confirmed
-- email is in admin_emails. The edge functions use the service role.

create extension if not exists pgcrypto;

-- ── tables ──────────────────────────────────────────────────────────────────

create table public.licenses (
    id              uuid primary key default gen_random_uuid(),
    -- normalised: uppercase, no dashes/spaces, e.g. QWABCD1234EFGH5678
    key             text not null unique
                    check (key ~ '^QW[0-9A-HJKMNP-TV-Z]{16}$'),
    -- formatted for people: QW-ABCD-1234-EFGH-5678
    display_key     text,
    label           text,
    contact         text,
    duration_days   int  not null default 14 check (duration_days between 1 and 365),
    max_machines    int  not null default 1  check (max_machines >= 1),
    notes           text,
    revoked         bool not null default false,
    revoked_at      timestamptz,
    created_at      timestamptz not null default now(),
    -- Set by the panel's "Move to new computer": the next activation ends on
    -- this date instead of now + duration_days (so moving never adds days).
    -- Cleared when that activation is made. Null = normal behaviour.
    next_expires_at timestamptz
);

comment on column public.licenses.next_expires_at is
    'If set, the next activation uses this as expires_at instead of now()+duration_days, then it is cleared. Set by "Move to new computer".';

create table public.activations (
    id            uuid primary key default gen_random_uuid(),
    license_id    uuid not null references public.licenses(id) on delete cascade,
    machine_hash  text not null check (machine_hash ~ '^[0-9a-f]{64}$'),
    machine_name  text,
    app_version   text,
    activated_at  timestamptz not null default now(),
    expires_at    timestamptz not null,
    last_seen_at  timestamptz,
    revoked       bool not null default false,
    unique (license_id, machine_hash)
);

create index activations_license_id_idx on public.activations(license_id);

create table public.admin_emails (
    email text primary key
);

insert into public.admin_emails(email) values ('matthewcooper2008@gmail.com');

-- ── admin check ─────────────────────────────────────────────────────────────
-- True when the caller's JWT email is in admin_emails AND that auth user has a
-- confirmed email. SECURITY DEFINER so it can read auth.users and admin_emails
-- (the latter is itself RLS-protected by this function).

create or replace function public.is_quewi_admin()
returns boolean
language sql
stable
security definer
set search_path = ''
as $$
    select exists (
        select 1
        from auth.users u
        join public.admin_emails a on lower(a.email) = lower(u.email)
        where u.id = auth.uid()
          and u.email_confirmed_at is not null
          and lower(u.email) = lower(coalesce(auth.jwt() ->> 'email', ''))
    );
$$;

revoke all on function public.is_quewi_admin() from public, anon;
grant execute on function public.is_quewi_admin() to authenticated, service_role;

-- ── RLS ─────────────────────────────────────────────────────────────────────

alter table public.licenses     enable row level security;
alter table public.activations  enable row level security;
alter table public.admin_emails enable row level security;

-- Belt and braces on top of RLS: anon has no table privileges at all, and
-- admin_emails is read-only through the API.
revoke all on public.licenses, public.activations, public.admin_emails from anon;
revoke insert, update, delete, truncate on public.admin_emails from authenticated;

create policy licenses_admin_all on public.licenses
    for all to authenticated
    using (public.is_quewi_admin())
    with check (public.is_quewi_admin());

create policy activations_admin_all on public.activations
    for all to authenticated
    using (public.is_quewi_admin())
    with check (public.is_quewi_admin());

create policy admin_emails_admin_read on public.admin_emails
    for select to authenticated
    using (public.is_quewi_admin());

-- The panel lists keys with their activations using a PostgREST embed:
--   from('licenses').select('*, activations(*)')
-- which goes through the RLS above, so no extra view is needed.

-- ── activation claim (service role only) ───────────────────────────────────
-- Atomically gives a machine an activation on a license. Locks the license
-- row so two computers activating at the same moment can't both get in.
-- Returns jsonb {result, activation}:
--   'existing' — this machine already had one (activation = that row)
--   'created'  — a new one was made (activation = the new row)
--   'full'     — max_machines already used by other computers
--   'expired'  — next_expires_at (from a move) is already in the past
--   'missing'  — license id not found

create or replace function public.quewi_claim_activation(
    p_license_id   uuid,
    p_machine_hash text,
    p_machine_name text,
    p_app_version  text
) returns jsonb
language plpgsql
security invoker
set search_path = ''
as $$
declare
    lic   public.licenses%rowtype;
    act   public.activations%rowtype;
    used  int;
    ends  timestamptz;
begin
    select * into lic from public.licenses where id = p_license_id for update;
    if not found then
        return jsonb_build_object('result', 'missing');
    end if;

    select * into act from public.activations
     where license_id = p_license_id and machine_hash = p_machine_hash;
    if found then
        return jsonb_build_object('result', 'existing', 'activation', to_jsonb(act));
    end if;

    select count(*) into used from public.activations where license_id = p_license_id;
    if used >= lic.max_machines then
        return jsonb_build_object('result', 'full');
    end if;

    ends := coalesce(lic.next_expires_at, now() + make_interval(days => lic.duration_days));
    if ends <= now() then
        return jsonb_build_object('result', 'expired', 'expires_at', ends);
    end if;

    insert into public.activations(license_id, machine_hash, machine_name, app_version,
                                   expires_at, last_seen_at)
    values (p_license_id, p_machine_hash, p_machine_name, p_app_version, ends, now())
    returning * into act;

    update public.licenses set next_expires_at = null where id = p_license_id;

    return jsonb_build_object('result', 'created', 'activation', to_jsonb(act));
end;
$$;

revoke all on function public.quewi_claim_activation(uuid, text, text, text)
    from public, anon, authenticated;
grant execute on function public.quewi_claim_activation(uuid, text, text, text)
    to service_role;
