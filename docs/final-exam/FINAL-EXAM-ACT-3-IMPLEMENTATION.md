# final exam act 3: intermittent godot audio

## immutable implementation contract

this document is immutable once handed to an implementor. do not revise it to
match implementation choices. stop and ask carson if a genuine contradiction
or behavior-changing ambiguity remains.

record results in:

```text
docs/final-exam/FINAL-EXAM-ACT-3-IMPLEMENTATION-NOTES.md
```

do not mention this contract or project act in code comments or runtime
documentation. do not commit.

this contract is authoritative for the final audio pass. where the mutable
final-exam roadmap names act 2, miniaudio, or a Godot-aware miniaudio vfs, that
older direction is superseded. do not update the roadmap as part of
implementation.

## prerequisite

begin from a branch containing:

```text
3b52718  final exam act-2 implementation
b068d9b  icon change
```

and this contract.

before editing:

1. inspect and preserve concurrent work;
2. require a clean tracked worktree and index, while preserving the untracked or
   newly supplied audio assets as authored input;
3. read the complete final-exam roadmap, all prior final-exam contracts, and all
   prior final-exam implementation notes;
4. read the complete current `Exam`, `LevelPlayer`, their scenes and integration
   runners, the type-registration boundary, project startup configuration,
   export presets, desktop export workflow, and `SConstruct`;
5. inspect every file carson has supplied under `godot-project/audio/`;
6. establish the native, level-checking, GDExtension, resource-integration,
   editor-integration, player-integration, exam-integration, ordinary-startup,
   and `git diff --check` baselines; and
7. keep the Godot editor closed during native rebuilds and scene-affecting test
   runs.

the background track already supplied at formalization is:

```text
path:       godot-project/audio/bgm.mp3
format:     mp3, 48 khz, stereo, approximately 207.46 seconds
sha-256:    cbdd1b838baf43cf9917975b8fc1b30c11bd3681f31a977290ca19353c1ee891
```

preserve its bytes, path, and filename. carson will supply the sound-effect
variants at implementation time. if the background track is absent or changed,
or if any required effect bucket has no supplied variant, stop and report the
exact missing or changed input. do not synthesize, download, transcode, rename,
duplicate, or substitute audio.

## goal

add one persistent, presentation-only audio director to the existing exam:

```text
res://audio/bgm.mp3
        ↓
one endlessly looping Godot audio voice
        ↓
an independent intermittent gain envelope

successful Exam semantic signals
        ↓
six typed effect buckets
        ↓
random authored variant + small pitch/volume variation
        ↓
bounded polyphonic Godot playback
```

the background track is sometimes present and sometimes absent, but its
playback position never stops advancing. sound effects follow successful
semantic events already published by `Exam`. victory additionally ducks the
background music.

audio is not game state. failure to initialize or play audio must not alter
command legality, exact state, completion, navigation, persistence, rendering,
or whether the exam is ready to play.

## use Godot audio

use Godot's built-in audio surface:

- `AudioStreamPlayer` for the background voice and effect voices;
- `AudioStreamMP3` looping for the supplied background track; and
- one `AudioStreamRandomizer` for each effect bucket.

do not add miniaudio, another audio library, a custom output device, a custom
vfs, a custom decoder, an audio callback, or a second mixing graph. do not copy
audio code from `~/Desktop/wrkspc/rhythm-game/`.

do not add rhythm timing, a conductor, beat metadata, sample-position gameplay,
custom dsp, positional audio, pause shaders, or track-editing behavior.

## asset layout

retain the background track at the root of the audio directory and place every
sound-effect source file in exactly one cue directory:

```text
godot-project/audio/
├── bgm.mp3
├── palette_select/
├── rotate/
├── place/
├── remove_undo/
├── hover/
└── victory/
```

each direct Godot-importable audio file in one of those directories is one
authored variant of that cue. variants are not shared across directories.
subdirectories beneath a cue directory are not part of the asset contract.
Godot import metadata and a built-in `.tres` randomizer, if used, are not
variants.

reference every supplied variant explicitly through Godot resources. do not
discover filenames or construct variant lists by scanning directories at
runtime. the implementation may store the six built-in randomizers in the exam
scene, in dedicated `.tres` files, or through an equivalently direct
Godot-native resource arrangement, provided:

