# Usage: run quewi with --selftest-idle (OSC on 53535, or 53000 if another
# quewi holds 53535 — set QUEWI_OSC_PORT), then `python tools/osc_triggers_drive.py`.
# Test media (made with ffmpeg) next to this script, or point QUEWI_TEST_DIR at them:
#   ffmpeg -f lavfi -i "sine=frequency=440:duration=20" -ac 2 tone.wav
#   ffmpeg -f lavfi -i "color=c=navy:s=320x180:d=6" -f lavfi -i "sine=frequency=330:duration=6" \
#          -c:v libx264 -pix_fmt yuv420p -c:a aac -shortest test-song.mp4
"""Drive quewi's v5 OSC API end to end against a test copy on UDP 53000.

One socket is the remote (HeliOSC stand-in), the subscriber, AND the lighting
desk the triggers send to, so every packet lands here with a timestamp.
"""
import json, os, socket, struct, sys, time

QUEWI = ("127.0.0.1", int(os.environ.get("QUEWI_OSC_PORT", "53000")))
TEST_DIR = os.environ.get("QUEWI_TEST_DIR", os.path.dirname(os.path.abspath(__file__)))
TONE = os.path.join(TEST_DIR, "tone.wav")

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind(("127.0.0.1", 0))
MYPORT = sock.getsockname()[1]


def pad(b):
    return b + b"\0" * (4 - len(b) % 4)


def enc(address, *args):
    tags, data = ",", b""
    for a in args:
        if a is True: tags += "T"
        elif a is False: tags += "F"
        elif isinstance(a, int): tags += "i"; data += struct.pack(">i", a)
        elif isinstance(a, float): tags += "f"; data += struct.pack(">f", a)
        else: tags += "s"; data += pad(str(a).encode())
    return pad(address.encode()) + pad(tags.encode()) + data


def dec(pkt):
    def rstr(p, i):
        j = p.index(b"\0", i)
        s = p[i:j].decode()
        return s, (j + 4) & ~3
    addr, i = rstr(pkt, 0)
    if i >= len(pkt): return addr, []
    tags, i = rstr(pkt, i)
    out = []
    for t in tags[1:]:
        if t == "i": out.append(struct.unpack(">i", pkt[i:i+4])[0]); i += 4
        elif t == "f": out.append(round(struct.unpack(">f", pkt[i:i+4])[0], 4)); i += 4
        elif t == "d": out.append(struct.unpack(">d", pkt[i:i+8])[0]); i += 8
        elif t == "h": out.append(struct.unpack(">q", pkt[i:i+8])[0]); i += 8
        elif t == "s": s, i = rstr(pkt, i); out.append(s)
        elif t == "T": out.append(True)
        elif t == "F": out.append(False)
    return addr, out


def send(address, *args):
    sock.sendto(enc(address, *args), QUEWI)


def collect(seconds, t0=None):
    t0 = t0 or time.time()
    got = []
    sock.settimeout(0.05)
    end = time.time() + seconds
    while time.time() < end:
        try:
            pkt, _ = sock.recvfrom(65535)
        except socket.timeout:
            continue
        a, args = dec(pkt)
        if a.startswith("/quewi/notify/cue/playback"):
            continue
        got.append((round(time.time() - t0, 3), a, args))
    return got


def expect(name, cond):
    print(("PASS " if cond else "FAIL ") + name)
    if not cond: expect.failed = True
expect.failed = False

# Fresh state: one audio cue playing the tone.
send("/quewi/cue/add", "audio", 1.0, "Song"); time.sleep(0.3)
send("/quewi/cue/1/set/filePath", TONE); time.sleep(1.5)
collect(0.2)

# Two triggers: a hit at 0.5 s, a chorus range 1.0–2.0 s.
send("/quewi/cue/1/triggers/add", 0.5, "hit")
send("/quewi/cue/1/triggers/add", 1.0, 2.0, "chorus")
r = collect(0.5)
added = [x for x in r if x[1] == "/quewi/reply/cue/triggers/added"]
expect("add replies with id + index", len(added) == 2 and added[1][2][1] == 1)

