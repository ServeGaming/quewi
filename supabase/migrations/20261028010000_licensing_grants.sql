-- Explicit table privileges. Newer Supabase projects don't grant the API
-- roles access to new tables automatically, so the license functions (which
-- run as service_role) were refused ("server error" on /activate). Row
-- Level Security still decides which rows an admin sees; anon gets nothing.

grant usage on schema public to service_role, authenticated;

grant select, insert, update, delete on public.licenses, public.activations to service_role;
grant select on public.admin_emails to service_role;

-- The admin panel (a signed-in admin; RLS checks is_quewi_admin()).
grant select, insert, update, delete on public.licenses, public.activations to authenticated;
grant select on public.admin_emails to authenticated;

-- Nothing for anon, ever.
revoke all on public.licenses, public.activations, public.admin_emails from anon;
