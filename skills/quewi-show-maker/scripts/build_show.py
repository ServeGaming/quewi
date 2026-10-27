#!/usr/bin/env python3
"""Write a quewi show file (.quewi) from a plan.

  build_show.py PLAN.json [--analysis analysis.json] [--out show.quewi]
                [--force] [--sheet lighting.md]

The plan says which tracks become cues and what each one tells the lighting
desk, and when (see the skill's reference/plan-format.md). Times can be
seconds (83.5), minutes ("1:23.5"), or musical ("bar 17", "bar 17 beat 3")
when an analysis from analyze_track.py is given. "fill" entries put a trigger
on every beat (or every Nth) of a span, on the beats actually heard in the
song (or on the cue's beat grid).

Writes the show exactly as quewi itself saves one (SQLite, schema v1), plus a
lighting cue sheet (Markdown + CSV) listing every desk cue the show calls, so
the cues can be built on the desk. Nothing is sent anywhere.
"""
import argparse
import csv
import json
import os
import re
import sqlite3
import sys
import uuid

SCHEMA_VERSION = 1
CONTINUE = {"none": 0, "": 0, None: 0, "do-not-continue": 0,
            "auto-continue": 1, "continue": 1, "auto-follow": 2, "follow": 2}
DESK_DO = {"go", "stop", "back", "cue", "subLevel", "subBump", "faderLevel",
           "faderBump", "macro", "command", "goList"}
DESK_DO_ALIASES = {"gotocue": "cue", "goto": "cue", "go_to_cue": "cue", "sub": "subLevel",
                   "bump": "subBump", "sublevel": "subLevel", "subbump": "subBump",
                   "faderlevel": "faderLevel", "faderbump": "faderBump", "golist": "goList"}

errors, warnings = [], []


def qt_uuid():
    """A new id as Qt writes one for lists/cues: {lowercase-with-braces}."""
    return "{" + str(uuid.uuid4()) + "}"


# ── times ───────────────────────────────────────────────────────────────────

def parse_time(v, track, where):
    """Seconds from 83.5 / "83.5" / "1:23.5" / "bar 17" / "bar 17 beat 3"."""
    if isinstance(v, (int, float)):
        return float(v)
    s = str(v).strip().lower()
    m = re.fullmatch(r"(\d+):(\d+(?:\.\d+)?)", s)
    if m:
        return int(m.group(1)) * 60 + float(m.group(2))
    m = re.fullmatch(r"bar\s+(\d+)(?:\s+beat\s+(\d+(?:\.\d+)?))?", s)
    if m:
        if not track:
            errors.append(f"{where}: '{v}' needs --analysis (bars come from the song's beats)")
            return 0.0
        bar, beat = int(m.group(1)), float(m.group(2) or 1)
        db = track["downbeats"]
        if not 1 <= bar <= len(db):
            errors.append(f"{where}: bar {bar} isn't in the song (it has {len(db)} bars)")
            return 0.0
        if beat == 1:
            return db[bar - 1]
        beats = track["beats"]
        i0 = min(range(len(beats)), key=lambda i: abs(beats[i] - db[bar - 1]))
        idx = i0 + (beat - 1)
        lo = int(idx)
        if lo + 1 < len(beats):
            return beats[lo] + (idx - lo) * (beats[lo + 1] - beats[lo])
        return beats[-1]
    try:
        return float(s)
    except ValueError:
        errors.append(f"{where}: can't read the time '{v}'")
        return 0.0


# ── trigger actions ─────────────────────────────────────────────────────────

