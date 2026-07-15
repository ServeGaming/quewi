#!/usr/bin/env python3
"""Yamaha DM7 RCP capability probe.

    python dm7_probe.py <DM7_IP>

Writes everything to dm7_session.log. Point it at the console's
SETUP -> NETWORK -> "For Mixer Control" IP.

WHY THIS EXISTS
---------------
Every third-party parameter table for Yamaha consoles is a firmware-stamped
snapshot, and we proved they under-report: the published table shows no
DCA-assign on TF, yet TheatreMix ships TF support at firmware >=4.00. Absence
in a dump is not evidence of absence on the console.

This script gets ground truth from the actual desk on its actual firmware.
See docs/dev/console-protocols.md.

SAFETY
------
All writes target Input Ch 1 and DCA 1 only, and are restored afterwards.
Nothing here stores a scene. Even so: run this on a rehearsal or blank scene,
NOT on a show file.
"""

import socket
import sys
import time

if len(sys.argv) < 2:
    sys.exit(__doc__)

HOST, PORT = sys.argv[1], 49280
LOG = open("dm7_session.log", "w", encoding="utf-8")


def log(s):
    print(s)
    LOG.write(s + "\n")
    LOG.flush()


sock = socket.create_connection((HOST, PORT), timeout=5)
buf = ""


def drain(wait=0.4):
    """Collect all complete LF-terminated lines currently available."""
    global buf
    out, end = [], time.time() + wait
    sock.settimeout(0.15)
    while time.time() < end:
        try:
            d = sock.recv(65536).decode("utf-8", "replace")
            if not d:
                break
            buf += d
            while "\n" in buf:
                line, buf = buf.split("\n", 1)
                if line.strip():
                    out.append(line.strip())
            end = time.time() + wait      # extend the window on activity
        except socket.timeout:
            pass
    return out


def cmd(c, wait=0.4):
    log(f"\n>>> {c}")
    sock.sendall((c + "\n").encode())
    for r in drain(wait):
        log(f"<<< {r}")


# ---------------------------------------------------------- A: identity
log("=" * 70 + "\n TEST A - identity / firmware / protocol version\n" + "=" * 70)
for c in ["devinfo productname", "devinfo devicename", "devinfo version",
          "devinfo paramsetver", "devinfo protocolver", "devinfo serialno",
          "devinfo manufacturer", "devinfo category", "devstatus runmode",
          "devstatus error"]:
    cmd(c)
# EXPECT: OK devinfo productname "DM7"  /  OK devinfo version "1.60"
# devinfo version + paramsetver are the architectural answer to stale tables:
#   gate our capability profile on them at runtime instead of hardcoding.
# devstatus error: OK => supported (Companion's exclusion is over-cautious)
#                  ERROR devstatus UnknownCommand => genuinely unsupported

# ---------------------------------------------------------- B: SELF-DESCRIPTION
log("\n" + "=" * 70 + "\n TEST B - prmnum / prminfo self-description  (THE BIG ONE)\n" + "=" * 70)
cmd("prmnum")
cmd("mtrnum")
# EXPECT (hoped): OK prmnum 218   -> real table size; the console enumerates itself
#        (maybe): OK prmnum 1000  -> DME7-style fixed cap; enumerate anyway
#        (bad):   ERROR prmnum UnknownCommand -> not supported on consoles;
#                 fall back to the tables in docs/dev/console-protocols.md
cmd("prminfo 0")
cmd("prminfo 18")     # EXPECT: OK prminfo 18 "MIXER:Current/InCh/DCA/Assign" 120 24 0 1 0 "" integer any rw 1
cmd("prminfo 217")
cmd("mtrinfo 2000")
cmd("scninfo 1000")   # may ERROR - scninfo is unverified, possibly Companion-invented
cmd("ssnum_ex")       # DME7-style alternative

log("\n--- FULL TABLE DUMP ATTEMPT (0..2200) ---")
for i in list(range(0, 400)) + list(range(1000, 1020)) + list(range(2000, 2200)):
    sock.sendall(f"prminfo {i}\n".encode())
    time.sleep(0.006)
    if i % 50 == 0:
        for r in drain(0.2):
            log(r)
for r in drain(2.0):
    log(r)
# Every "OK prminfo ..." line here is CURRENT-FIRMWARE GROUND TRUTH.
# Watch indices 101 and 122-155 especially - those are holes in the published
# table, and dynamics/delay are the hypothesis for what lives there.

