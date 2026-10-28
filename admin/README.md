# quewi licence admin panel

`index.html` is the whole thing: open it in a browser (double-click works) or
host it anywhere static. Paste the project's **anon public** key into
`SUPABASE_ANON_KEY` at the top first (Supabase dashboard → Project Settings →
API). The anon key is public by design; everything is enforced by row-level
security — only a signed-in user whose confirmed email is in `admin_emails`
can read or change keys. Never put the service_role key in this file.

What it does: sign in / out; list and search keys with their computer,
activation, end date and last check-in; make keys (shown once big, with a
ready-to-send message); per key: +7 days / set end date, move to a new
computer (keeps the end date), edit label/contact/notes/days, revoke /
un-revoke, delete.