- every supplied direct variant is referenced once;
- every reference remains a `res://` resource and survives desktop export;
- adding or removing authored variants is a resource-data change, not a C++
  change; and
- no new custom audio resource schema or serialized cue enum is introduced.

the supplied files are source assets. do not normalize, trim, resample, edit, or
otherwise rewrite them. loudness or timing concerns discovered during manual
playtesting must be reported to carson rather than hidden by destructive asset
editing.

## persistent ownership

add one small C++ `AudioDirector` node and register it at scene initialization.
the ordinary `exam.tscn` owns exactly one director for its whole lifetime. it
may be a direct child of `Exam` or an equally stable scene child whose ownership
is unambiguous.

```text
Exam
├── persistent problem state
├── replaceable LevelPlayer canvas
└── one persistent AudioDirector
    ├── one background player
    └── six effect players
```

the director connects once to the stable semantic signals on `Exam`. it does
not connect to individual `LevelPlayer` canvases. navigating, mounting an
incoming canvas, completing a transition, and freeing an outgoing canvas do
not reconstruct the director, restart the background track, reset its envelope,
or discard playing effect voices.

keep the dependency one-way:

```text
Exam semantic result → AudioDirector presentation
```

`Exam`, `LevelPlayer`, `ProblemState`, `Session`, and every exact engine command
must remain unaware of audio cues, variants, players, gain, pitch, or playback
success. do not add an audio return value or audio precondition to a gameplay
operation.

## background playback

load `res://audio/bgm.mp3` as one `AudioStreamMP3`, enable looping from the
beginning of the file, and start it once when the director becomes ready.

the source is authored as a seamless full-file loop. do not seek, restart,
crossfade a second copy, insert silence into the file, or stop the player at the
inaudible floor. the same voice advances continuously through:

- every envelope phase;
- every problem transition;
- every victory duck; and
- repeated full-file loops.

the music begins at the start of the file and at the inaudible floor, then
immediately begins its first fade-in. there is no initial absent hold.

### independent intermittent envelope

the ambient envelope is a four-phase state machine:

```text
fade in → audible hold → fade out → absent hold → fade in → ...
```

use these exact bounds:

```text
audible gain:          0 db
inaudible floor:     -70 db

fade-in duration:      uniform real value in [6, 12] seconds
audible-hold duration: uniform real value in [35, 100] seconds
fade-out duration:     uniform real value in [8, 16] seconds
absent-hold duration:  uniform real value in [25, 120] seconds
```

sample one new duration when its phase begins. ordinary runtime randomness is
intended; the sequence need not persist across launches and must not enter
gameplay state.

interpolate the fade endpoints smoothly and monotonically in decibels. the
exact easing polynomial is an implementation choice, but it may not overshoot,
step abruptly, or change the phase duration. holds remain exactly at their
endpoint.

advance this state from the director's ordinary process delta. one unusually
large delta must consume completed phases and continue into the correct later
phase rather than losing elapsed time. resizing, navigation, input, and canvas
lifetime do not advance it through separate paths.

the envelope controls only audibility. because the player continues beneath
the `-70 db` floor, the musical position at the next fade-in is generally out
of phase with both the envelope cycle and the start of the composition.

## victory duck

every `victory_reached` signal both plays the victory effect and starts this
background duck:

```text
attack:       current duck gain → -12 db over 0.15 seconds
hold:         -12 db for 2.0 seconds
release:      -12 db → 0 db over 3.0 seconds
```

the ambient envelope and victory duck are independent gain components. apply
their sum to the background player:

```text
background volume db = ambient envelope db + victory duck db
```

therefore a victory during an absent hold remains inaudible, and a victory
during a fade does not cancel, restart, delay, or reshape that fade. ducking
never pauses, stops, seeks, or restarts the background track.

use smooth monotonic attack and release curves without overshoot. a later
victory received while a duck is active plays another polyphonic victory voice
and restarts the duck sequence from its current duck gain: attack toward
`-12 db`, hold, then release. it does not stack another `-12 db`.

## typed sound-effect buckets

the implementation has exactly these six logical cues:

```cpp
enum class AudioCue {
    palette_select,
    rotate,
    place,
    remove_undo,
    hover,
    victory,
};
```

an equivalent private scoped enum is acceptable. do not expose this cue type
through the existing Godot signals, exact engine, resources, or gameplay
operations. its purpose is to keep the audio consumer internally exhaustive;
it is not a new application event system.