def desk_action(a, where):
    """The JSON quewi stores for a "desk" action (TriggerAction::toJson)."""
    do = str(a.get("do", "go"))
    do = DESK_DO_ALIASES.get(do.lower().replace(" ", ""), do)
    if do not in DESK_DO:
        errors.append(f"{where}: unknown desk action '{do}' (one of {sorted(DESK_DO)})")
        do = "go"
    o = {"kind": "desk", "do": do}
    if do in ("go", "stop", "back"):
        return o
    if do == "command":
        o["text"] = str(a.get("text", ""))
        if not o["text"]:
            errors.append(f"{where}: a desk command needs 'text'")
        return o
    if do == "goList":
        o["list"] = int(a.get("list", 1))
        if a.get("number") not in (None, ""):
            o["number"] = str(a["number"])
        return o
    if a.get("number") in (None, ""):
        errors.append(f"{where}: '{do}' needs a 'number'")
    o["number"] = str(a.get("number", "")).strip()
    if do in ("cue", "faderLevel", "faderBump"):
        o["list"] = int(a.get("list", 1))
    if do in ("subLevel", "faderLevel"):
        o["level"] = int(max(0, min(100, a.get("level", 100))))
    if do in ("subBump", "faderBump"):
        o["hold"] = float(max(0.02, min(30.0, a.get("hold", 0.25))))
    return o


def action(a, where):
    if a is None:
        return {"kind": "none"}
    if "osc" in a:
        o = a["osc"]
        return {"kind": "osc", "host": o.get("host", ""), "port": int(o.get("port", 0)),
                "transport": int(o.get("transport", 0)), "address": o["address"],
                "args": str(o.get("args", ""))}
    if "midi" in a:
        o = a["midi"]
        return {"kind": "midi", "midiPort": o.get("port", ""), "midiType": o.get("type", "noteOn"),
                "channel": int(o.get("channel", 1)), "data1": int(o.get("data1", 60)),
                "data2": int(o.get("data2", 127))}
    if "desk" in a:
        return desk_action(a["desk"], where)
    if "do" in a:
        return desk_action(a, where)
    errors.append(f"{where}: an action needs 'do' (a desk action), 'osc' or 'midi'")
    return {"kind": "none"}


def describe(act):
    k = act.get("kind")
    if k != "desk":
        return {"osc": "OSC " + act.get("address", ""), "midi": "MIDI", "none": "nothing"}.get(k, k)
    d = act["do"]
    if d == "cue":
        return f"Go to cue {act['list']}/{act['number']}"
    return {"go": "GO", "stop": "Stop", "back": "Back",
            "subLevel": f"Sub {act.get('number')} @ {act.get('level')}%",
            "subBump": f"Bump sub {act.get('number')} ({act.get('hold')} s)",
            "faderLevel": f"Fader {act.get('list')}/{act.get('number')} @ {act.get('level')}%",
            "faderBump": f"Bump fader {act.get('list')}/{act.get('number')}",
            "macro": f"Macro {act.get('number')}", "command": f"Command: {act.get('text')}",
            "goList": f"GO list {act.get('list')}"}.get(d, d)


# ── triggers ────────────────────────────────────────────────────────────────

def beat_times_for(track, grid, t0, t1, every, source):
    if source == "grid" or not track:
        if not grid:
            errors.append("a 'fill' on the grid needs the cue's beatGrid (or an analysis)")
            return []
        L = 60.0 / grid["bpm"]
        k0 = int((t0 - grid["firstBeat"]) / L - 1e-9)
        out, k = [], max(0, k0)
        while True:
            t = grid["firstBeat"] + k * L
            if t > t1 + 1e-6:
                break
            if t >= t0 - 1e-6 and (k % every == 0):
                out.append(t)
            k += 1
        return out
    beats = [b for b in track["beats"] if t0 - 0.03 <= b <= t1 + 0.03]
    return beats[::every]


def bar_span(bars, track, where):
    """ "92-114" -> (bar 92's downbeat, just before bar 115's): whole bars."""
    try:
        a, _, b = str(bars).partition("-")
        a, b = int(a), int(b or a)
    except ValueError:
        errors.append(f"{where}: bars must look like \"92-114\"")
        return 0.0, 0.0
    t0 = parse_time(f"bar {a}", track, where)
    db = track["downbeats"] if track else []
    t1 = (db[b] - 0.05) if b < len(db) else (track["musicEnd"] if track else 0.0)
    return t0, t1


