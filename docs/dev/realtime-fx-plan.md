# Implementation plan — Real-time audio effects on live cues (BACKLOG #1, XL)

> Produced by a 3-agent codebase scout (engine / effects / model). This is the
> executable plan: follow it top-down in a fresh window. The hard part is one
> restructuring of the real-time mix loop — do that carefully, it runs on the
> audio callback with a hard deadline.

## Goal
Apply each AudioCue's EQ/Compressor/Reverb/Delay rack to the **fired** voice in
real time. Today the rack only affects the audio editor and the offline bounce.

## Key insight — the chain already persists
The cue's effects chain already round-trips in the show file: it lives at
`AudioCue.editorModelJson["tracks"][0]["effects"]` (added v0.9.78). The JSON
shape is `[{ "type": "eq|compressor|reverb|delay", "enabled": bool,
"params": { id: double, ... } }]`. **No new persisted field is needed.**
It is simply never read at fire time. So: *cue chain == editor model track-0
effects chain.*

## Design (simplest, reuses everything)
1. Factor the effects-array deserialize loop out of `AudioEditorTrack::fromJson`
   (`AudioEditorModel.cpp:155-170`) into a shared free function
   `std::vector<std::unique_ptr<AudioEffect>> buildEffectsFromJson(const QJsonArray&)`
   so there is ONE deserializer. (`effectTypeFromKey` + `AudioEffect::create` +
   `setEnabled` + `setParameterValue` loop.) Have the track call it too.
2. `AudioCue::buildEffectChain() const` — reads `m_editorModelJson["tracks"][0]
   ["effects"]` and returns a fresh chain via that helper. Returns empty when no
   editor session/effects (cue plays dry, as today).
3. `VoiceParams` (`AudioEngine.h:26-51`): add
   `std::vector<std::shared_ptr<AudioEffect>> effects;` (shared_ptr keeps the
   value-type struct copyable/movable).
4. `GoEngine.cpp` audio branch (~after line 189, before object-audio block):
   `p.effects = audioCue->buildEffectChain();` (move into shared_ptrs).
5. `Mixer::addVoice` (`AudioEngine.cpp:33-73`): move `params.effects` onto a new
   `Voice` member; call `prepare(sampleRate)` + `reset()` on each ONCE here
   (fire/GUI thread, before the push_back under `m_mutex`). Mirrors
   `LiveEffectDevice::start` (`LiveEffectDevice.cpp:24-30`).
6. `Voice` struct (`AudioEngine.cpp:318-447`): add `effects` vector + a
   pre-allocated stereo scratch `std::vector<float>`; extend the hand-written
   move ctor/assign (`:384-446`).
7. `Mixer::readData` (`AudioEngine.cpp:457-741`) — THE one hot-loop change:
   when a voice HAS effects AND is NOT object-audio (no `channelGains`), render
   its resampled+envelope+pan result into the per-voice interleaved-stereo
   scratch for the whole `framesWanted` block, run
   `for (fx) if (fx->isEnabled()) fx->process(scratch.data(), framesWanted);`,
   THEN sum scratch into `out` applying `outputGains`. When `effects` is empty,
   keep the existing per-frame zero-copy path untouched (no perf regression).
8. Editor stays the single entry point (track 0 = cue chain). `closeEvent`
   already saves the model into the cue, so the dialed chain becomes live on the
   next GO automatically. Optional: an "Edit Effects…" button in the Inspector
   audio panel that opens the existing `AudioEditorWindow` (launch code at
   `MainWindow.cpp:439`).

## Real-time-safety — the non-negotiables
- **Mixing runs on the audio callback** (`Mixer::readData`, QAudioSink pull
  thread / CoreAudio render thread), hard deadline. `m_mutex` is held across the
  whole voice loop.
- `process()` for all four effects is **allocation-free, lock-free, no Qt
  signals** — safe to run in the callback. (EqEffect.cpp:121, CompressorEffect.cpp:41,
  ReverbEffect.cpp:56, DelayEffect.cpp:29.) Compressor does one relaxed atomic
  store/block (fine).
- **prepare()/reset() ALLOCATE** for Reverb (rebuildDelayLines) and Delay
  (rebuildBuffers). Call them OFF the audio thread, once, at addVoice/fire — never
  in readData.
- **Effects are QObjects** — construct/destroy on the GUI/fire thread only.
  `m_voices.erase` on voice-finish runs on the audio thread → it would destroy
  the effect unique_ptrs there. **Defer effect destruction to the GUI thread**
  (move the finished voice's fx out and delete via the existing QueuedConnection
  `voiceFinished`/`onMixerVoiceFinished` path at `:735-738`).
- **Pre-allocate the per-voice scratch** at addVoice (size to a safe max block);
  never resize inside readData. (Avoid the on-growth resize wart at
  `LiveEffectDevice.cpp:46`.)
- Live param tweaks of a playing cue are NOT required for v1 — freeze params at
  fire (chain built once from the spec). Strictly safer than the editor's
  benign-race path. If added later: mirror `setVoiceChannelGains`
  (`AudioEngine.cpp:227-237`), a mutex-guarded setter the callback reads under
  the lock it already holds.

## v1 scope / limitations (document these)
- **Stereo-only insert.** Effects process interleaved stereo in-place; the mixer
  output can be 2..16 channels and the object-audio path downmixes to a mono
  point source. v1: apply the chain only to non-object-audio (no `channelGains`)
  voices, as a stereo insert BEFORE the send matrix. Skip it for object-audio
  cues (call out as a known limitation; the editor chain is also stereo).
- Each fired voice gets its OWN chain (stateful: reverb tails, comp envelopes) —
  always deep-copy via the JSON rebuild, never share effect pointers.

## Files to touch
- `src/audio/AudioEditorModel.{h,cpp}` — extract `buildEffectsFromJson` shared helper.
- `src/audio/AudioCue.{h,cpp}` — `buildEffectChain()`.
- `src/audio/AudioEngine.h` — `VoiceParams::effects`.
- `src/audio/AudioEngine.cpp` — Voice member + scratch + move members; addVoice
  prepare/reset; readData scratch-render branch; deferred fx destruction.
- `src/app/GoEngine.cpp` — `p.effects = audioCue->buildEffectChain();`.
- (optional) `src/ui/Inspector.cpp` — "Edit Effects…" button.

## Suggested commit slicing (so a budget cap never leaves broken code)
1. ~~**Foundation (builds, no behavior change):** add `AudioCue::buildEffectChain()`,
   add `VoiceParams::effects` (unused yet).~~ ✅ **DONE v0.9.81.** (Deserialize loop
   was inlined in `AudioCue.cpp` rather than extracted into a shared helper — a
   small duplication of the 4-key type map; dedup with `AudioEditorTrack::fromJson`
   is a future cleanup.) Next: step 2.
2. **Engine wiring:** Voice member + scratch + addVoice prepare/reset + deferred
   destruction + the readData scratch-render branch + GoEngine line. This is the
   real-time-critical commit — test hard (play overlapping cues with reverb,
   verify no dropouts/crackle, verify no crash on voice finish).
3. **Polish:** Inspector "Edit Effects…" button; docs in `docs/cue-types/audio.md`.

## Reference templates in-repo
- `src/ui/LiveEffectDevice.cpp` — prepare/reset-at-start + process-per-block in a
  real QAudioSink callback. The exact loop to replicate per-voice.
- `src/audio/AudioEditorRenderer.cpp:150-165` — chunked offline application of the
  same chain (1024-frame blocks).