each bucket has one built-in `AudioStreamRandomizer` containing all and only the
variants from its corresponding directory. configure every randomizer as:

```text
playback mode:             random, immediate repeats allowed
variant probability:       equal weight
random pitch deviation:    up to 2 semitones below or above authored pitch
random volume deviation:   up to 1 db below or above authored volume
```

in Godot terms, use the randomizer mode which permits repeats,
`random_pitch_semitones = 2.0`, and `random_volume_offset_db = 1.0`. do not
manually roll a variant and then duplicate Godot's randomization.

pitch and volume variation are sampled independently for every play. they apply
to sound effects only, including victory; they never alter the background
track. do not add panning, positional attenuation, filtering, reverb, delay, or
per-problem variation.

### polyphony and trigger policy

give every effect bucket its own `AudioStreamPlayer` with:

```text
max polyphony: 8
base pitch:    1.0
base gain:     0 db
```

every qualifying signal calls `play()` exactly once, even when that bucket
already has one or more active voices. all six buckets are polyphonic. rely on
Godot's bounded voice behavior after eight simultaneous plays in one bucket;
do not add a queue.

there is no audio-side throttling, debounce, cooldown, coalescing,
same-frame suppression, or duplicate suppression. the semantic signal surface
already rejects unsuccessful or unchanged gameplay operations. the director
must not invent another filter.

in particular, every `active_proposal_changed(true, proposal)` plays one hover
cue, even when hover cues are already playing or another accepted hover change
occurred in the same frame. `active_proposal_changed(false, proposal)` is
silent. a disappearing ghost is not a hover cue.

## semantic mapping

consume the existing `Exam` signals exactly as follows:

```text
palette_selected       → palette_select
orientation_changed    → rotate
placement_succeeded    → place
removal_succeeded      → remove_undo
undo_succeeded         → remove_undo
active_proposal_changed
    present = true     → hover
    present = false    → no cue
victory_reached        → victory + background duck
problem_changed        → no cue
```

ignore payload fields which the audio decision does not need. do not use palette
entry, orientation, proposal, problem index, or problem number to choose a
variant or alter playback.

preserve the existing producer semantics:

- reselecting the current palette entry remains silent;
- cycling an entry with one representative orientation remains silent;
- failed placement and removal remain silent;
- empty undo remains silent;
- pointer motion which preserves the same exact active proposal remains silent;
- binding or remounting a solved problem remains silent;
- each new unsolved-to-solved transition emits and plays victory; and
- refused navigation remains silent.

audio connects to these results. it must not add raw input callbacks or infer
success by inspecting mutable game state.

## initialization and failure

initialize every required resource and player before connecting the director to
semantic signals. start the background track only after its looping stream and
envelope state are valid.

authored audio is required shipped content, but a runtime audio failure is
presentation failure:

- report a concise error which identifies the background path or cue bucket;
- keep the exam playable;
- leave a failed bucket silent while unaffected buckets continue;
- never retry every frame;
- never replace a missing sound with another bucket; and
- shut down through ordinary Godot node and resource ownership.

do not add raw `new`/`delete`, manual decoder lifetime, audio threads, blocking
waits, or global audio pointers. do not make successful audio-device
initialization a condition of `Exam::ready_to_play()`.

## production and test surface

keep `AudioDirector` small and cohesive. expose only the ordinary production
operations and read-only state needed to prove:

- initialization;
- current envelope phase, duration, progress, and gain;
- current duck phase or gain;
- the six configured effect players and randomizers;
- signal-to-cue routing; and
- continued ownership across problem navigation.

tests may advance the same production delta-driven state transition function
that `_process` calls. do not add a test-only clock, test-only signal route,
test-only playback method, deterministic production seed, or alternate asset
catalog.

tests must not assert a particular random variant, pitch value, volume value, or
phase-duration sequence. they must assert configuration and bounds.

## verification

extend the existing Godot integration proof or add one dedicated
`AudioIntegrationRunner` and scene. exercise the real `exam.tscn`, real
`AudioDirector`, real resources, real semantic connections, and production
state-transition functions.

automated checks must prove at least:

1. the ordinary exam scene owns exactly one persistent audio director;
2. the supplied background resource loads, has full-file looping enabled,
   begins playing once, and starts in fade-in from `-70 db`;
