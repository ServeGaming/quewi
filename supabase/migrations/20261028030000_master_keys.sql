-- Master keys (Matthew, 2026-10-28: "make an option for a master key for
-- myself"). A master key activates on any number of computers and each
-- activation runs for 10 years instead of duration_days. It's still a signed,
-- per-computer license (works offline) and can be revoked like any other.

alter table public.licenses
    add column if not exists master boolean not null default false;

comment on column public.licenses.master is
    'Owner''s key: any number of computers, 10 years per activation.';

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

    if not lic.master then
        select count(*) into used from public.activations where license_id = p_license_id;
        if used >= lic.max_machines then
            return jsonb_build_object('result', 'full');
        end if;
    end if;

    if lic.master then
        ends := now() + interval '10 years';
    else
        ends := coalesce(lic.next_expires_at, now() + make_interval(days => lic.duration_days));
    end if;
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
