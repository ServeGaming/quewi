---
name: quewi
tagline: Theatre cueing software — sound, light, projection, OSC. A single GO button you can trust.
audience: Stage managers, sound designers, lighting designers, projection designers, master electricians, and assistant directors working in proscenium, black-box, opera, dance, and concert spaces.
platform: Native desktop (Windows, macOS, Linux). Designed for a 24-inch booth display and a hardware keyboard.
density: compact
appearance:
  default_theme: dark
  themes: [dark, dark_high_contrast, dark_colorblind_safe, light_rehearsal]

color:
  brand:
    mark: '#E84A2F'         # quewi red — the app mark and the panic button only
    accent: '#7AB8FF'       # interactive accent: focused borders, primary buttons (non-GO), links
  surface:
    canvas: '#0E0F12'        # deepest background; the room around the app
    panel: '#16181D'         # cue list, inspector, dock backgrounds
    elevated: '#1E2128'      # selected rows, popovers, inspector header strip
    raised: '#262A33'        # buttons, input chrome, dropdowns
    overlay: 'rgba(8, 10, 14, 0.72)'
    cuelist_alt_row: '#13151A'
  border:
    subtle: '#23262E'
    strong: '#33373F'
    focus: '#7AB8FF'
  text:
    primary: '#ECEEF3'
    secondary: '#A8AEBA'
    tertiary: '#727884'
    disabled: '#4A4F5A'
    inverse: '#0E0F12'
  state:
    armed: '#ECEEF3'         # white — next to fire
    running: '#3DD68C'       # green — playing
    paused: '#F2C94C'        # amber — held
    broken: '#FF5A5A'        # red — would fail
    loaded: '#62B4FF'        # blue — pre-rolled, sample-zero ready
    disarmed: '#6B7280'      # grey — present but inert
    auto_continue: '#A88BFF' # violet thread — visual link from a cue to the one it auto-fires
  feedback:
    info: '#62B4FF'
    success: '#3DD68C'
    warning: '#F2C94C'
    error: '#FF5A5A'
  network:
    osc_send: '#7AB8FF'
    osc_recv: '#A88BFF'
    midi: '#F2A06B'
    dmx: '#3DD68C'
  meter:
    audio_track: '#0E0F12'
    audio_signal: '#3DD68C'
    audio_warn: '#F2C94C'    # -6 dBFS to -3 dBFS
    audio_clip: '#FF5A5A'    # > -3 dBFS

typography:
  font_family:
    sans: '"Inter", "Segoe UI Variable", "SF Pro Text", system-ui, sans-serif'
    display: '"Inter", "Segoe UI Variable", "SF Pro Display", system-ui, sans-serif'
    mono: '"JetBrains Mono", "SF Mono", "Cascadia Mono", Consolas, monospace'
  scale:
    micro:        { size: 10px, line: 12px, weight: 600, tracking: 0.04em, case: uppercase }
    caption:      { size: 11px, line: 14px, weight: 500, tracking: 0.02em }
    body_small:   { size: 12px, line: 16px, weight: 400 }
    body:         { size: 13px, line: 18px, weight: 400 }
    body_strong:  { size: 13px, line: 18px, weight: 600 }
    title:        { size: 16px, line: 22px, weight: 600 }
    section:      { size: 20px, line: 26px, weight: 700 }
    cue_number:   { size: 18px, line: 22px, weight: 700, family: mono, tracking: -0.01em }
    next_label:   { size: 32px, line: 36px, weight: 700, tracking: -0.01em }
    go_label:     { size: 56px, line: 60px, weight: 800, tracking: 0.06em, case: uppercase }
    monitor_hex:  { size: 12px, line: 16px, weight: 400, family: mono }

space:
  0: 0px
  1: 4px
  2: 8px
  3: 12px
  4: 16px
  5: 24px
  6: 32px
  7: 48px
  8: 64px
  9: 96px

sizing:
  control_height_sm: 24px
  control_height_md: 28px
  control_height_lg: 36px
  go_button_min_width: 240px
  go_button_min_height: 88px
  cue_row_height: 28px
  inspector_form_label_width: 96px
  app_min_width: 1024px
  app_min_height: 720px

radius:
  none: 0px
  xs: 2px
  sm: 4px
  md: 6px
  lg: 10px
  xl: 14px
  pill: 999px

stroke:
  hairline: 1px
  default: 1px
  emphasis: 2px
  focus_ring: 2px