3. all six required randomizers exist, contain every supplied variant in their
   directory exactly once, permit immediate repeats, use equal weights, and
   carry the specified pitch and volume deviation;
4. all six effect players have max polyphony eight;
5. every existing semantic signal maps to exactly the cue or no-op named above;
6. removal and undo reach the same effect bucket;
7. two immediate qualifying calls, including two hover-presence calls, are
   both submitted without audio-side throttling;
8. hover disappearance and `problem_changed` submit no effect;
9. the ambient phase durations always fall within their specified bounds, each
   phase reaches its exact endpoint, and a large delta crosses phase boundaries
   without losing time;
10. the background player remains the same playing instance while the envelope
    becomes absent and while the exam navigates between problems;
11. victory submits its polyphonic effect and applies the specified duck without
    changing the ambient phase;
12. a repeated victory restarts rather than stacks the duck;
13. a failed audio resource or unavailable playback path reports presentation
    failure without changing exam readiness or exact play; and
14. no miniaudio dependency, copied rhythm-game machinery, or custom audio
    backend enters the repository.

run and preserve:

- native regression tests;
- authored-level checking;
- the GDExtension build;
- resource, editor, player, and exam integration scenes;
- the new or extended audio integration scene;
- ordinary headless startup;
- import and load checks for every audio resource;
- a desktop export check which establishes that the background and every effect
  variant are packaged and loadable; and
- `git diff --check`.

manual playtesting must:

1. launch the ordinary exam and hear the background fade in without a start
   click, abrupt gain step, or duplicate voice;
2. leave the application running through fade-in, audible, fade-out, absent,
   and the next fade-in, confirming that the composition returns at its
   continuously advanced position;
3. listen across the full-file loop boundary and confirm the authored seamless
   loop remains seamless;
4. trigger every cue through real successful play;
5. trigger rapid selection, rotation, placement, removal/undo, and hover changes
   and confirm audible overlap without throttling or uncontrolled runaway
   volume;
6. confirm unsuccessful and unchanged operations remain silent;
7. navigate while music and effects are playing and confirm no restart, cut, or
   duplicate audio director;
8. solve a problem and confirm the victory cue is clear while the background
   ducks smoothly, holds, and recovers;
9. undo and solve again during or shortly after the first victory, confirming a
   second victory voice and a restarted, non-stacking duck; and
10. judge the supplied assets for balance without modifying their source bytes.

## boundaries

act 3 does not:

- use miniaudio or add any non-Godot audio dependency;
- edit, generate, replace, rename, transcode, normalize, or otherwise rewrite
  carson's audio files;
- add runtime directory scanning or a custom serialized audio schema;
- add settings, sliders, mute controls, buses, saved preferences, pause behavior,
  focus-loss behavior, or a user-facing audio error;
- add positional sound, custom dsp, beat synchronization, sample-accurate game
  timing, or music-reactive visuals;
- add a sound for `problem_changed`, failed operations, hover disappearance, or
  application startup beyond the background track;
- throttle, debounce, queue, or coalesce sound effects;
- change semantic signal names, payloads, or producer rules;
- change exact engine state, command legality, level content, manifest order,
  navigation, persistent problem state, or presentation unrelated to audio;
- redesign the editor, exam shell, canvas, completion, or transitions;
- redesign exporting, add a platform, sign, notarize, publish, or create a
  release;
- update the mutable final-exam roadmap or an earlier immutable contract;
- commit, push, merge, rebase, squash, or rewrite history; or
- begin later document, presentation, save-data, or release work.

## acceptance summary

act 3 is complete exactly when:

```text
one persistent Godot AudioDirector
    ∧ no miniaudio

bgm.mp3
    ↓ one full-file loop begun once
continuous musical phase
    × independent random intermittent envelope
    × independent non-stacking victory duck

successful Exam semantic signal
    ↓ exact typed mapping
one of six equal-weight randomizer buckets
    ↓ ±2 semitones, ±1 db
one polyphonic play, never throttled

navigation does not restart or duplicate audio
    ∧ audio failure cannot affect exact play
    ∧ all supplied assets survive desktop export

all regressions and audio integration proofs pass
    ∧ manual listening acceptance passes
    ∧ no unrelated work
    ∧ no commit
```