def build_triggers(spec, track, grid, cue_where, duration):
    out, sheet = [], []
    for i, tr in enumerate(spec or []):
        where = f"{cue_where} trigger {i + 1}"
        if "fill" in tr:
            f = tr["fill"]
            if "bars" in f:
                t0, t1 = bar_span(f["bars"], track, where)
            else:
                # "to" is inclusive: "bar 87" stops ON bar 87's downbeat.
                t0 = parse_time(f.get("from", 0), track, where)
                t1 = parse_time(f.get("to", duration or 0), track, where)
            if duration and t1 > duration + 0.05:
                errors.append(f"{where}: the fill runs to {t1:.2f}s, past the end of the track ({duration:.2f}s)")
            every = int(f.get("every", 1))
            times = beat_times_for(track, grid, t0, t1, every, f.get("beats", "tracked"))
            if not times:
                warnings.append(f"{where}: the fill from {t0:.2f}s to {t1:.2f}s found no beats")
            act = action(f.get("action") or f, where)
            group = f.get("group") or tr.get("group") or f"Beats {t0:.0f}-{t1:.0f}s"
            name = f.get("name", "Beat {n}")
            for n, t in enumerate(times, 1):
                out.append({"id": str(uuid.uuid4()), "name": name.replace("{n}", str(n)),
                            "start": round(t, 4), "enabled": True, "enter": act, "group": group})
            if times:
                ev = {1: "every beat", 2: "every 2nd beat"}.get(every, f"every {every} beats")
                sheet.append({"at": times[0], "until": times[-1], "trigger": group,
                              "desk": f"{describe(act)} x{len(times)}, {ev}", "act": act,
                              "note": f.get("note", tr.get("note", ""))})
            continue
        t = parse_time(tr.get("at", 0), track, where)
        o = {"id": str(uuid.uuid4()), "name": tr.get("name", ""), "start": round(max(0.0, t), 4),
             "enabled": bool(tr.get("enabled", True)), "enter": action(tr.get("enter", tr), where)}
        if tr.get("group"):
            o["group"] = str(tr["group"]).strip()
        if "end" in tr:
            e = parse_time(tr["end"], track, where)
            if e <= t:
                errors.append(f"{where}: 'end' ({e:.2f}s) must be after 'at' ({t:.2f}s)")
            o["end"] = round(e, 4)
            o["exit"] = action(tr.get("exit"), where + " exit")
        if duration and t > duration:
            errors.append(f"{where}: at {t:.2f}s is past the end of the track ({duration:.2f}s)")
        out.append(o)
        sheet.append({"at": o["start"], "trigger": o["name"], "desk": describe(o["enter"]),
                      "act": o["enter"], "note": tr.get("note", "")})
        if "exit" in o:
            sheet.append({"at": o["end"], "trigger": o["name"] + " (out)", "desk": describe(o["exit"]),
                          "act": o["exit"], "note": ""})
    out.sort(key=lambda x: x["start"])
    sheet.sort(key=lambda x: x["at"])
    return out, sheet


# ── cues ────────────────────────────────────────────────────────────────────

def base_payload(c, number, where):
    p = {"number": float(number), "name": str(c.get("name", "")),
         "preWait": float(c.get("preWait", 0)), "postWait": float(c.get("postWait", 0)),
         "continueMode": CONTINUE.get(c.get("continue"), None),
         "notes": str(c.get("notes", "")), "armed": bool(c.get("armed", True))}
    if p["continueMode"] is None:
        errors.append(f"{where}: continue must be none / auto-continue / auto-follow")
        p["continueMode"] = 0
    if c.get("color"):
        col = str(c["color"]).lstrip("#")
        if len(col) == 6:
            col = "ff" + col
        p["color"] = "#" + col.lower()
    return p