elevation:
  0: 'none'
  1: '0 1px 0 rgba(255, 255, 255, 0.04) inset, 0 1px 2px rgba(0, 0, 0, 0.4)'
  2: '0 8px 24px rgba(0, 0, 0, 0.55), 0 1px 0 rgba(255, 255, 255, 0.04) inset'
  modal: '0 32px 80px rgba(0, 0, 0, 0.7), 0 2px 0 rgba(255, 255, 255, 0.04) inset'

motion:
  duration:
    instant: 60ms
    fast: 120ms
    standard: 200ms
    slow: 320ms
    show_critical: 0ms       # state changes during a running show are immediate
  easing:
    standard: 'cubic-bezier(0.2, 0, 0, 1)'
    enter:    'cubic-bezier(0, 0, 0, 1)'
    exit:     'cubic-bezier(0.4, 0, 1, 1)'
    snap:     'cubic-bezier(0.16, 1, 0.3, 1)'

opacity:
  disabled: 0.4
  hover: 0.88
  pressed: 0.72
  scrim: 0.72
  inactive_panel: 0.6

icon:
  size_sm: 14px
  size_md: 16px
  size_lg: 20px
  size_xl: 28px
  stroke: 1.5px
  style: line
  shape_redundancy: true     # state icons carry a shape so meaning isn't color-only

focus:
  ring_color: '#7AB8FF'
  ring_width: 2px
  ring_offset: 2px
  always_visible: true

layout:
  pane:
    cuelist_min_width: 520px
    inspector_min_width: 360px
    transport_height: 96px
  splitter_thickness: 4px
  splitter_handle_color: '#0A0B0E'

state_palette_alternates:
  high_contrast:
    armed: '#FFFFFF'
    running: '#33FF77'
    paused: '#FFD400'
    broken: '#FF2A2A'
    loaded: '#00B0FF'
    disarmed: '#909090'
  colorblind_safe:           # deuteranopia/protanopia-safe; pair with shape glyphs
    armed: '#F4F4F4'         # ●
    running: '#3DA8FF'       # ▶
    paused: '#FFB400'        # ⏸
    broken: '#D55E00'        # ✕
    loaded: '#56B4E9'        # ↓
    disarmed: '#737373'      # —

light_rehearsal_theme:
  surface:
    canvas: '#FAFAFB'
    panel: '#FFFFFF'
    elevated: '#F2F3F6'
    raised: '#E7E9EE'
  text:
    primary: '#0E0F12'
    secondary: '#4B5159'
    tertiary: '#7A808A'
    disabled: '#B7BBC2'
  border:
    subtle: '#E2E4E9'
    strong: '#C7CAD1'
---

# The vision

quewi is the brain at the back of the house. It runs while the audience watches, while the operator's hands rest on a keyboard in a dark booth, and while one wrong keystroke could ruin the show. The visual design exists to make that one keystroke impossible to mis-press, and every other keystroke effortless.

Three feelings should land in the first second a user opens the app:

1. **Calm.** The interface is quiet. The room is dark. Nothing pulses, nothing bounces, nothing competes for attention except the next cue.
2. **Trust.** Surfaces are flat and honest. State is communicated by color *and* shape, never one alone. Errors don't shout — they accumulate in a non-blocking inbox the operator can read between cues.
3. **Speed.** Everything happens in under 200ms. During a running show, *everything happens in zero milliseconds* — no animation can delay the sound, the light, or the projection.

# Mood and atmosphere

The default theme is dark, but it is not black. Pure black on an LCD is harsh in a tech booth lit only by a console lamp; the canvas is a desaturated blue-charcoal that reads as black under low ambient light but doesn't strobe the operator's pupils when they look up at the stage. Surfaces step up from there in three layers — panel, elevated, raised — so structure reads instantly even before any text loads.

Color is rationed. Apart from the six **state colors** for cues (armed white, running green, paused amber, broken red, loaded blue, disarmed grey) and one accent for interactive focus, the chrome is pure greyscale. The eye should be drawn to the cue list and the GO button, not to the toolbar. The app's sole branded mark — a stylized **q** in quewi red — appears in the title bar and on the panic button. Nowhere else.

# The GO button is the centerpiece

The bottom transport bar exists for one reason: to make the next action unmissable. The GO button is at least 240×88 pixels, lives in the bottom-right corner where the operator's right hand sits, and renders the next cue's number and name in 32 px display type next to it ("NEXT — 14.5 Door SFX"). Spacebar fires it. The button has three visual states — idle (raised, accent-bordered), pressed (sunken, brand red briefly flashed for 60 ms), and disabled (40% opacity, when there is no next cue).

