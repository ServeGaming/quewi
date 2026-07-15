# Idea: distributed show nodes (host + workers)

Status: **idea only — not scheduled, not designed, nothing built.**
Captured 2026-07-15 from a conversation with Matthew. This file exists so the
idea doesn't evaporate; it is not a commitment or a plan.

## The pitch

Today quewi is one process on one machine doing everything. As the feature set
grows (audio cues, DCA mixing, projections, lighting), one machine and one pair
of eyes stops being enough.

The idea: **one host computer owns the show file. Other computers join it over
the network as nodes, and each node is given a job.**

- Host: holds the show file, the cue list, the master GO. Single source of truth.
- Node A: runs the audio tracks.
- Node B: runs the DCA / mixing surface.
- Node C: runs projections / video out.
- Node D: whatever comes next.

All of it driven from the host. Press GO once, every node does its part.

Wired Ethernet is the assumed transport (a show LAN is standard kit in this
world and it's what the consoles are already on). Wi-Fi is a nice-to-have and
should be treated as best-effort — nobody sane runs a show over hotel Wi-Fi,
but rehearsal and programming over Wi-Fi is reasonable.

## Why it's interesting

- **Horsepower.** Video decode and audio DSP stop fighting each other for CPU
  when they live on different machines.
- **Output count.** Each node brings its own audio interface and its own display
  outputs. Two machines = twice the outputs, no aggregate-device hackery.
- **Roles.** A real production has more than one operator. This maps a software
  architecture onto how the room already works.
- **Redundancy** (later, maybe). If the host is the only thing that matters, a
  warm-spare host is a plausible follow-on. Not v1.

## Related, from the same conversation

Multi-monitor within a single machine is the smaller, nearer version of this:
tracks on one monitor, DCA groups on another. That's a windowing problem, not a
distribution problem, and it should ship first and independently. If the panels
are already detachable to a second monitor, "detach to a second *machine*" is a
much shorter conceptual leap.

## Hard questions to answer before this is a design

These are the things that decide whether it's a weekend or a quarter:

1. **What actually crosses the wire?** Cue fires only (thin, host stays boss), or
   full state replication (fat, nodes can survive a host dropout)? Thin is much
   easier and probably right.
2. **Clock.** Do nodes need sample-accurate sync with each other, or is
   "within a few ms of the GO" fine? If two nodes both play audio into the same
   PA, drift is a real problem and this becomes a genuinely hard engineering
   task. If each node owns its own outputs, it's much easier.
3. **Media distribution.** Does every node need a local copy of the media, or
   does the host stream it? Local copies + a sync/verify step is the sane
   answer; streaming uncompressed audio over the show LAN is not.
4. **Discovery.** mDNS/Bonjour, or type in an IP? Start with typing an IP.
5. **Failure.** Node drops mid-show — what does the operator see, and what
   happens to the cue that was running on it? This is the question that
   separates a demo from a tool anyone will run a paying show on.
6. **Does it reuse OSC?** quewi already has a real OSC surface with subscribe/
   notify. A node link is arguably just an OSC client with a fancier handshake.
   Tempting. Possibly too tempting — OSC over UDP has no delivery guarantee,
   and "the GO didn't arrive" is not an acceptable failure mode.

## Prior art worth a look

QLab does this today. Its answer is roughly: cues on the host fire OSC at other
machines, plus a separate "Sync"/redundancy story that people mostly use for
backup rather than role-splitting. Worth understanding what they got right and
what people complain about before designing ours.