def find_track(analysis, path):
    if not analysis:
        return None
    ap = os.path.normcase(os.path.abspath(path))
    for t in analysis["tracks"]:
        if os.path.normcase(os.path.abspath(t["file"])) == ap:
            return t
    base = os.path.basename(path).lower()
    for t in analysis["tracks"]:
        if os.path.basename(t["file"]).lower() == base:
            return t
    return None


def audio_cue(c, number, where, plan_dir, analysis):
    if not c.get("file"):
        errors.append(f"{where}: an audio cue needs 'file'")
        return None, None, None
    path = c["file"] if os.path.isabs(c["file"]) else os.path.join(plan_dir, c["file"])
    path = os.path.abspath(path)
    if not os.path.exists(path):
        errors.append(f"{where}: the track {path} doesn't exist")
    track = find_track(analysis, path)
    duration = track["duration"] if track else None
    p = base_payload(c, number, where)
    if not p["name"]:
        p["name"] = os.path.splitext(os.path.basename(path))[0]
    p.update({"filePath": path.replace("\\", "/"),          # Qt reads / on every OS
              "gainDb": float(c.get("gainDb", 0)), "fadeInSeconds": float(c.get("fadeIn", 0)),
              "fadeOutSeconds": float(c.get("fadeOut", 0)), "trimInSeconds": float(c.get("trimIn", 0)),
              "trimOutSeconds": float(c.get("trimOut", 0)), "pan": float(c.get("pan", 0)),
              "loop": bool(c.get("loop", False)), "outputDeviceId": ""})
    grid = c.get("beatGrid")
    if grid == "auto":
        grid = track["grid"] if track and track.get("grid") else None
        if not grid:
            warnings.append(f"{where}: beatGrid 'auto' but no analysis for this track")
    if grid:
        g = {"bpm": float(grid["bpm"]), "firstBeat": float(grid.get("firstBeat", 0)),
             "beatsPerBar": int(grid.get("beatsPerBar", 4))}
        if not 0 < g["bpm"] <= 999:
            errors.append(f"{where}: beatGrid bpm must be 1-999")
        p["beatGrid"] = g
    if grid and track and track.get("grid") and not track["grid"].get("steady", True):
        warnings.append(f"{where}: this track's tempo moves, so a single beatGrid drifts against it "
                        f"(fills still use the beats heard; consider leaving beatGrid out)")
    # Vamps / repeats / holds / skips / markers (quewi 1.3+: AudioCue "playEvents").
    events = []
    for k, e in enumerate(c.get("vamps") or []):
        w = f"{where} vamp {k + 1}"
        kind = str(e.get("kind", "vamp")).lower()
        if kind in ("caesura", "fermata"):
            kind = "hold"
        if kind == "repeat":
            kind = "vamp"
        if kind not in ("vamp", "hold", "skip", "marker"):
            errors.append(f"{w}: kind must be vamp / repeat / hold / skip / marker")
            continue
        if "bars" in e:
            a0, a1 = bar_span(e["bars"], track, w)
            a1 += 0.05                       # bar_span stops just short of the next bar line
        else:
            a0 = parse_time(e.get("start", e.get("at", 0)), track, w)
            a1 = parse_time(e["end"], track, w) if "end" in e else a0
        o = {"id": str(uuid.uuid4()), "kind": kind, "start": round(a0, 4)}
        if e.get("name"):
            o["name"] = str(e["name"])
        if kind in ("vamp", "skip"):
            if a1 - a0 < 0.05:
                errors.append(f"{w}: a {kind} needs an end after its start")
            o["end"] = round(a1, 4)
        if kind == "vamp":
            n = int(e.get("count", e.get("times", 0)) or 0)
            if str(e.get("kind", "")).lower() == "repeat" and n < 2:
                errors.append(f"{w}: a repeat needs a count of 2 or more")
            if n:
                o["count"] = max(2, n)
            ex = e.get("exit")
            if ex:
                o["exit"] = {"end": "endOfPass", "endofpass": "endOfPass", "bar": "nextBar",
                             "nextbar": "nextBar", "now": "now"}.get(str(ex).replace(" ", "").lower(), "endOfPass")
        if duration and a0 > duration:
            errors.append(f"{w}: starts past the end of the track")
        events.append(o)
    vamps = sorted((x for x in events if x["kind"] == "vamp"), key=lambda x: x["start"])
    for x, y in zip(vamps, vamps[1:]):
        if y["start"] < x["end"]:
            errors.append(f"{where}: vamps at {x['start']:.2f}s and {y['start']:.2f}s overlap")
    if events:
        p["playEvents"] = sorted(events, key=lambda x: x["start"])
    trig, sheet = build_triggers(c.get("triggers"), track, p.get("beatGrid"), where, duration)
    if trig:
        p["lightTriggers"] = trig
    return p, trig, sheet