The panic button sits to its left, smaller and red. Pause is left of that, neutral. This left-to-right escalation — pause, panic, GO — mirrors the severity of consequence; the most-pressed and the most-dangerous keys are physically separated.

# State language

Cue state is the single most important thing the UI shows, and it must be readable from a glance across a dim booth. Six colors, each mapped to the operator's mental model:

- **White / armed.** Ready. The cue that GO will fire next.
- **Green / running.** This cue is currently playing. Audio meters and progress bars next to it use the same green.
- **Amber / paused.** Running but held; resume returns it to green.
- **Blue / loaded.** Pre-rolled — the engine has already opened the file or resolved the destination, and the cue is sample-zero ready.
- **Red / broken.** Something is wrong; the cue would fail if fired. The pre-flight panel uses the same red.
- **Grey / disarmed.** Present in the list but skipped on GO.

The colorblind-safe palette pairs each state with a glyph (`● ▶ ⏸ ↓ ✕ —`) so the meaning survives even if every channel of color information disappears. The high-contrast theme cranks all six toward AAA contrast against the canvas.

# Typography

A single sans-serif family carries the entire interface — Inter where present, otherwise the system's default UI sans (Segoe UI Variable on Windows, SF Pro on macOS). One font, used at well-defined sizes, keeps the app feeling unified across operating systems.

Cue numbers and any time-codes (pre-wait, post-wait, runtime, OSC time tags) are set in a monospaced face at 18 px so columns align perfectly and digits don't shift width as values change. The "NEXT" label and the GO button use a heavier display weight at 32 px and 56 px respectively, with slight tracking on the GO label so the four uppercase characters command the corner.

The OSC monitor's hex view, the show-file inspector, and any code-like surface use the mono family. There are no script faces, no ligatures, no ornaments anywhere.

# Density and rhythm

The app defaults to **compact** density: 28 px row heights in the cue list, 28 px form controls, 8 px gutters between rows. A typical show has 100–500 cues; the operator must scroll and select without losing place. Compact density means roughly 20 cues fit on a 1080p screen at default zoom.

The form labels in the inspector all align to a 96 px label gutter, giving every field a predictable horizontal anchor regardless of which type of cue is selected. Section group boxes use a single-pixel border with the section title rendered in 11 px uppercase tracked-out caps — a quiet but unmistakable hierarchy marker.

# Motion

Motion is a tool, not a flourish. The ruling principle: **nothing animates during a running show.** State changes — armed becomes running, blue loaded glow appearing on a pre-rolled cue, GO press feedback — happen instantly. There is no easing, no fade, no spring. The operator sees the change at the moment it happened.

Outside of show mode, transitions are short: 120 ms for hover and focus, 200 ms for opening a dialog or expanding an inspector group, 320 ms for the longest interaction. All curves use a tight-out cubic — fast departures, settled arrivals.

# Iconography

Line icons only. 1.5 px stroke. Monochrome by default, taking on the surrounding text color. Where an icon represents a cue type (audio, video, light, OSC, MIDI, group, and so on) it sits in a 14 px square in the cue list's type column and lives in the user's peripheral vision; the icons must be readable at that size on a 1080p display, which means no fine detail and no fills.

State icons (the play triangle, pause bars, broken cross) use the corresponding state color and are always paired with text — never icon-only.

# Theming

Four themes ship by default. **Dark** is the default and what most users will ever see. **Dark High Contrast** raises every contrast pair to WCAG AAA for users with low vision; controls grow to 36 px and the focus ring widens to 3 px. **Dark Colorblind-Safe** swaps the six state colors for a deuteranopia/protanopia-safe palette and turns on the redundant glyph layer so state is encoded twice. **Light Rehearsal** exists for daylight-rehearsal use only — it is intentionally less polished than the dark themes; the product is a booth tool first.

Themes are user-droppable as plain stylesheet files, so a venue can match its house aesthetic, but the four built-ins should cover most needs.

# What it does not look like

It does not look like a digital audio workstation — no skeuomorphic knobs, no plugin window chrome, no faders shaped like physical strips.

It does not look like a media server's compositing UI — no node graph, no glowing particle effects, no dark-blue gradient backgrounds.

It does not look like a creative tool — no playful illustrations, no rounded oversized cards, no marketing-grade empty states.

It looks like a console. A flight panel. A piece of equipment that an experienced operator trusts at 11 PM on a Saturday with a sold-out house, because every pixel earns its place.
