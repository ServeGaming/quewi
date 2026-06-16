#!/usr/bin/env python3
"""Tiny OSC probe: send a few /quewi messages to a running quewi and print
any replies. No deps — raw UDP + hand-rolled OSC 1.0 encoding/decoding.
Usage: python osc_probe.py [host] [port]
"""
import socket
import sys
import time

HOST = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1"
PORT = int(sys.argv[2]) if len(sys.argv) > 2 else 53535


def pad(b: bytes) -> bytes:
    return b + b"\x00" * ((4 - len(b) % 4) % 4)


def osc_msg(addr: str, *args) -> bytes:
    out = pad(addr.encode() + b"\x00")
    tags = ","
    body = b""
    for a in args:
        if isinstance(a, int):
            tags += "i"
            body += a.to_bytes(4, "big", signed=True)
        elif isinstance(a, float):
            import struct
            tags += "f"
            body += struct.pack(">f", a)
        elif isinstance(a, str):
            tags += "s"
            body += pad(a.encode() + b"\x00")
    out += pad(tags.encode() + b"\x00") + body
    return out


def decode(data: bytes):
    """Decode a flat OSC message → (address, [args]). Best-effort."""
    z = data.index(b"\x00")
    addr = data[:z].decode(errors="replace")
    i = (z + 4) & ~3
    if i >= len(data) or data[i:i + 1] != b",":
        return addr, []
    z2 = data.index(b"\x00", i)
    tags = data[i + 1:z2].decode(errors="replace")
    i = (z2 + 4) & ~3
    args = []
    import struct
    for t in tags:
        if t == "i":
            args.append(int.from_bytes(data[i:i + 4], "big", signed=True)); i += 4
        elif t == "f":
            args.append(struct.unpack(">f", data[i:i + 4])[0]); i += 4
        elif t == "s":
            z3 = data.index(b"\x00", i)
            args.append(data[i:z3].decode(errors="replace"))
            i = (z3 + 4) & ~3
    return addr, args


s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.bind(("0.0.0.0", 0))
myport = s.getsockname()[1]
s.settimeout(3.0)
print(f"probe: sending to {HOST}:{PORT}, listening for replies on :{myport}")

# Queries that should each produce a reply (proves receive + dispatch + reply).
for addr in ("/quewi/query/version", "/quewi/query/showName",
             "/quewi/query/workspace"):
    s.sendto(osc_msg(addr), (HOST, PORT))
    try:
        data, src = s.recvfrom(8192)
        a, args = decode(data)
        print(f"  {addr:28s} -> {a}  {args}")
    except socket.timeout:
        print(f"  {addr:28s} -> NO REPLY (timeout)")

# A fire-and-forget command (no reply) just to exercise the inbound path.
s.sendto(osc_msg("/quewi/heartbeat"), (HOST, PORT))
print("  /quewi/heartbeat             -> sent (no reply expected)")
s.close()