# ---------------------------------------------------------- C: DCA assign
log("\n" + "=" * 70 + "\n TEST C - DCA assign round-trip (ch1 -> DCA1)\n" + "=" * 70)
cmd("get MIXER:Current/InCh/DCA/Assign 0 0")     # EXPECT: OK get ... 0 0 <0|1>
cmd("set MIXER:Current/InCh/DCA/Assign 0 0 1")
cmd("get MIXER:Current/InCh/DCA/Assign 0 0")     # EXPECT: OK get ... 0 0 1
# >>> LOOK AT THE SURFACE. Ch1 must now be in DCA1. If the reply says OK but
#     nothing moved, the address is accepted-but-inert - that matters.
cmd("get MIXER:Current/InCh/DCA/Assign 0 23")    # DCA24 - valid (Y max)
cmd("get MIXER:Current/InCh/DCA/Assign 0 24")    # EXPECT: ERROR (out of range)
cmd("get MIXER:Current/InCh/DCA/Assign 119 0")   # DM7 => OK ; DM7 Compact => ERROR
cmd("set MIXER:Current/InCh/DCA/Assign 0 0 0")   # restore

# ---------------------------------------------------------- D: mute polarity
log("\n" + "=" * 70 + "\n TEST D - channel-on + mute-group polarity\n" + "=" * 70)
cmd("get MIXER:Current/InCh/Fader/On 0 0")       # EXPECT: OK get ... 0 0 1 (unmuted)
cmd("set MIXER:Current/InCh/Fader/On 0 0 0")
# >>> SURFACE: Ch1 ON key should go DARK. Confirms 0 = muted.
cmd("set MIXER:Current/InCh/Fader/On 0 0 1")     # restore
cmd("get MIXER:Current/MuteGrpCtrl/On 0 0")
cmd("set MIXER:Current/MuteGrpCtrl/On 0 0 1")
# >>> SURFACE: does Mute Group 1 ENGAGE (channels mute) or RELEASE?
#     Engage => 1 = mute active, i.e. OPPOSITE polarity to Fader/On.
#     THIS IS THE ANSWER. Getting it backwards mutes the cast mid-show.
cmd("set MIXER:Current/MuteGrpCtrl/On 0 0 0")    # restore
cmd("get MIXER:Current/MuteGrpCtrl/Label/Name 0 0")

# ---------------------------------------------------------- E: scaling
log("\n" + "=" * 70 + "\n TEST E - EQ / HPF / HA scaling (three sources disagree!)\n" + "=" * 70)
cmd("get MIXER:Current/InCh/PEQ/Band/Gain 0 0")
cmd("set MIXER:Current/InCh/PEQ/Band/Gain 0 0 600")
# >>> SURFACE: Ch1 EQ band 1 gain reads
#       +6.00 dB => scaling 100  (physics says this; most likely)
#       +60.0 dB => scaling 10   (Yamaha's own official OSC spec claims this)
#       +600     => scaling 1    (the Companion dump claims this)
cmd("get MIXER:Current/InCh/PEQ/Band/Gain 0 0")
cmd("set MIXER:Current/InCh/PEQ/Band/Gain 0 0 0")      # restore
cmd("get MIXER:Current/InCh/PEQ/Band/Q 0 0")
cmd("set MIXER:Current/InCh/PEQ/Band/Q 0 0 4000")      # Q=4.0 => scaling 1000
cmd("get MIXER:Current/InCh/PEQ/Type 0 0")             # quoted "PRECISE"? => string
cmd('set MIXER:Current/InCh/PEQ/Type 0 0 "SMOOTH"')
cmd("set MIXER:Current/InCh/HPF/Freq 0 0 800")         # => 80.0 Hz => scaling 10
cmd("set MIXER:Current/InCh/HPF/Freq 0 0 100000")      # 10 kHz: OK => max 200000
                                                       #         ERROR => max 20000
cmd("set MIXER:Current/InCh/Port/HA/Gain 0 0 3000")    # => +30.00 dB => scaling 100
cmd("set MIXER:Current/InCh/Port/HA/Gain 0 0 30")      # => +30 dB    => scaling 1