def write_show(out, name, lists):
    tmp = out + ".writing"
    if os.path.exists(tmp):
        os.remove(tmp)
    db = sqlite3.connect(tmp)
    db.executescript(
        "CREATE TABLE IF NOT EXISTS meta (key TEXT PRIMARY KEY, value TEXT);"
        "CREATE TABLE IF NOT EXISTS cue_lists (id TEXT PRIMARY KEY, name TEXT NOT NULL, ord INTEGER NOT NULL);"
        "CREATE TABLE IF NOT EXISTS cues (id TEXT PRIMARY KEY,"
        " list_id TEXT NOT NULL REFERENCES cue_lists(id) ON DELETE CASCADE,"
        " parent_id TEXT, ord INTEGER NOT NULL, type TEXT NOT NULL, payload TEXT NOT NULL);")
    db.execute("INSERT INTO meta(key, value) VALUES ('schema_version', ?)", (str(SCHEMA_VERSION),))
    db.execute("INSERT INTO meta(key, value) VALUES ('workspace_name', ?)", (name,))
    for lo, (lname, cues) in enumerate(lists):
        lid = qt_uuid()
        db.execute("INSERT INTO cue_lists(id, name, ord) VALUES (?, ?, ?)", (lid, lname, lo))
        for co, (ctype, payload) in enumerate(cues):
            db.execute("INSERT INTO cues(id, list_id, parent_id, ord, type, payload) VALUES (?, ?, NULL, ?, ?, ?)",
                       (qt_uuid(), lid, co, ctype, json.dumps(payload, separators=(",", ":"), ensure_ascii=False)))
    db.commit()
    db.close()
    os.replace(tmp, out)


