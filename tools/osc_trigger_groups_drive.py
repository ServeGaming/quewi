# Usage: run quewi with --selftest-idle (OSC on 53000, or set QUEWI_OSC_PORT),
# then `python tools/osc_trigger_groups_drive.py`. Needs tone.wav next to this
# script or in QUEWI_TEST_DIR (see osc_triggers_drive.py for how to make it).
"""Drive the trigger-group OSC addresses end to end.

A group of four beats is added, edited as a group (one address sets every
member), shifted, played (this socket is the desk, so the hits land here),
and removed. The group name has a space, sent percent-encoded.
"""
import json, os, socket, struct, time

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
        return p[i:j].decode(), (j + 4) & ~3
    addr, i = rstr(pkt, 0)
    if i >= len(pkt): return addr, []
    tags, i = rstr(pkt, i)
    out = []
    for t in tags[1:]:
        if t == "i": out.append(struct.unpack(">i", pkt[i:i+4])[0]); i += 4
        elif t == "f": out.append(round(struct.unpack(">f", pkt[i:i+4])[0], 4)); i += 4
        elif t == "d": out.append(struct.unpack(">d", pkt[i:i+8])[0]); i += 8
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
        if not a.startswith("/quewi/notify/"):
            got.append((round(time.time() - t0, 3), a, args))
    return got


def triggers():
    collect(0.1)
    send("/quewi/cue/1/triggers/list")
    r = [x for x in collect(0.5) if x[1] == "/quewi/reply/cue/triggers"]
    return json.loads(r[0][2][0])["triggers"] if r else None


failed = False
def expect(name, cond):
    global failed
    print(("PASS " if cond else "FAIL ") + name)
    failed |= not cond


send("/quewi/cue/add", "audio", 1.0, "Song"); time.sleep(0.3)
send("/quewi/cue/1/set/filePath", TONE); time.sleep(1.5)
collect(0.2)

# Four beats in "Chorus Bumps" plus one loner, added fully specified.
for i, s in enumerate([0.5, 1.0, 1.5, 2.0]):
    send("/quewi/cue/1/triggers/add", json.dumps(
        {"name": f"Beat {i+1}", "start": s, "group": "Chorus Bumps",
         "enter": {"kind": "desk", "do": "subBump", "number": str(i + 1)}}))
send("/quewi/cue/1/triggers/add", json.dumps({"name": "Loner", "start": 3.0}))
time.sleep(0.4)
t = triggers()
expect("five added, four in the group",
       t is not None and len(t) == 5 and sum(x.get("group") == "Chorus Bumps" for x in t) == 4)

# One address edits every member: send them to this socket as custom OSC.
G = "/quewi/cue/1/triggers/group/Chorus%20Bumps"
for field, val in [("enter.kind", "osc"), ("enter.host", "127.0.0.1"),
                   ("enter.port", MYPORT), ("enter.address", "/eos/sub/5/fire"),
                   ("enter.args", "1.0")]:
    send(f"{G}/set/{field}", val)
time.sleep(0.4)
t = triggers()
grp = [x for x in t if x.get("group") == "Chorus Bumps"]
expect("group set reached all four",
       all(x["enter"]["kind"] == "osc" and x["enter"]["address"] == "/eos/sub/5/fire" for x in grp))
expect("loner untouched", [x for x in t if x["name"] == "Loner"][0]["enter"]["kind"] == "none")

# Shift the group a quarter second later.
send(f"{G}/shift", 0.25); time.sleep(0.4)
t = triggers()
starts = sorted(x["start"] for x in t if x.get("group") == "Chorus Bumps")
expect("shifted by 0.25", [round(s, 3) for s in starts] == [0.75, 1.25, 1.75, 2.25])

# Play: the four hits land here at the shifted times.
t0 = time.time()
send("/quewi/cue/start", 1.0)
r = collect(2.8, t0)
hits = [x[0] for x in r if x[1] == "/eos/sub/5/fire"]
print("   hits at:", hits)
expect("four hits", len(hits) == 4)
if len(hits) == 4:
    expect("on time (within 120 ms)",
           all(abs(h - w) < 0.12 for h, w in zip(hits, [0.75, 1.25, 1.75, 2.25])))
send("/quewi/panic"); time.sleep(0.4); collect(0.3)

# Rename the group with set/group, then remove it by its new name.
send(f"{G}/set/group", "Verse"); time.sleep(0.4)
t = triggers()
expect("renamed", sum(x.get("group") == "Verse" for x in t) == 4)
send("/quewi/cue/1/triggers/group/Verse/remove"); time.sleep(0.4)
t = triggers()
expect("group removed, loner kept", t is not None and [x["name"] for x in t] == ["Loner"])

# Undo brings it back (one step).
send("/quewi/undo"); time.sleep(0.4)
t = triggers()
expect("undo restores the group", t is not None and len(t) == 5)

print("ALL PASS" if not failed else "SOME FAILED")