# ---------------------------------------------------------- F: ABSENCE PROBE
log("\n" + "=" * 70 + "\n TEST F - do 'absent' params actually exist?  (the TF lesson)\n" + "=" * 70)
for a in ["MIXER:Current/InCh/Dyna1/Threshold", "MIXER:Current/InCh/Dyna1/Type",
          "MIXER:Current/InCh/Dyna1/On", "MIXER:Current/InCh/Dyna1/Ratio",
          "MIXER:Current/InCh/Dyna1/Attack", "MIXER:Current/InCh/Dyna1/Release",
          "MIXER:Current/InCh/Dyna1/Knee", "MIXER:Current/InCh/Dyna2/Threshold",
          "MIXER:Current/InCh/Dyna2/Ratio", "MIXER:Current/InCh/Gate/Threshold",
          "MIXER:Current/InCh/Delay/On", "MIXER:Current/InCh/Delay/Time",
          "MIXER:Current/InCh/Insert/On", "MIXER:Current/InCh/DigitalGain",
          "MIXER:Current/InCh/Port/HA/Phantom", "MIXER:Current/InCh/Phase"]:
    cmd(f"get {a} 0 0", wait=0.25)
# EXPECT per line: OK get <addr> 0 0 <val>  => EXISTS (the table was incomplete)
#                  ERROR get UnknownAddress => genuinely absent on this firmware
# These decide whether channel profiles (spec phase 4) are possible on DM7.

# ---------------------------------------------------------- G: colours + names
log("\n" + "=" * 70 + "\n TEST G - colour set + name length\n" + "=" * 70)
cmd("get MIXER:Current/InCh/Label/Color 0 0")
# Note whether the reply is  ... 0 0 <int> "Blue"  (Val + TxtVal) or just a string.
for c in ["Blue", "Green", "Orange", "Pink", "Purple", "Red",
          "SkyBlue", "Yellow", "Cyan", "Magenta", "Off"]:
    cmd(f'set MIXER:Current/InCh/Label/Color 0 0 "{c}"', wait=0.2)
# EXPECT all 11 OK (official Table 3). Any ERROR => the palette differs.
cmd('set MIXER:Current/InCh/Label/Name 0 0 "ABCDEFGHIJKL"')   # 12 chars
cmd("get MIXER:Current/InCh/Label/Name 0 0")
# Truncated to "ABCDEFGH" => max 8 (official). Full 12 back => the dump's 64 wins.
cmd('set MIXER:Current/InCh/Label/Name 0 0 "ch 1"')           # restore
cmd("get MIXER:Current/DCA/Label/Name 0 0")
cmd("get MIXER:Current/DCA/Label/Color 0 0")

# ---------------------------------------------------------- H: metering
log("\n" + "=" * 70 + "\n TEST H - metering\n" + "=" * 70)
cmd("mtrstart MIXER:Current/InCh/PreFader 100", wait=2.0)
# EXPECT a stream: NOTIFY mtr MIXER:Current/InCh/PreFader <name> <hex> <hex> ...
# Speak into Ch1 and watch the first hex value move.
# Calibrate with a known -20 dBFS tone to validate the hex -> dB mapping.
log("--- waiting 12 s to see whether the meter subscription EXPIRES (~10 s) ---")
for r in drain(12.0):
    log(r)
# If the stream stops near 10 s, we must re-arm with mtrstart periodically.

# ---------------------------------------------------------- I: NOTIFY capture
log("\n" + "=" * 70 + "\n TEST I - NOTIFY capture (30 s)\n" + "=" * 70)
log(">>> NOW, ON THE CONSOLE:")
log("    1. move Ch1 fader")
log("    2. press Ch1 ON key")
log("    3. change a DCA assign in the DCA ASSIGN screen   <-- THE CRITICAL ONE")
log("    4. rename a DCA")
log("    5. recall a scene")
log("    6. connect DM7 Editor    <-- does this RCP link survive? (the slot question)")
for r in drain(30.0):
    log(r)
# EXPECT: NOTIFY set MIXER:Current/InCh/Fader/Level 0 0 -1234
#         NOTIFY set MIXER:Current/InCh/DCA/Assign 0 0 1   <-- live DCA capture works
#         NOTIFY sscurrentt_ex scene_a "4.00" modified
# Our OWN writes echo as "OK set ..." - the Status field is what breaks feedback
# loops in the capture engine.
# If connecting the Editor kills this socket, RCP DOES consume an Editor slot.

sock.close()
LOG.close()
print("\nDone -> dm7_session.log")
