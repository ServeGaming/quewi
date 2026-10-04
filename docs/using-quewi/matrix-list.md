# Matrix List

*New in the next release (not in 1.1.0).*

A Matrix List is the whole show in one running order: quewi's sound and
video cues and your lighting desk's cues, interleaved, one row per
moment. It reads the desk's cue list straight off an ETC Eos-family desk
(Eos, Ion, Element, Nomad), so you see every lighting cue, with its
label, time, notes and scene, next to the quewi cue it goes with, the
way you'd read a prompt script.

It's a view, not a second show. quewi's cues stay in their own cue list
and the lighting cues stay on the desk. Nothing on the desk is ever
changed: quewi only asks it questions.

---

## Making one

1. Set up your desk once, if you haven't: **Tools → Lighting Desk…**,
   choose **ETC Eos / Ion / Element / Nomad**, type its IP address, and
   leave **Read the desk's state back** on. (quewi talks to the desk on
   its OSC TCP port, 3032, which Eos allows by default. There's nothing
   to set up on the desk.)
2. Choose **List → Matrix List (sound + lights)** (also under
   **View**).
3. At the top of the page, choose:
    - **Sound & video from**: the quewi cue list to interleave (it starts
      as the one you were on);
    - **Lights from desk cue list**: the desk's cue list. The menu lists
      the desk's cue lists once it's connected; you can also type a number.

quewi reads the desk's list and fills the Lights column. A desk answers
about ten cues a second, so a 300-cue list takes half a minute the first
time. The line under the settings shows the progress, and the page is
usable while it reads.

---

## Reading it

| Column | Shows |
|---|---|
| (state) | **STANDBY** (what GO fires next), **PLAYING**, **LX LIVE** (the desk's running cue), **LX NEXT** (the desk's pending cue), or ⚠ for a lighting cue that isn't on the desk. |
| **Q** | The quewi cue number. |
| **Sound / Video** | The quewi cue. A lighting cue hit part-way through a song sits on its own row underneath, indented: "↳ hit at 0:30 · Chorus". |
| **Lights** | The desk cue(s) for this moment: "LX 12  Sunrise". A multipart cue says how many parts it has. |
| **Time** | The desk cue's up / down time, and its follow (F) or hang (H) if it has one. |
| **Notes / Scene** | The quewi cue's notes, then the desk cue's scene ("Scene: Act 1") and notes. |

The colours match Show Mode: **amber** for standby, **green** for what's
playing, **blue** for the desk's own cues and **lavender** for lighting
hits inside a song.

**Follow the show** (on by default) keeps standby, or the desk's live
cue, in view as the show runs.

---

## Where each lighting cue goes

quewi places every desk cue by the first of these that applies:

1. **You put it there.** Drag a lighting cue onto a quewi cue to tie it
   to that cue (same row), or drop it between two rows to give it a row of
   its own after the quewi cue above. Drop it above the first cue to put
   it at the very top. Your placements are saved with the show.
2. **quewi fires it at GO.** A cue that sends the desk to that cue puts
   it on the same row: a lighting trigger at the top of a song set to
   **Go to cue**, an OSC cue sending `/eos/cue/<list>/<cue>/fire` (or
   `/eos/cue/<cue>/fire`, or a `Go_To_Cue` command line), or an MSC GO with
   that Q number.
3. **quewi hits it in a song.** A lighting trigger part-way through a song
   gives the desk cue a row under the song, at its time.
4. **By order.** Nothing in quewi fires it, so it follows the desk cue
   before it: it gets its own row after the quewi cue that one is tied to.
   Desk cues before anything tied go at the top.

So the more of your lighting quewi fires, the less you have to place by
hand. For the rest, a few drags early on usually settle the whole act,
because every untied cue after a tied one follows it.

To undo a placement, right-click the row and pick **Put LX 12 back
automatically**.

!!! note "When the desk changes"
    The desk tells quewi when its cues change, and quewi reads just those
    cues again. If the desk **renumbers** a cue you placed, your placement
    follows it (quewi recognises the cue by its ID, not only its number).
    If a cue you placed is **deleted** on the desk, its row stays where you
    put it, marked ⚠ "not on the desk", so you can see what went. Right-click
    it to forget it. A quewi cue that fires a desk cue the desk doesn't have
    is marked ⚠ too.

---

## Running the show from it

GO on the Matrix List is quewi's normal GO: while the page is up, its
quewi cue list is the one GO runs, exactly as if you were on that list's
tab. Nothing on this page fires a desk cue by itself, and clicking a row
only selects it. To move standby, double-click a quewi cue (or right-click
it and pick **Make this the standby cue**).

In **Show Mode**, nothing can be re-placed. If the Matrix List was the page
you were on, the stage-manager screen's **COMING UP** shows the merged
order: each quewi cue with its lighting cues alongside ("LX 12 Sunrise"),
and the lighting cues on their own rows in between, in blue. STANDBY also
says which lighting cues go with the next GO.

---

## Without the desk

quewi saves the desk's cues with the show each time it reads them. Open
the show with no desk connected (planning at home, or the desk is off)
and the Matrix List still shows them, with a line saying when they were
last read. It reads them again as soon as the desk is back.

Placements are saved in the show too, so you can lay out the whole show
offline and check it against the desk later.

---

## On a second monitor

Right-click the Matrix List's tab and pick **Detach to window**. The
window follows the show live, like the tab. A show has one Matrix List:
pressing **+ → Matrix List** again just takes you to it.

---

## Remote control

HeliOSC and other remotes can read the merged list with
`/quewi/query/matrix` and `/quewi/query/matrix/current`. See the
[OSC reference](../osc-control/reference.md#matrix-list).

---

## What it doesn't do (yet)

- Only ETC Eos-family desks can be read. grandMA and MSC desks can still
  be fired from quewi, but their cue lists can't be read back.
- Lighting is the only other department so far. The layout is built to
  take more (another desk, video servers, follow spots) later.
- It doesn't fire desk cues itself. quewi's own cues do that, as always.
- Placements aren't on the undo stack: drag the cue again, or use **Put
  back automatically**.
