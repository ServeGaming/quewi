# Usage: run quewi with --selftest-idle (OSC on 53535, or 53000 if another
# quewi holds 53535 — set QUEWI_OSC_PORT), then `python tools/osc_triggers_video.py`.
# Test media (made with ffmpeg) next to this script, or point QUEWI_TEST_DIR at them:
#   ffmpeg -f lavfi -i "sine=frequency=440:duration=20" -ac 2 tone.wav
#   ffmpeg -f lavfi -i "color=c=navy:s=320x180:d=6" -f lavfi -i "sine=frequency=330:duration=6" \
#          -c:v libx264 -pix_fmt yuv420p -c:a aac -shortest test-song.mp4
import json, os, time, sys
HERE = os.path.dirname(os.path.abspath(__file__))
__file__ = os.path.join(HERE, "osc_triggers_drive.py")
src = open(__file__, encoding="utf-8").read().split("# Fresh state")[0]
exec(src)
def expect(name, cond):
    print(("PASS " if cond else "FAIL ") + name)
    if not cond: expect.failed = True
expect.failed = False
VID = os.path.join(TEST_DIR, "test-song.mp4")

def listing(num):
    send(f"/quewi/cue/{num}/triggers/list")
    r = collect(0.5)
    x = [a for a in r if a[1] == "/quewi/reply/cue/triggers"]
    return json.loads(x[0][2][0]) if x else None

send("/quewi/subscribe"); collect(0.3)
send("/quewi/cue/add", "video", 2.0, "Video song"); time.sleep(0.3)
send("/quewi/cue/2/set/filePath", VID); time.sleep(2.0)
send("/quewi/cue/2/triggers/add", 0.5, "vhit")
time.sleep(0.3)
for f, v in [("enter.kind", "osc"), ("enter.port", MYPORT), ("enter.address", "/eos/macro/1/fire")]:
    send(f"/quewi/cue/2/trigger/0/set/{f}", v)
time.sleep(0.4)
j = listing(2)
expect("video cue lists its triggers", j and j["type"] == "video" and j["triggers"][0]["name"] == "vhit")

def go_and_watch(label):
    collect(0.2)
    t0 = time.time(); send("/quewi/cue/start", 2.0)
    r = collect(1.3, t0)
    desk = [(t, a) for t, a, _ in r if a.startswith("/eos/")]
    print("  ", label, desk)
    send("/quewi/panic"); time.sleep(0.5); collect(0.3)
    return desk

d = go_and_watch("video w/ sound")
expect("video soundtrack sends its trigger ~0.5 s", len(d) == 1 and abs(d[0][0] - 0.5) < 0.15)

send("/quewi/cue/2/set/soundEnabled", False); time.sleep(0.4)
d = go_and_watch("silent video")
expect("silent video still sends (picture clock, within 250 ms)", len(d) == 1 and abs(d[0][0] - 0.5) < 0.25)
send("/quewi/cue/2/set/soundEnabled", True); time.sleep(0.4)

send("/quewi/cue/2/convert"); time.sleep(0.8)
j = listing(2)
expect("convert -> audio keeps triggers", j and j["type"] == "audio" and len(j["triggers"]) == 1)
d = go_and_watch("converted audio")
expect("converted cue sends", len(d) == 1)
send("/quewi/cue/2/convert"); time.sleep(0.8)
j = listing(2)
expect("convert back -> video keeps triggers", j and j["type"] == "video" and len(j["triggers"]) == 1)

send("/quewi/unsubscribe")
print("RESULT:", "FAIL" if expect.failed else "ALL PASS")
