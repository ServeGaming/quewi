#!/usr/bin/env python3
"""Listen to a track so lighting can be timed to it.

Decodes any audio/video file with ffmpeg and finds, with numpy only (no
librosa, so it runs on any Python with numpy):

  - where the music starts and ends (leading/trailing silence, fade-out)
  - tempo (BPM), every beat, and which beats are bar downbeats
  - a constant beat grid quewi can use (bpm, firstBeat, beatsPerBar) and how
    well the song actually holds that tempo
  - sections: where the music changes character (verse -> chorus), snapped
    to bar lines, labelled A/B/C by similarity, with a loudness rank
  - hits: the strongest single accents (stabs, crashes, button endings)
  - drops/lifts and stops: big jumps in loudness, and sudden silences
  - a per-bar loudness curve

Usage:
  analyze_track.py TRACK [TRACK ...] [--out analysis.json] [--bpb 4]
                   [--bpm-hint 120] [--min-section-bars 4]

Prints a readable summary per track and (with --out) writes all the
numbers as JSON for build_show.py / the plan.
"""
import argparse
import json
import os
import shutil
import subprocess
import sys

import numpy as np

SR = 22050          # analysis sample rate
HOP = 512           # ~23 ms per frame
NFFT = 2048


# ── decoding ────────────────────────────────────────────────────────────────

def find_ffmpeg():
    exe = shutil.which("ffmpeg")
    if exe:
        return exe
    for c in (r"C:\ffmpeg\bin\ffmpeg.exe", "/opt/homebrew/bin/ffmpeg", "/usr/local/bin/ffmpeg"):
        if os.path.exists(c):
            return c
    sys.exit("ffmpeg not found: install it (winget install ffmpeg / brew install ffmpeg / apt install ffmpeg) or put it on PATH")


def decode(path, ffmpeg):
    cmd = [ffmpeg, "-v", "error", "-nostdin", "-i", path, "-vn", "-ac", "1",
           "-ar", str(SR), "-f", "f32le", "-"]
    raw = subprocess.run(cmd, capture_output=True, check=False)
    if raw.returncode != 0 or not raw.stdout:
        sys.exit(f"ffmpeg couldn't decode {path}: {raw.stderr.decode(errors='replace').strip()}")
    return np.frombuffer(raw.stdout, dtype=np.float32).astype(np.float64)


# ── features ────────────────────────────────────────────────────────────────