def mmss(s):
    return f"{int(s // 60)}:{s % 60:05.2f}"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("plan")
    ap.add_argument("--analysis", help="analysis JSON from analyze_track.py (for bars / fills / checks)")
    ap.add_argument("--out", help="the .quewi to write (default: the plan's show.output)")
    ap.add_argument("--sheet", help="lighting cue sheet (.md; a .csv is written beside it)")
    ap.add_argument("--force", action="store_true", help="replace an existing show file")
    ap.add_argument("--check", action="store_true", help="check the plan, write nothing")
    args = ap.parse_args()

    with open(args.plan, encoding="utf-8") as f:
        plan = json.load(f)
    analysis = None
    if args.analysis:
        with open(args.analysis, encoding="utf-8") as f:
            analysis = json.load(f)
    plan_dir = os.path.dirname(os.path.abspath(args.plan))
    show = plan.get("show", {})
    out = args.out or show.get("output")
    if not out:
        sys.exit("say where to write the show: --out, or show.output in the plan")
    out = os.path.abspath(out if os.path.isabs(out) else os.path.join(plan_dir, out))
    if not out.lower().endswith(".quewi"):
        out += ".quewi"
    name = show.get("name") or os.path.splitext(os.path.basename(out))[0]

    lists_spec = plan.get("lists") or [{"name": show.get("listName", "Main"), "cues": plan.get("cues", [])}]
    lists, sheet_rows, cue_summary = [], [], []
    for li, l in enumerate(lists_spec):
        cues, used, auto = [], set(), 0
        for ci, c in enumerate(l.get("cues", [])):
            ctype = c.get("type", "audio" if c.get("file") else "memo")
            auto = float(c["number"]) if "number" in c else auto + 1
            number = auto
            where = f"list '{l.get('name', 'Main')}' cue {number:g}"
            if number in used:
                errors.append(f"{where}: cue number {number:g} is used twice")
            used.add(number)
            if ctype == "audio":
                p, trig, sheet = audio_cue(c, number, where, plan_dir, analysis)
                if p is None:
                    continue
                cues.append(("audio", p))
                cue_summary.append((number, p["name"], len(trig or [])))
                for r in sheet or []:
                    act = r["act"]
                    at = mmss(r["at"]) + (f" - {mmss(r['until'])}" if "until" in r else "")
                    sheet_rows.append({"sound cue": f"{number:g}", "song": p["name"], "at": at,
                                       "seconds": round(r["at"], 3), "trigger": r["trigger"],
                                       "desk": r["desk"], "action": act.get("do", act.get("kind", "")),
                                       "list": act.get("list", "") if act.get("do") in ("cue", "goList", "faderLevel", "faderBump") else "",
                                       "number": act.get("number", ""), "note": r["note"]})
            elif ctype == "memo":
                p = base_payload(c, number, where)
                cues.append(("memo", p))
                cue_summary.append((number, p["name"], 0))
                sheet_rows.append({"sound cue": f"{number:g}", "song": f"(memo) {p['name']}", "at": "",
                                   "seconds": "", "trigger": "", "desk": "", "action": "memo",
                                   "list": "", "number": "", "note": p["notes"]})
            else:
                errors.append(f"{where}: this skill writes audio and memo cues (got '{ctype}')")
        lists.append((l.get("name", "Main"), cues))

    for w in warnings:
        print("warning:", w)
    if errors:
        for e in errors:
            print("error:", e)
        sys.exit(f"{len(errors)} problem(s) in the plan; nothing written")

    print(f"show '{name}': {sum(len(c) for _, c in lists)} cues in {len(lists)} list(s)")
    for n, nm, k in cue_summary:
        print(f"  {n:g}  {nm}" + (f"  - {k} lighting triggers" if k else ""))
    if args.check:
        print("plan OK (--check: nothing written)")
        return
    if os.path.exists(out) and not args.force:
        sys.exit(f"{out} already exists: pick another name, or --force to replace it")
    os.makedirs(os.path.dirname(out), exist_ok=True)
    write_show(out, name, lists)
    print(f"wrote {out}")

    sheet = args.sheet or os.path.splitext(out)[0] + " - lighting cues.md"
    if sheet_rows:
        with open(sheet, "w", encoding="utf-8") as f:
            f.write(f"# {name}: what quewi tells the lighting desk\n\n")
            f.write("Build these on the desk (cues, subs, macros). Times are into each song; "
                    "a range is a run of beat triggers.\n\n")
            f.write("| Sound cue | Song | At | Trigger | quewi tells the desk | Note |\n|---|---|---|---|---|---|\n")
            for r in sheet_rows:
                f.write(f"| {r['sound cue']} | {r['song']} | {r['at']} | {r['trigger']} | {r['desk']} | {r['note']} |\n")
        with open(os.path.splitext(sheet)[0] + ".csv", "w", newline="", encoding="utf-8") as f:
            w = csv.DictWriter(f, fieldnames=list(sheet_rows[0].keys()))
            w.writeheader()
            w.writerows(sheet_rows)
        print(f"wrote {sheet} (+ .csv)")


if __name__ == "__main__":
    main()