for ref, field, val in [
    ("hit", "enter.kind", "osc"), ("hit", "enter.port", MYPORT),
    ("hit", "enter.address", "/eos/cue/1/1/fire"),
    ("chorus", "enter.kind", "osc"), ("chorus", "enter.port", MYPORT),
    ("chorus", "enter.address", "/eos/key/go_0"),
    ("chorus", "exit.kind", "osc"), ("chorus", "exit.port", MYPORT),
    ("chorus", "exit.address", "/eos/sub/1"), ("chorus", "exit.args", "0.0"),
]:
    send(f"/quewi/cue/1/trigger/{ref}/set/{field}", val)
time.sleep(0.4)
send("/quewi/cue/1/triggers/list")
r = collect(0.5)
lst = [x for x in r if x[1] == "/quewi/reply/cue/triggers"]
expect("list replies", len(lst) == 1)
if lst:
    j = json.loads(lst[0][2][0])
    print("   list:", json.dumps(j)[:400])
    expect("list has both, sorted, with summaries",
           [t["name"] for t in j["triggers"]] == ["hit", "chorus"]
           and j["triggers"][1]["range"] is True
           and "summary" in j["triggers"][1]["exit"])

# Subscribe, then GO the song and watch what the "desk" receives.
send("/quewi/subscribe"); collect(0.3)
t0 = time.time()
send("/quewi/cue/start", 1.0)
r = collect(2.6, t0)
desk = [(t, a, args) for t, a, args in r if a.startswith("/eos/")]
notes = [(t, args[3], args[4]) for t, a, args in r if a == "/quewi/notify/trigger/fired"]
print("   desk:", desk)
print("   notify:", notes)
expect("desk got hit, go, sub in order",
       [a for _, a, _ in desk] == ["/eos/cue/1/1/fire", "/eos/key/go_0", "/eos/sub/1"])
if len(desk) == 3:
    expect("timing ~0.5/1.0/2.0 s (within 120 ms)",
           abs(desk[0][0] - 0.5) < 0.12 and abs(desk[1][0] - 1.0) < 0.12 and abs(desk[2][0] - 2.0) < 0.12)
    expect("exit arg typed as float 0.0", desk[2][2] == [0.0])
expect("notify fired x3 (enter, enter, exit)",
       notes == [(n[0], "hit", "enter") for n in notes[:1]] + notes[1:] and
       [(x[1], x[2]) for x in notes] == [("hit", "enter"), ("chorus", "enter"), ("chorus", "exit")])
send("/quewi/panic"); time.sleep(0.5); collect(0.3)

# Disarmed: the song plays, nothing goes to the desk.
send("/quewi/triggers/armed", False)
r = collect(0.4)
expect("armed F replies F", any(a == "/quewi/reply/triggers/armed" and args == [False] for _, a, args in r))
send("/quewi/cue/start", 1.0)
r = collect(1.4)
expect("disarmed sends nothing", not any(a.startswith("/eos/") for _, a, _ in r))
send("/quewi/panic"); time.sleep(0.3)
send("/quewi/triggers/armed", True); collect(0.4)

# Test verb sends right now, and the exit edge on request.
send("/quewi/cue/1/trigger/1/test", "exit")
r = collect(0.4)
expect("test exit sends /eos/sub/1", any(a == "/eos/sub/1" for _, a, _ in r))

# Undo the last trigger edit, then remove + clear.
send("/quewi/cue/1/trigger/chorus/remove"); time.sleep(0.3)
send("/quewi/cue/1/triggers/list")
r = collect(0.4)
j = json.loads([x for x in r if x[1] == "/quewi/reply/cue/triggers"][0][2][0])
expect("remove leaves one", [t["name"] for t in j["triggers"]] == ["hit"])
send("/quewi/undo"); time.sleep(0.3)
send("/quewi/cue/1/triggers/list")
r = collect(0.4)
j = json.loads([x for x in r if x[1] == "/quewi/reply/cue/triggers"][0][2][0])
expect("undo brings chorus back", [t["name"] for t in j["triggers"]] == ["hit", "chorus"])

# Soundboard mic query.
send("/quewi/query/soundboard/mic")
r = collect(0.6)
mic = [x for x in r if x[1] == "/quewi/reply/soundboard/mic"]
expect("mic query replies JSON with device lists", bool(mic) and "outputs" in json.loads(mic[0][2][0]))
if mic:
    m = json.loads(mic[0][2][0])
    print("   mic:", {k: v for k, v in m.items() if k not in ("outputs", "inputs")},
          len(m["outputs"]), "outputs")

send("/quewi/unsubscribe")
print("RESULT:", "FAIL" if expect.failed else "ALL PASS")
sys.exit(1 if expect.failed else 0)
