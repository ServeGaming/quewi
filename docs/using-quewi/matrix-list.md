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

The top of the page says what you're looking at in one line, for example
"Sound & video from **Main** · Lights from desk list **2** ‘B/O’ · 146 cues
· ● Live from ETC Eos at 127.0.0.1". Under it you can change the cue list
and the desk cue list, and a key explains the colours and marks. If the
desk isn't connected, a banner says why and what to do about it. Hover over
anything for more.

| Column | Shows |
|---|---|
| **NOW** | **STANDBY** (what GO fires next), **PLAYING**, **LX LIVE** (the desk's running cue), **LX NEXT** (what GO Lights fires), or ⚠ for a lighting cue that isn't on the desk. |
| **Q** | The quewi cue number. |
| **SOUND & VIDEO** | The quewi cue. A lighting cue hit part-way through a song sits on its own row underneath, indented and in lavender: "└─ hit at 0:30 · Chorus". |
| **LIGHTS** | The desk cue(s) for this moment: "LX 12  Sunrise". A multipart cue says how many parts it has. |
| **DESK TIME** | The desk cue's up / down time, and its follow (F) or hang (H) if it has one. |
| **LINED UP** | Why the lighting cue is on this row: **◆ Lined up by hand** (you did it), "Fired by Q3's GO", "Hit 0:30 into Q1", or "Follows the desk's order". |
| **NOTES & SCENE** | The quewi cue's notes, then the desk cue's scene ("Scene: Act 1") and notes. |

The rows that matter right now are coloured right across, with a stripe
down the left edge, in Show Mode's colours: **amber** for standby, **green**
for what's playing, **blue** for the desk's live cue (and a paler blue for
its next one), and **lavender** for lighting hits inside a song.

**Follow the show** (on by default) keeps standby, or the desk's live
cue, in view as the show runs.

---

## Where each lighting cue goes

quewi places every desk cue by the first of these that applies:

1. **You lined it up.** See [Lining up sound and lights](#lining-up-sound-and-lights)
   below. Your placements are saved with the show.
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

!!! note "When the desk changes"
    The desk tells quewi when its cues change, and quewi reads just those
    cues again. If the desk **renumbers** a cue you placed, your placement
    follows it (quewi recognises the cue by its ID, not only its number).
    If a cue you placed is **deleted** on the desk, its row stays where you
    put it, marked ⚠ "not on the desk", so you can see what went. Right-click
    it to forget it. A quewi cue that fires a desk cue the desk doesn't have
    is marked ⚠ too.

---

## Lining up sound and lights

Lining up always moves the **lighting** cue. quewi's cue order is your GO
order, and the Matrix List never changes it.

- **Drag a lighting cue onto a quewi cue.** The lighting cue moves onto that
  cue's row. Drop it *between* two rows to give it a row of its own after
  the quewi cue above, or above the first cue to put it at the very top.
- **Drag a quewi cue onto a lighting cue.** That lighting cue moves onto
  the quewi cue's row. (Dropping a quewi cue onto another quewi cue does
  nothing, because that would reorder your GO order. The status bar says
  so.)
- **Right-click** either kind of row and pick **Line up … with a quewi cue…**
  or **Line up Q… with a lighting cue…**. A list opens that you can filter
  by typing a number or a name.
- **Unlink**: right-click a row that's ◆ lined up by hand and pick
  **Unlink LX 12**. quewi then places it automatically again.

Every change is one step on the show's undo list: <kbd>Mod</kbd>+<kbd>Z</kbd>
puts it back, and <kbd>Mod</kbd>+<kbd>Y</kbd> does it again.

---

## Running the show from it

GO on the Matrix List is quewi's normal GO: while the page is up, its
quewi cue list is the one GO runs, exactly as if you were on that list's
tab. Clicking a row only selects it. To move standby, double-click a quewi
cue (or right-click it and pick **Make Q… the standby cue**).

### GO Lights

The **GO Lights** button at the top right fires the lighting desk's next
cue in this Matrix List's desk cue list: the row marked **LX NEXT**. The
button names it, for example **GO Lights  8.4  Back**. The keyboard
shortcut is <kbd>Mod</kbd>+<kbd>Shift</kbd>+<kbd>G</kbd>. It never fires
from a row click, and <kbd>Space</kbd> stays quewi's GO.

Which cue is "next":

- the desk's pending cue, when it's in this list;
- otherwise the cue after the desk's running cue, when that's in this list;
- otherwise (the desk is running a different list) this list's first cue.

quewi sends `/eos/cue/<list>/<cue>/fire` to the desk, the same way a
lighting trigger's **Go to cue** does. That works for any cue list, not
only the one on the desk's main playback. The status bar confirms what
went.

GO Lights is greyed out, with the reason when you hover over it, when the
lighting desk isn't an ETC Eos, when quewi isn't connected to it (it needs
to know the desk's next cue), or at the end of the list.

In **Show Mode**, nothing can be re-placed. If the Matrix List was the page
you were on, the stage-manager screen's **COMING UP** shows the merged
order: each quewi cue with its lighting cues alongside ("LX 12 Sunrise"),
and the lighting cues on their own rows in between, in blue. STANDBY also
says which lighting cues go with the next GO, and a blue **GO Lights**
button sits under GO, smaller than GO and well away from PANIC.

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
choosing **List → Matrix List** again just takes you to it.

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
- It only fires desk cues when you press **GO Lights**. quewi's own cues
  fire the desk as they always have.
