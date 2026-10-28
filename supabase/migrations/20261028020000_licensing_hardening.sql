-- Hardening (2026-10-28, Matthew: "make sure the supabase is safe").
--
-- 1. Admin needs the authenticator step. The panel signs in with an emailed
--    one-time code and then a TOTP code; is_quewi_admin() now also requires
--    that second step (the session's assurance level aal2), so an email code
--    on its own — or a stolen magic-link session — reads and changes nothing.
-- 2. /activate is rate-limited per address, so nobody can hammer it (keys
--    are 80 random bits, so guessing one was already hopeless; this keeps the
--    function bill and the logs quiet too).

create or replace function public.is_quewi_admin()
returns boolean
language sql
stable
security definer
set search_path = ''
as $$
    select coalesce(auth.jwt() ->> 'aal', '') = 'aal2'
       and exists (
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

-- Activation attempts, by a hash of the caller's address (not the address
-- itself), kept for a day.
create table if not exists public.activation_attempts (
    id         bigint generated always as identity primary key,
    who        text not null,
    at         timestamptz not null default now()
);
create index if not exists activation_attempts_who_at on public.activation_attempts (who, at);
alter table public.activation_attempts enable row level security;
revoke all on public.activation_attempts from anon, authenticated;
grant select, insert, delete on public.activation_attempts to service_role;

-- Note an attempt and say how many this caller made in the last hour
-- (this one included). Service role only.
create or replace function public.quewi_note_attempt(p_who text)
returns integer
language plpgsql
security definer
set search_path = ''
as $$
declare
    n integer;
begin
    delete from public.activation_attempts where at < now() - interval '1 day';
    insert into public.activation_attempts (who) values (p_who);
    select count(*) into n from public.activation_attempts
     where who = p_who and at > now() - interval '1 hour';
    return n;
end;
$$;

revoke all on function public.quewi_note_attempt(text) from public, anon, authenticated;
grant execute on function public.quewi_note_attempt(text) to service_role;