def stft_mag(y):
    win = np.hanning(NFFT)
    n = 1 + max(0, (len(y) - NFFT) // HOP)
    if n <= 1:
        return np.zeros((NFFT // 2 + 1, 1))
    idx = np.arange(NFFT)[None, :] + HOP * np.arange(n)[:, None]
    frames = y[idx] * win
    return np.abs(np.fft.rfft(frames, axis=1)).T          # (bins, frames)


def frame_times(n):
    return (np.arange(n) * HOP + NFFT / 2) / SR


def onset_envelope(S):
    """Spectral flux on a log-compressed spectrum, mean-removed, >= 0."""
    L = np.log1p(1000.0 * S)
    flux = np.maximum(0.0, np.diff(L, axis=1)).sum(axis=0)
    flux = np.concatenate([[0.0], flux])
    # Local mean removal: what's left is change, not loudness.
    k = 16
    kern = np.ones(2 * k + 1) / (2 * k + 1)
    local = np.convolve(flux, kern, mode="same")
    env = np.maximum(0.0, flux - local)
    if env.max() > 0:
        env /= env.std() + 1e-9
    return env


def rms_db(y):
    n = 1 + max(0, (len(y) - NFFT) // HOP)
    idx = np.arange(NFFT)[None, :] + HOP * np.arange(n)[:, None]
    r = np.sqrt((y[idx] ** 2).mean(axis=1) + 1e-12)
    return 20 * np.log10(r + 1e-9)


def chroma(S):
    """12 pitch classes from the spectrum (65 Hz – 4 kHz)."""
    freqs = np.fft.rfftfreq(NFFT, 1.0 / SR)
    C = np.zeros((12, S.shape[1]))
    ok = (freqs > 65) & (freqs < 4000)
    pcs = np.round(12 * np.log2(freqs[ok] / 440.0) + 9).astype(int) % 12
    P = S[ok] ** 2
    for pc in range(12):
        C[pc] = P[pcs == pc].sum(axis=0)
    C = np.log1p(C)
    return C / (np.linalg.norm(C, axis=0, keepdims=True) + 1e-9)


def band_energies(S, nbands=8):
    freqs = np.fft.rfftfreq(NFFT, 1.0 / SR)
    edges = np.geomspace(40, SR / 2, nbands + 1)
    B = np.zeros((nbands, S.shape[1]))
    for i in range(nbands):
        m = (freqs >= edges[i]) & (freqs < edges[i + 1])
        if m.any():
            B[i] = np.log1p((S[m] ** 2).sum(axis=0))
    return B


# ── tempo + beats (Ellis 2007 dynamic programming) ──────────────────────────

def estimate_tempo(env, bpm_hint=None):
    fps = SR / HOP
    lo, hi = int(fps * 60 / 210), int(fps * 60 / 55)
    x = env - env.mean()
    ac = np.correlate(x, x, mode="full")[len(x) - 1:]
    ac = ac / (ac[0] + 1e-9)
    lags = np.arange(lo, min(hi, len(ac) - 1))
    bpms = 60.0 * fps / lags
    centre = bpm_hint or 120.0
    weight = np.exp(-0.5 * (np.log2(bpms / centre) / (1.0 if bpm_hint is None else 0.35)) ** 2)
    # Also reward lags whose double is strong (a real beat repeats).
    dbl = np.array([ac[min(len(ac) - 1, 2 * l)] for l in lags])
    score = (ac[lags] + 0.5 * dbl) * weight
    best = lags[np.argmax(score)]
    # Parabolic refinement of the peak.
    if 1 <= best < len(ac) - 1:
        a, b, c = ac[best - 1], ac[best], ac[best + 1]
        off = 0.5 * (a - c) / (a - 2 * b + c + 1e-12)
        best = best + float(np.clip(off, -0.5, 0.5))
    bpm = 60.0 * fps / best
    cands = sorted({round(float(bpm) / 2, 1), round(float(bpm), 1), round(float(bpm) * 2, 1)})
    return bpm, [c for c in cands if 40 <= c <= 260]


def track_beats(env, bpm, tightness=100.0):
    fps = SR / HOP
    period = fps * 60.0 / bpm
    n = len(env)
    score = env.astype(np.float64).copy()
    back = -np.ones(n, dtype=int)
    lo, hi = int(round(period / 2)), int(round(period * 2))
    for t in range(n):
        a, b = t - hi, t - lo
        if b < 0:
            continue
        prev = np.arange(max(0, a), b + 1)
        pen = -tightness * (np.log((t - prev) / period)) ** 2
        cand = score[prev] + pen
        j = int(np.argmax(cand))
        if cand[j] > 0:
            score[t] += cand[j]
            back[t] = prev[j]
    # Start from the best-scoring frame in the last period; walk back.
    tail = np.arange(max(0, n - int(period)), n)
    t = int(tail[np.argmax(score[tail])]) if len(tail) else n - 1
    beats = []
    while t >= 0:
        beats.append(t)
        t = back[t]
    beats = np.array(beats[::-1])
    # Trim weak beats tracked into leading/trailing silence.
    if len(beats):
        strong = env[beats] > 0.15 * np.median(env[beats] + 1e-9)
        if strong.any():
            first, last = np.argmax(strong), len(strong) - 1 - np.argmax(strong[::-1])
            beats = beats[first:last + 1]
    return frame_times(n)[beats] if len(beats) else np.array([]), beats


def downbeat_phase(beat_frames, env, low, bpb):
    """Which beat of each bar is the 1: the phase with the most low-end + onset."""
    best, best_score = 0, -1e18
    for ph in range(bpb):
        sel = beat_frames[ph::bpb]
        if len(sel) == 0:
            continue
        s = env[sel].mean() + 1.5 * low[sel].mean()
        if s > best_score:
            best, best_score = ph, s
    return best


def fit_grid(beat_times, downbeat_index, bpb):
    """Least-squares constant grid through the tracked beats; how well it holds."""
    if len(beat_times) < 8:
        return None
    # Number the beats by the gaps between them, so a beat the tracker
    # skipped (or doubled) doesn't shift every later beat's number.
    ioi = np.diff(beat_times)
    period = float(np.median(ioi))
    k = np.r_[0, np.cumsum(np.maximum(1, np.round(ioi / period)))].astype(float)
    downbeat_index = int(k[min(downbeat_index, len(k) - 1)])
    A = np.vstack([k, np.ones_like(k)]).T
    slope, icpt = np.linalg.lstsq(A, beat_times, rcond=None)[0]
    resid = beat_times - (slope * k + icpt)
    bpm = 60.0 / slope
    # firstBeat = the grid time of the first downbeat, moved back by whole
    # bars to the earliest one at >= 0 s.
    first_db = icpt + slope * downbeat_index
    bar = slope * bpb
    while first_db - bar >= 0:
        first_db -= bar
    drift_ms = float(np.percentile(np.abs(resid), 90) * 1000)
    return {
        "bpm": round(float(bpm), 3),
        "firstBeat": round(float(max(0.0, first_db)), 4),
        "beatsPerBar": bpb,
        "drift90Ms": round(drift_ms, 1),
        "steady": drift_ms < 60.0,
    }


# ── structure ───────────────────────────────────────────────────────────────

def bar_features(feat, bar_frames):
    out = []
    for a, b in zip(bar_frames[:-1], bar_frames[1:]):
        b = max(b, a + 1)
        out.append(feat[:, a:b].mean(axis=1))
    return np.array(out)


def novelty(F, k):
    """Checkerboard-kernel novelty on a cosine self-similarity matrix."""
    X = F - F.mean(axis=0)
    X = X / (np.linalg.norm(X, axis=1, keepdims=True) + 1e-9)
    Ssm = X @ X.T
    n = len(Ssm)
    g = np.exp(-0.5 * (np.linspace(-1, 1, 2 * k) / 0.5) ** 2)
    kern = np.outer(g, g) * np.outer(np.r_[-np.ones(k), np.ones(k)], np.r_[-np.ones(k), np.ones(k)])
    nov = np.zeros(n)
    for i in range(k, n - k + 1):
        nov[i] = (Ssm[i - k:i + k, i - k:i + k] * kern).sum()
    nov = np.maximum(0, nov)
    return nov / (nov.max() + 1e-9), Ssm


def pick_peaks(x, min_gap, thresh):
    peaks = []
    for i in range(1, len(x) - 1):
        if x[i] >= thresh and x[i] >= x[i - 1] and x[i] >= x[i + 1]:
            if peaks and i - peaks[-1] < min_gap:
                if x[i] > x[peaks[-1]]:
                    peaks[-1] = i
                continue
            peaks.append(i)
    return peaks


def label_sections(F, bounds):
    """A, B, C… : sections whose average sound is alike share a letter."""
    means = []
    for a, b in zip(bounds[:-1], bounds[1:]):
        m = F[a:b].mean(axis=0)
        means.append(m / (np.linalg.norm(m) + 1e-9))
    labels, protos = [], []
    for m in means:
        sims = [float(m @ p) for p in protos]
        if sims and max(sims) > 0.985:
            labels.append(chr(ord("A") + int(np.argmax(sims))))
        else:
            protos.append(m)
            labels.append(chr(ord("A") + len(protos) - 1) if len(protos) <= 26 else "?")
    return labels


# ── how the song ends ───────────────────────────────────────────────────────

def classify_ending(t, db, env, start, end):
    """button (loud until a hard stop), fade (a long steady fall) or ring
    (a last hit left to decay); and the last strong hit before the end."""
    fps = SR / HOP
    sm = np.convolve(db, np.ones(11) / 11, mode="same")            # 0.25 s
    body = (t > start) & (t < end - 2)
    ref = np.percentile(sm[body], 75) if body.any() else sm.max()
    loud = np.where((sm > ref - 8) & (t <= end))[0]
    last_loud = float(t[loud[-1]]) if len(loud) else end
    # Last strong onset: the final 30 s, top quarter of onsets there.
    win = (t > max(start, end - 30)) & (t <= end)
    last_hit = None
    if win.any():
        e = np.where(win, env, 0.0)
        thr = np.percentile(env[win], 92)
        idx = [i for i in range(1, len(e) - 1) if e[i] >= thr and e[i] >= e[i - 1] and e[i] >= e[i + 1]
               and t[i] <= last_loud + 0.3]
        if idx:
            last_hit = float(t[idx[-1]])
    # A fade: from where it last sat at its usual level, the level keeps
    # falling (most 0.5 s steps down), for 4 s or more, by 12 dB or more.
    fade = None
    usual = np.where((sm > ref - 3) & (t < end))[0]
    if len(usual):
        f0 = float(t[usual[-1]])
        span = end - f0
        if span >= 4:
            steps = np.arange(f0, end, 0.5)
            lv = [sm[min(len(sm) - 1, int(x * fps))] for x in steps]
            downs = sum(1 for a, b in zip(lv, lv[1:]) if b < a - 0.2)
            if len(lv) > 4 and downs >= 0.7 * (len(lv) - 1) and lv[0] - lv[-1] >= 12:
                fade = {"from": round(f0, 2), "to": round(end, 2)}
    if fade:
        kind = "fade"
    elif end - last_loud < 1.2:
        kind = "button"
    else:
        kind = "ring"
    ending = {"type": kind, "lastHit": round(last_hit, 3) if last_hit is not None else None,
              "lastLoud": round(last_loud, 3), "silenceAt": round(end, 3)}
    if fade:
        ending.update(fade)
    return ending, fade


# ── main analysis ───────────────────────────────────────────────────────────

def analyze(path, ffmpeg, bpb=4, bpm_hint=None, min_section_bars=4):
    y = decode(path, ffmpeg)
    duration = len(y) / SR
    S = stft_mag(y)
    n = S.shape[1]
    t = frame_times(n)
    env = onset_envelope(S)
    db = rms_db(y)[:n]
    if len(db) < n:
        db = np.pad(db, (0, n - len(db)), constant_values=-120)
    peak_db = np.percentile(db, 99)

    # Where the music is: first/last frame within 40 dB of the loud parts.
    audible = db > peak_db - 40
    start = float(t[np.argmax(audible)]) if audible.any() else 0.0
    end = float(t[len(audible) - 1 - np.argmax(audible[::-1])]) if audible.any() else duration
    ending, fade_out = classify_ending(t, db, env, start, end)

    bpm0, cands = estimate_tempo(env, bpm_hint)
    beat_times, beat_frames = track_beats(env, bpm0)
    B = band_energies(S)
    low = (B[0] + B[1]) / 2
    low = (low - low.mean()) / (low.std() + 1e-9)
    phase = downbeat_phase(beat_frames, env, low, bpb) if len(beat_frames) else 0
    grid = fit_grid(beat_times, phase, bpb)
    downbeats = beat_times[phase::bpb]

    # Bars = downbeat to downbeat (plus the music's start/end).
    bar_frames = np.searchsorted(t, downbeats)
    sections, bar_loud = [], []
    if len(bar_frames) > 2 * min_section_bars:
        C = chroma(S)
        Bz = (B - B.mean(axis=1, keepdims=True)) / (B.std(axis=1, keepdims=True) + 1e-9)
        feat = np.vstack([C, 0.6 * Bz, (db[None, :] - db.mean()) / (db.std() + 1e-9)])
        F = bar_features(feat, np.r_[bar_frames, n])
        Fl = bar_features(db[None, :], np.r_[bar_frames, n])[:, 0]
        bar_loud = [round(float(v), 1) for v in Fl]
        k = max(2, min(8, len(F) // 6))
        nov, _ = novelty(F, k)
        peaks = pick_peaks(nov, min_section_bars, 0.25)
        bounds = [0] + [p for p in peaks if min_section_bars <= p <= len(F) - 2] + [len(F)]
        labels = label_sections(F, bounds)
        louds = []
        for a, b in zip(bounds[:-1], bounds[1:]):
            louds.append(float(np.mean(Fl[a:b])))
        order = np.argsort(np.argsort(louds))
        nsec = len(louds)
        for i, (a, b) in enumerate(zip(bounds[:-1], bounds[1:])):
            st = float(downbeats[a]) if a < len(downbeats) else start
            en = float(downbeats[b]) if b < len(downbeats) else end
            rank = int(order[i])
            level = "low" if rank < nsec / 3 else ("high" if rank >= 2 * nsec / 3 else "mid")
            # This section's own tempo: songs speed up, slow down, break.
            inside = beat_times[(beat_times >= st - 0.05) & (beat_times < en - 0.05)]
            local = fit_grid(inside, 0, bpb) if len(inside) >= 8 else None
            sections.append({
                "index": i + 1, "label": labels[i],
                "bpm": local["bpm"] if local else None,
                "steady": local["steady"] if local else None,
                "start": round(st, 3), "end": round(en, 3),
                "bars": int(b - a), "startBar": int(a + 1),
                "loudnessDb": round(louds[i], 1), "level": level,
                "change": round(float(nov[a]), 2) if a > 0 else None,
            })

    # Hits: onset peaks far above their surroundings, at least 1 s apart,
    # the strongest 12, plus how loud the moment is.
    hits = []
    if len(env) > 50:
        win = int(SR / HOP * 4)
        loc = np.array([np.percentile(env[max(0, i - win):i + win], 90) for i in range(0, len(env), 8)])
        loc = np.repeat(loc, 8)[:len(env)]
        strength = env / (loc + 1e-9)
        cand = pick_peaks(strength * (env > 2.5), int(SR / HOP * 1.0), 1.8)
        top = set(sorted(cand, key=lambda i: -env[i])[:10])
        # …and the strongest in each section, so a late section's stab isn't
        # crowded out by a busy intro.
        for sct in sections:
            inside = [i for i in cand if sct["start"] <= t[i] < sct["end"]]
            if inside:
                top.add(max(inside, key=lambda i: env[i]))
        cand = sorted(top)
        for i in sorted(cand):
            hits.append({"at": round(float(t[i]), 3), "strength": round(float(env[i]), 1),
                         "onBeat": bool(len(beat_times) and np.min(np.abs(beat_times - t[i])) < 0.07)})

    # Drops / lifts (+6 dB bar to bar) and stops (sudden near-silence).
    jumps, stops = [], []
    if len(bar_loud) > 2:
        for i in range(1, len(bar_loud)):
            d = bar_loud[i] - bar_loud[i - 1]
            if abs(d) >= 6:
                jumps.append({"at": round(float(downbeats[i]), 3), "bar": i + 1,
                              "changeDb": round(d, 1), "kind": "lift" if d > 0 else "drop"})
    sm = np.convolve(db, np.ones(5) / 5, mode="same")
    i = 0
    while i < len(sm) - 10:
        if t[i] > start + 1 and t[i] < end - 1 and sm[i] > peak_db - 15 and sm[i + 4] < peak_db - 35:
            stops.append({"at": round(float(t[i + 2]), 3)})
            i += int(SR / HOP * 2)
        i += 1

    return {
        "file": os.path.abspath(path),
        "name": os.path.splitext(os.path.basename(path))[0],
        "duration": round(duration, 3),
        "musicStart": round(start, 3),
        "musicEnd": round(end, 3),
        "fadeOut": fade_out,
        "ending": ending,
        "pickupBeats": int(sum(1 for b in beat_times if len(downbeats) and b < downbeats[0] - 0.05)),
        "firstBeat": round(float(beat_times[0]), 3) if len(beat_times) else None,
        "tempo": {"bpm": round(float(bpm0), 2), "candidates": cands},
        "grid": grid,
        "beats": [round(float(x), 3) for x in beat_times],
        "downbeats": [round(float(x), 3) for x in downbeats],
        "sections": sections,
        "hits": hits,
        "jumps": jumps,
        "stops": stops,
        "barLoudnessDb": bar_loud,
    }


def mmss(s):
    return f"{int(s // 60)}:{s % 60:05.2f}"


def summary(a):
    g = a["grid"]
    e = a["ending"]
    how = {"button": "ends on a button", "fade": "fades out", "ring": "last hit rings out"}[e["type"]]
    endtxt = f"{how}: " + (f"fade {mmss(e['from'])} -> {mmss(e['to'])}" if e["type"] == "fade"
                            else f"last hit {mmss(e['lastHit'])}" if e["lastHit"] is not None else f"loud until {mmss(e['lastLoud'])}")
    lines = [f"== {a['name']}  ({mmss(a['duration'])})",
             f"   music {mmss(a['musicStart'])} -> {mmss(a['musicEnd'])}; {endtxt}"]
    if g:
        lines.append(f"   tempo {g['bpm']:.1f} BPM ({'steady: a beatGrid fits' if g['steady'] else 'tempo moves: no beatGrid, fills use the beats heard'}),"
                     f" {g['beatsPerBar']} beats/bar; also try {a['tempo']['candidates']}")
    if a["downbeats"]:
        lines.append(f"   first beat {mmss(a['firstBeat'])}, bar 1 at {mmss(a['downbeats'][0])}"
                     + (f" ({a['pickupBeats']} pickup beat{'s' if a['pickupBeats'] != 1 else ''} before it)" if a['pickupBeats'] else ""))
    lines.append(f"   {len(a['beats'])} beats, {len(a['downbeats'])} bars")
    for s in a["sections"]:
        tempo = f"{s['bpm']:6.1f} BPM{'' if s['steady'] else '~'}" if s.get('bpm') else "    — BPM"
        lines.append(f"   [{s['label']}] {mmss(s['start'])}-{mmss(s['end'])}  bars {s['startBar']}-{s['startBar'] + s['bars'] - 1}"
                     f"  {tempo}  {s['level']:>4}  {s['loudnessDb']} dB" + (f"  change {s['change']}" if s['change'] is not None else ""))
    if a["hits"]:
        lines.append("   hits: " + ", ".join(f"{mmss(h['at'])}{'' if h['onBeat'] else '*'}" for h in a["hits"]) + "   (* off the beat)")
    if a["jumps"]:
        lines.append("   jumps: " + ", ".join(f"{j['kind']} {j['changeDb']:+} dB @ {mmss(j['at'])}" for j in a["jumps"]))
    if a["stops"]:
        lines.append("   stops: " + ", ".join(mmss(s["at"]) for s in a["stops"]))
    if a["barLoudnessDb"]:
        # One character per bar, loudest = 9: section levels are averages,
        # so a short loud tag or a 1-bar drop shows here, not there.
        L = a["barLoudnessDb"]
        # Scaled to the song's usual range (a silent last bar would flatten it).
        lo, hi = sorted(L)[len(L) // 10] - 1.0, max(L)
        strip = "".join(str(int(np.clip(round(9 * (v - lo) / (hi - lo + 1e-9)), 0, 9))) for v in L)
        lines.append("   loudness per bar (0 quiet .. 9 loud), bar 1 first:")
        for i in range(0, len(strip), 32):
            lines.append(f"     {i + 1:>4}: " + " ".join(strip[j:j + 4] for j in range(i, min(i + 32, len(strip)), 4)))
    return "\n".join(lines)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("tracks", nargs="+")
    ap.add_argument("--out", help="write the full analysis (JSON) here")
    ap.add_argument("--bpb", type=int, default=4, help="beats per bar (default 4; 3 for a waltz, 6 for 6/8)")
    ap.add_argument("--bpm-hint", type=float, help="roughly what tempo it is, if the guess is half/double")
    ap.add_argument("--min-section-bars", type=int, default=4)
    args = ap.parse_args()
    ffmpeg = find_ffmpeg()
    results = []
    for p in args.tracks:
        if not os.path.exists(p):
            sys.exit(f"no such file: {p}")
        a = analyze(p, ffmpeg, args.bpb, args.bpm_hint, args.min_section_bars)
        results.append(a)
        print(summary(a))
        print()
    if args.out:
        with open(args.out, "w", encoding="utf-8") as f:
            json.dump({"tracks": results}, f, indent=1)
        print(f"analysis written to {args.out}")


if __name__ == "__main__":
    main()
