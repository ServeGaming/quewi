# Drives simple-mode lighting triggers ("GO", "bump sub", desk command) end to
# end: this script is both the OSC remote and the fake lighting desk.
#
# Usage: run quewi with --selftest-idle, then `python tools/osc_desk_drive.py`.
#   QUEWI_OSC_PORT  quewi's OSC port (default 53000 — what a 2nd copy binds)
#   QUEWI_TEST_DIR  folder with tone.wav (see osc_triggers_drive.py)
# WARNING: it points Preferences → Lighting → desk at this script. It prints
# the desk settings it found so you can put them back (or delete the
# HKCU\Software\ServeGaming\quewi\lighting\desk key if there weren't any).
import json, os, socket, struct, sys, time

QUEWI = ("127.0.0.1", int(os.environ.get("QUEWI_OSC_PORT", "53000")))
TEST_DIR = os.environ.get("QUEWI_TEST_DIR", os.path.dirname(os.path.abspath(__file__)))
TONE = os.path.join(TEST_DIR, "tone.wav")

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind(("127.0.0.1", 0))
MYPORT = sock.getsockname()[1]


def pad(b): return b + b"\0" * (4 - len(b) % 4)


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
        j = p.index(b"\0", i); return p[i:j].decode(), (j + 4) & ~3
    addr, i = rstr(pkt, 0)
    if i >= len(pkt): return addr, []
    tags, i = rstr(pkt, i); out = []
    for t in tags[1:]:
        if t == "i": out.append(struct.unpack(">i", pkt[i:i+4])[0]); i += 4
        elif t == "f": out.append(round(struct.unpack(">f", pkt[i:i+4])[0], 3)); i += 4
        elif t == "d": out.append(struct.unpack(">d", pkt[i:i+8])[0]); i += 8
        elif t == "s": s, i = rstr(pkt, i); out.append(s)
        elif t in "TF": out.append(t == "T")
    return addr, out


def send(address, *args): sock.sendto(enc(address, *args), QUEWI)


def collect(seconds, t0=None):
    t0 = t0 or time.time(); got = []; sock.settimeout(0.05); end = time.time() + seconds
    while time.time() < end:
        try: pkt, _ = sock.recvfrom(65535)
        except socket.timeout: continue
        a, args = dec(pkt)
        if not a.startswith("/quewi/notify/cue/playback"): got.append((round(time.time() - t0, 3), a, args))
    return got


failed = False
def expect(name, cond):
    global failed
    print(("PASS " if cond else "FAIL ") + name)
    failed |= not cond


send("/quewi/query/lightingDesk")
r = [x for x in collect(0.5) if x[1] == "/quewi/reply/lightingDesk"]
expect("desk query replies", bool(r))
if r: print("   desk before:", r[0][2][0])

send("/quewi/lightingDesk/set", json.dumps({"type": "eos", "host": "127.0.0.1", "port": MYPORT}))
time.sleep(0.3)
send("/quewi/query/lightingDesk")
r = [x for x in collect(0.5) if x[1] == "/quewi/reply/lightingDesk"]
d = json.loads(r[0][2][0]) if r else {}
expect("desk now points here, Eos offers all 10 actions",
       d.get("port") == MYPORT and len(d.get("actions", [])) == 10)

send("/quewi/cue/add", "audio", 1.0, "Song"); time.sleep(0.3)
send("/quewi/cue/1/set/filePath", TONE); time.sleep(1.5)
for start, name in [(0.5, "go"), (1.0, "bump"), (1.5, "cmd"), (1.8, "custom")]:
    send("/quewi/cue/1/triggers/add", start, name)
time.sleep(0.4)
for ref, field, val in [
    ("go", "enter.kind", "desk"), ("go", "enter.do", "go"),
    ("bump", "enter.kind", "desk"), ("bump", "enter.do", "subBump"),
    ("bump", "enter.number", "3"), ("bump", "enter.hold", 0.2),
    ("cmd", "enter.kind", "desk"), ("cmd", "enter.do", "command"),
    ("cmd", "enter.text", "Chan 1 At Full"),
    # Custom OSC with no host/port of its own goes to the desk too.
    ("custom", "enter.kind", "osc"), ("custom", "enter.address", "/eos/key/back"),
]:
    send(f"/quewi/cue/1/trigger/{ref}/set/{field}", val)
time.sleep(0.5)
collect(0.2)

t0 = time.time()
send("/quewi/cue/start", 1.0)
r = collect(2.3, t0)
desk = [(t, a, args) for t, a, args in r if a.startswith("/eos/")]
for x in desk: print("   desk got:", x)
got = [(a, args) for _, a, args in desk]
expect("GO pressed then released", got[:2] == [("/eos/key/go_0", [1.0]), ("/eos/key/go_0", [0.0])])
expect("sub 3 bumped on then off", ("/eos/sub/3/fire", [1.0]) in got and ("/eos/sub/3/fire", [0.0]) in got)
expect("command line sent", ("/eos/newcmd", ["Chan 1 At Full Enter"]) in got)
expect("custom OSC with no host went to the desk", ("/eos/key/back", []) in got)
times = {a + str(args): t for t, a, args in desk}
if "/eos/sub/3/fire[1.0]" in times and "/eos/sub/3/fire[0.0]" in times:
    held = times["/eos/sub/3/fire[0.0]"] - times["/eos/sub/3/fire[1.0]"]
    expect(f"bump held ~0.2 s ({held:.3f})", 0.15 < held < 0.3)
send("/quewi/panic"); time.sleep(0.4); collect(0.2)

# Same trigger, grandMA3: GO becomes its command line "Go+".
send("/quewi/lightingDesk/type", "ma3"); time.sleep(0.3)
send("/quewi/cue/1/trigger/go/test")
r = collect(0.5)
expect("grandMA3 GO is /cmd Go+", any(a == "/cmd" and args == ["Go+"] for _, a, args in r))

print("RESULT:", "FAIL" if failed else "ALL PASS")
sys.exit(1 if failed else 0)
