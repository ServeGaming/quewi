#!/usr/bin/env python3
"""Read a quewi show file (.quewi) and say what's in it.

  inspect_show.py SHOW.quewi [--json] [--triggers [--all]]

Lists every cue list and cue (number, type, name, file, continue mode) and,
with --triggers, every lighting trigger on each audio/video cue (time, name,
group, what it tells the desk). Checks the file is one quewi can open:
schema version, every cue in a list that exists, every payload valid JSON,
audio files present. Read-only.
"""
import argparse
import json
import os
import sqlite3
import sys

KNOWN_SCHEMA = 1
CONT = {0: "", 1: "auto-continue", 2: "auto-follow"}


def mmss(s):
    return f"{int(s // 60)}:{s % 60:05.2f}"


def desk_text(a):
    if not a or a.get("kind") in (None, "none"):
        return "—"
    if a["kind"] != "desk":
        return a["kind"].upper() + (" " + a.get("address", "") if a["kind"] == "osc" else "")
    d = a.get("do", "go")
    return {"go": "GO", "stop": "Stop", "back": "Back",
            "cue": f"Go to cue {a.get('list', 1)}/{a.get('number')}",
            "subLevel": f"Sub {a.get('number')} @ {a.get('level')}%",
            "subBump": f"Bump sub {a.get('number')}",
            "faderLevel": f"Fader {a.get('list')}/{a.get('number')} @ {a.get('level')}%",
            "faderBump": f"Bump fader {a.get('list')}/{a.get('number')}",
            "macro": f"Macro {a.get('number')}", "command": f"Cmd: {a.get('text')}",
            "goList": f"GO list {a.get('list')}"}.get(d, d)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("show")
    ap.add_argument("--triggers", action="store_true", help="list every lighting trigger")
    ap.add_argument("--json", action="store_true", help="print the whole show as JSON")
    ap.add_argument("--all", action="store_true", help="with --triggers: every beat of a fill on its own line")
    args = ap.parse_args()
    if not os.path.exists(args.show):
        sys.exit(f"no such file: {args.show}")
    db = sqlite3.connect(f"file:{os.path.abspath(args.show)}?mode=ro", uri=True)
    problems = []
    meta = dict(db.execute("SELECT key, value FROM meta").fetchall())
    ver = int(meta.get("schema_version", "0") or 0)
    if ver != KNOWN_SCHEMA:
        problems.append(f"schema_version {ver} (this reader knows {KNOWN_SCHEMA})")
    lists = db.execute("SELECT id, name FROM cue_lists ORDER BY ord").fetchall()
    ids = {lid for lid, _ in lists}
    orphans = db.execute("SELECT COUNT(*) FROM cues WHERE list_id NOT IN (SELECT id FROM cue_lists)").fetchone()[0]
    if orphans:
        problems.append(f"{orphans} cue(s) belong to no list (quewi won't show them)")
    out = {"name": meta.get("workspace_name", ""), "lists": []}
    print(f"show: {meta.get('workspace_name', '(no name)')}   schema v{ver}   {os.path.abspath(args.show)}")
    for lid, lname in lists:
        rows = db.execute("SELECT id, type, payload FROM cues WHERE list_id = ? ORDER BY ord", (lid,)).fetchall()
        print(f"\n[{lname}]  {len(rows)} cues")
        L = {"name": lname, "cues": []}
        for cid, ctype, payload in rows:
            try:
                p = json.loads(payload)
            except json.JSONDecodeError:
                problems.append(f"cue {cid}: payload isn't JSON (quewi loads it with defaults)")
                p = {}
            L["cues"].append({"type": ctype, **p})
            extra = ""
            if ctype == "audio":
                fp = p.get("filePath", "")
                if fp and not os.path.exists(fp):
                    problems.append(f"cue {p.get('number', 0):g}: missing file {fp}")
                extra = f"  <{os.path.basename(fp)}>"
                if p.get("beatGrid"):
                    g = p["beatGrid"]
                    extra += f"  grid {g['bpm']:.1f} BPM @ {g['firstBeat']:.3f}s"
                if p.get("lightTriggers"):
                    extra += f"  {len(p['lightTriggers'])} triggers"
            cont = CONT.get(p.get("continueMode", 0), "?")
            print(f"  {p.get('number', 0):>6g}  {ctype:<6} {p.get('name', '')}{extra}" + (f"  [{cont}]" if cont else ""))
            if args.triggers:
                trs = p.get("lightTriggers", [])
                k = 0
                while k < len(trs):
                    t = trs[k]
                    # A run of one group doing one thing (a beat fill) is one line.
                    j = k
                    while (not args.all and t.get("group") and j + 1 < len(trs)
                           and trs[j + 1].get("group") == t.get("group")
                           and trs[j + 1].get("enter") == t.get("enter") and "end" not in trs[j + 1]):
                        j += 1
                    if j > k:
                        print(f"           {mmss(t['start'])}-{mmss(trs[j]['start'])}  {t['group']:<22} "
                              f"{desk_text(t.get('enter'))} x{j - k + 1}")
                        k = j + 1
                        continue
                    k += 1
                    rng = f"-{mmss(t['end'])}" if t.get("end", -1) >= 0 else ""
                    grp = f"  ({t['group']})" if t.get("group") else ""
                    off = "" if t.get("enabled", True) else "  [off]"
                    print(f"           {mmss(t['start'])}{rng}  {t.get('name', ''):<22} {desk_text(t.get('enter'))}"
                          + (f" / out: {desk_text(t.get('exit'))}" if t.get('exit') else "") + grp + off)
        out["lists"].append(L)
    if args.json:
        print(json.dumps(out, indent=1))
    print()
    if problems:
        print("PROBLEMS:")
        for p in problems:
            print("  -", p)
        sys.exit(1)
    print("OK: quewi can open this show.")


if __name__ == "__main__":
    main()
