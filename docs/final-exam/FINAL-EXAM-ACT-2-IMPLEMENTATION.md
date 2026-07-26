# final exam act 2: minimal exam assembly

## immutable implementation contract

this document is immutable once handed to an implementor. do not revise it to
match implementation choices. stop and ask carson if a genuine contradiction
or behavior-changing ambiguity remains.

record results in:

```text
docs/final-exam/FINAL-EXAM-ACT-2-IMPLEMENTATION-NOTES.md
```

do not mention this contract or project act in code comments or runtime
documentation. do not commit.

this contract is authoritative for act 2 where the mutable final-exam roadmap
still describes audio, continuous scrolling, front matter, or a reusable
mathematical document. those are not part of this act. do not update the
roadmap as part of implementation.

## prerequisite

begin from:

```text
504b988  final exam act-1 implementation
```

before editing:

1. inspect and preserve concurrent work;
2. require a clean tracked worktree and index;
3. read `DEFINITIONS.md`, `COORDINATES.md`, `level-notes.txt`,
   `docs/TEST-LEVELS.md`, the complete final-exam roadmap, both prior
   final-exam contracts, and both implementation notes;
4. read the complete current `LevelPlayer`, its scene and integration runner,
   `PrototilePreview`, level loader and resource compiler, `State`, `Session`,
   engine commands, type-registration boundary, project startup configuration,
   and every direct level artifact;
5. establish the native, level-checking, GDExtension, resource-integration,
   editor-integration, player-integration, ordinary-startup, and
   `git diff --check` baselines; and
6. keep the Godot editor closed during native rebuilds and scene-affecting test
   runs.

if the required commit or any established level artifact is missing, stop. do
not merge, rebase, cherry-pick, recreate, or substitute history.

## goal

assemble the current exact level player into the smallest complete exam:

```text
ordered manifest
        ↓
eagerly loaded exact state for every problem
        ↓
application boots directly into problem n
        ↓
scroll down: current problem moves out above
             next problem moves in below
        ↓
scroll up: the exact reverse
        ↓
returning to any problem restores its complete play state
```

this is an mvp assembly act. preserve the current successful play grammar and
plain presentation except for the explicitly required palette interaction,
completion treatment, problem number, and vertical problem transition.

there is no title page, acknowledgements page, table of contents, continuous
document, paginated document chrome, menu, save data, remaining-area
presentation, remaining-problem presentation, audio playback, or visual
redesign.

## canonical exam manifest

the application owns one explicit ordered manifest. use one inspector-authored
`PackedStringArray` of level paths on the exam scene or an equally small
Godot-native ordered value with the same observable behavior. do not introduce
per-problem resources, display-name metadata, difficulty metadata, a manifest
schema/versioning system, or a second copy of the order in production code.

the current manifest contains every direct level artifact, in exactly the order
carson authored in `level-notes.txt`:

```text
res://levels/act0-level.tres
res://levels/raine_flower.tres
res://levels/horse.tres
res://levels/izjl.tres
res://levels/small.tres
res://levels/raine_candy_bug.tres
res://levels/ring.tres
res://levels/zooble.tres
res://levels/goul.tres
res://levels/glider.tres
res://levels/bird.tres
res://levels/spore.tres
res://levels/pythag.tres
res://levels/snake_tri_hex.tres
res://levels/guy_f.tres
res://levels/dude.tres
res://levels/galaxy.tres
res://levels/fan.tres
res://levels/zig.tres
res://levels/bug.tres
res://levels/slurm.tres
res://levels/spider.tres
res://levels/message.tres
res://levels/perkins.tres
```

this list presently contains 24 problems, including `act0-level`. that is
authored content, not a program constant. no runtime logic, display logic,
navigation bound, storage allocation, or problem-number calculation may assume
24. adding, removing, or reordering manifest entries must require changing the
manifest data rather than c++ control flow.

the manifest must be nonempty. every path must be nonempty, unique, and load a
valid exact level. duplicates are invalid exam content rather than repeated
problems.

the first manifest entry is the first problem shown, but its displayed number
is the total manifest size. for zero-based manifest index `i`:

```text
displayed problem number = manifest size - i
```

therefore the current sequence displays problem 24 through problem 1, while the
same code would display `n` through 1 for any later nonempty manifest size.

do not derive order from filenames, resource discovery, directory order,
difficulty measurements, palette order, tile count, or any property of the
compiled geometry.

## exam ownership and eager loading

create one application-level `Exam` control as the main scene root and point
ordinary startup at its scene.

`Exam` owns:

- the canonical prototile catalog;
- the ordered manifest;
- one stable-address problem-state slot per manifest entry;
- the current manifest index;
- problem navigation and transition state; and
- the one visible player shell.

each persistent problem-state slot owns at least:

```text
exact engine::Session
current palette-entry and orientation selection
authored palette colors in palette order
```

`Session` already owns the current exact `State` and complete undo history.
supply and completion remain derived from that state. do not store parallel
arrangement, supply, undo, or completion values.

construct the canonical catalog once. before showing any problem, load and
compile every manifest entry through the existing public consumer:

```cpp
load_level_resource(path, catalog)
```

extract authored colors in exact palette order and construct one empty
`engine::Session` from each compiled exact level. the compiled blueprint and
witness arrangement remain authoring proof only and never become player
progress, hints, transition content, or completion evidence.

the problem-state slots must remain at stable addresses for the lifetime of the
`Exam`. a visible presentation may borrow a problem-state slot, but it must
never outlive the `Exam`, retain a pointer across slot relocation, or own the
slot it presents.

all level resources and exact sessions are eagerly loaded. only presentation
nodes are mounted on demand. do not add background loading, threaded loading,
prefetch queues, resource eviction, session serialization, or loading
placeholders.

invalid manifest or level content is a programmer/build failure. report the
path and available typed failure detail, then fail before exposing a partially
loaded exam. do not skip a problem, reorder the valid remainder, substitute a
fallback, open a file picker, or construct a friendly recovery flow.

## player ownership refactor

the current `LevelPlayer` directly owns its catalog, loaded level, `Session`,
colors, and selection. refactor that ownership rather than reloading a path
whenever navigation occurs.

after the refactor:

- `Exam` owns every persistent problem state;
- the fixed player shell and moving canvas presentations only borrow the
  problem states they currently present;
- all player mutations apply to the borrowed problem's real `Session`;
- selection reads and writes the borrowed problem's persistent selection;
- proposals, active proposal, pointer position, projection, hover, and hit-test
  results are transient presentation state; and
- destroying every presentation node for a problem leaves its persistent slot
  unchanged.

do not bind `Session`, `State`, `Level`, palette, region, commands, or other
core/engine values into Godot variants or resources. keep the ownership seam in
c++ and expose only the existing node-level control/test surface required by
the application.

the player integration runner must continue to exercise the same real player
operations. update its construction to bind a persistent problem state rather
than preserving a second self-loading mode or a test-only controller.

## fixed shell and moving problem canvas

the ordinary game contains one fixed full-window shell:

```text
┌──────────────┬──────────────────────────────────────┐
│ fixed        │ moving problem viewport              │
│ palette      │                                      │
│              │ problem number                       │
│              │ exact target, tiles, ghost           │
│              │ completion overlay                   │
├──────────────┴──────────────────────────────────────┤
│ fixed instruction/status line                       │
└─────────────────────────────────────────────────────┘
```

preserve the current palette width, canvas fitting behavior, colors, target
drawing, placement drawing, ghost drawing, and concise bottom control text
unless this contract requires otherwise.

the palette panel and bottom instruction/status line are never children of the
animated problem layer and never translate during problem navigation.

one clipped viewport region occupies the remaining playable area. moving
problem canvases are full-size children of that clipped region. each moving
canvas presents:

- the problem number derived from the manifest;
- the exact target;
- the exact current arrangement;
- the active exact ghost;
- the complete overlay when solved; and
- no other problem prose or metadata.

the problem number moves with its problem canvas. the palette and bottom
control line do not.

during steady state, retain exactly one moving problem canvas. during an active
transition, retain exactly the current and destination canvases. do not retain
one rendered canvas per eagerly loaded problem.

## palette

the fixed palette presents the currently active problem only.

show one row per exact palette entry, in authored order. each row contains:

- the existing polygon preview in its authored color;
- the current finite remaining supply; and
- the existing visual selection distinction.

all shipped supplies are finite. do not add the word `unlimited`, catalog
display names, prototile ids, generated tile names, orientation names, or other
labels.

make the shape preview in every row clickable. clicking it selects that exact
palette entry and resets orientation exactly as the existing
`select_entry(...)` operation does. use the same public selection operation as
tab navigation and integration tests; do not create a pointer-only selection
path.

the preview may remain a non-focusable `PrototilePreview` inside a clickable
parent control. keyboard focus must remain coherent for the player's existing
shortcuts.

during a problem transition, the palette remains stationary and continues to
show the source problem. puzzle and palette input are disabled for the
transition. only after the destination becomes current does the palette rebuild
once from the destination's persistent state and show its saved selection and
derived supplies.

## existing play behavior

preserve:

```text
click palette shape / tab     select next palette entry
shift-tab                     select previous palette entry
r                             select next distinct orientation
shift-r                       select previous distinct orientation
left click on canvas          accept the highlighted exact proposal
right click on placed tile    remove that exact placement
ctrl-z                        undo
```

preserve the current exact proposal enumeration, positive-length joining rule,
pointer ranking, region fitting, placement hit testing, command application,
undo behavior, supply derivation, and `State::solved()` completion authority.

no transition, manifest, visual canvas, or audio hook may enter exact command
legality. screen positions continue to rank and hit-test already-exact values
only.

selection persists across navigation. pointer and hover state do not. mounting
or remounting a canvas rebuilds its transient proposals from the persistent
`Session` and selection.

## completion presentation

retain the exact text:

```text
complete!
```

present it centered over the moving problem canvas in a `RichTextLabel`.
increase it to approximately 80-point text and give it a conspicuous,
high-contrast bbcode outline. preserve the existing playful animated/rainbow
treatment unless it prevents the larger outlined text from rendering
correctly.

the completion control ignores pointer input. it is visible exactly when the
bound session's current exact state is solved, hides again after removal or
undo makes that state unsolved, and moves with the problem during navigation.

do not add a completion dialog, button, automatic navigation, unlock gate,
document-wide completion screen, confetti system, or persisted completion
flag.

## vertical scroll navigation

navigation is free. a problem need not be solved before the player may leave
it, and returning to it restores its state.

a mouse-wheel or ordinary two-finger trackpad scroll over the playable canvas
requests one adjacent problem:

```text
scroll down  → manifest index + 1
scroll up    → manifest index - 1
```

scrolling over the fixed palette or bottom instruction/status line does not
navigate. scrolling beyond the first or last manifest entry is a non-mutating
no-op. there is no wraparound, bounce page, navigation button, keyboard
navigation shortcut, scrollbar, minimap, page selector, or table of contents.

do not explain scrolling in the player-facing interface. add no instruction,
arrow, label, status text, tooltip, tutorial, first-use prompt, animation hint,
or other copy which tells the player to scroll or describes the navigation
direction. preserve the existing bottom control text without adding navigation
language. discovery is intentional.

one accepted scroll begins one short, eased vertical transition. while it is
active:

- puzzle, palette, and further navigation input are ignored;
- the source and destination sessions remain unchanged;
- the source and destination canvases move by the same offset;
- advancing moves the source upward and the destination in from below;
- reversing moves the source downward and the destination in from above; and
- the fixed palette and bottom line do not move.

transition animation changes presentation positions only. it never copies,
moves, reconstructs, resets, or snapshots a `Session`.

## pop-free handoff

the destination problem must be fully ready before either canvas begins moving.
the required order is:

```text
bind destination persistent state
    ↓
construct its complete canvas presentation
    ↓
give it the exact final viewport size
    ↓
fit and project its exact region
    ↓
rebuild its transient proposals and completion presentation
    ↓
place it one full viewport above or below the current canvas
    ↓
allow layout and drawing readiness
    ↓
begin one synchronized two-canvas translation
```

if destination layout, projection, or drawing is not ready, defer motion. do
not begin with an empty canvas and populate it after it has entered the visible
viewport.

the two full-size canvases must remain edge-adjacent throughout the animation,
inside one clipping parent, so their synchronized motion never reveals an
unowned gap, stale framebuffer, default-sized control, or background strip.

at successful completion:

1. place the destination exactly at the canonical zero offset;
2. make its manifest index the current index;
3. rebind the stationary palette to that persistent problem;
4. emit the completed problem-change hook;
5. only then detach and destroy the old canvas; and
6. restore input.

do not destroy the source first and replace it on a later frame. do not
recreate, reload, or reset the destination after it has become visible.

resizing during a transition must leave one valid current problem, a correctly
fitted destination or completed handoff, and no dangling borrowed state. the
exact resize policy is an implementation choice, but it may not expose
partially initialized content or mutate either session.

visual absence of popping is ultimately a windowed acceptance item. integration
tests must still prove the structural preconditions: two fully bound canvases
exist before motion, both have the final viewport size and exact adjacent
offsets, the destination is installed at zero before the source is freed, and
steady state returns to one canvas.

## semantic event hooks

add no audio engine, player, stream, bus, miniaudio dependency, music, sound
effect, resource, or placeholder asset.

expose a small Godot-connectable semantic event surface which either built-in
Godot audio or a future native audio engine may consume without changing play
legality:

```text
palette_selected
orientation_changed
placement_succeeded
removal_succeeded
undo_succeeded
active_proposal_changed
victory_reached
problem_changed
```

use separate named signals or an equivalently clear registered node-level API.
do not expose sound filenames, cue ids, volumes, variants, voice ownership, or
an audio-backend type. payloads may contain stable presentation indices or
booleans where useful, but never native pointers, rendered geometry, mutable
state, or an engine command.

emit from successful semantic operations, not raw input:

- keyboard and pointer selection route through the same operation and hook;
- selection emits only when the selected entry actually changes;
- orientation emits only when the representative orientation actually changes;
- failed placement, failed removal, empty undo, rejected navigation, and
  pointer motion which leaves the same active proposal emit nothing;
- active-proposal change reports a real change in the current ghost selection,
  including enough presence information for a consumer to ignore disappearance
  if desired;
- victory emits exactly on an unsolved-to-solved transition caused by a
  successful mutation or undo;
- returning to an unsolved state permits a later unsolved-to-solved transition
  to emit victory again; and
- problem change emits once, after the destination is installed as current,
  never merely because a transition was requested.

hooks observe results. they do not decide whether an action is legal, make an
action succeed, retain proposals, or become required for ordinary play.

## integration verification

extend the existing player integration and add one dedicated exam integration
scene/runner. production and tests must use the same manifest loading, problem
binding, player operations, transition preparation, handoff, and state
ownership paths.

prove at least:

### manifest and startup

- the authored manifest paths and relative order exactly match the list in this
  contract;
- every current manifest entry loads and compiles;
- every problem session begins with an empty arrangement;
- current index begins at zero;
- the first displayed problem number is derived from manifest size;
- a smaller test manifest derives its own descending numbers without a
  production count constant;
- duplicate, empty, or invalid manifest entries fail before partial play; and
- ordinary startup reaches the first problem.

### persistent state

using real player operations:

1. change selection on the first problem;
2. place at least one exact tile;
3. record arrangement entries and order, placement identities, `next_id`,
   undo depth, derived supplies, selection, and solved state;
4. navigate forward and complete the handoff;
5. mutate the second problem independently;
6. navigate backward and complete the handoff; and
7. prove every recorded first-problem value is unchanged.

also prove that the second problem retained its own independent state and that
destroying presentation nodes never destroys either problem slot.

### fixed shell and transition

- clicking every palette preview selects its exact authored entry;
- tab and click share selection behavior;
- palette and bottom controls are outside the animated layer;
- rejected endpoint navigation changes no current index or session;
- transition preparation mounts exactly two fully bound, final-sized canvases;
- forward canvases begin at `0` and `+viewport height`;
- reverse canvases begin at `0` and `-viewport height`;
- input cannot mutate a session during transition;
- handoff installs the destination at zero before freeing the source;
- completed handoff leaves exactly one canvas;
- the palette then reflects the destination's saved selection and supplies; and
- navigation back uses the exact opposite vertical direction.

do not make a wall-clock-sensitive test depend on visually sampling arbitrary
tween frames. expose or use a deterministic completion seam around the same
production handoff where necessary; do not create a test-only state mutation or
alternate navigation algorithm.

### presentation and hooks

- the completion overlay follows exact solved/unsolved changes;
- it carries the required large outlined presentation and ignores pointer
  input;
- successful selection, rotation, placement, removal, undo, active-proposal
  change, victory, and completed navigation produce their semantic hooks;
- their corresponding no-op or failure paths do not;
- clicking and keyboard selection produce the same hook; and
- remounting a problem does not emit invented gameplay or victory hooks.

## complete verification

with the Godot editor closed before native rebuilds, run the established
equivalents of:

```text
scons tests
./build/tests/tiles_tests
scons levelcheck
python3 tools/level_check/check_levels.py
scons
./Godot.app/Contents/MacOS/Godot --headless --path godot-project \
    res://tests/resource_integration.tscn
./Godot.app/Contents/MacOS/Godot --headless --path godot-project \
    res://tests/level_editor_integration.tscn
./Godot.app/Contents/MacOS/Godot --headless --path godot-project \
    res://tests/level_player_integration.tscn
./Godot.app/Contents/MacOS/Godot --headless --path godot-project \
    res://tests/exam_integration.tscn
./Godot.app/Contents/MacOS/Godot --headless --path godot-project --quit-after 2
```

also:

- verify all current level-check rows remain unchanged;
- compile modified translation units with the project's established c++17,
  no-exceptions, and warning settings where applicable;
- inspect the complete implementation diff;
- run `git diff --check`;
- verify no temporary files, build products, imported cache, or exported
  bundles were added; and
- verify the implementor made no commit.

manual windowed acceptance must:

1. boot directly into the first manifest level as the highest-numbered problem;
2. click palette shapes and use keyboard selection;
3. place, remove, rotate, and undo normally;
4. solve a problem and inspect the comically large outlined completion;
5. scroll down over the playable canvas and observe the current problem leave
   above while the ready destination enters from below;
6. scroll up and observe the exact reverse;
7. confirm no blank frame, incorrect size, projection jump, late tile
   appearance, or one-frame stale destination occurs;
8. confirm the palette and bottom line never translate;
9. confirm scrolls over those fixed controls do not navigate;
10. attempt rapid repeated scrolling and confirm one coherent handoff;
11. navigate away from changed problems and confirm their arrangements,
    selection, supplies, undo, and completion survive return; and
12. visit the first and final endpoints and confirm scrolling outward is a
    quiet no-op.

## boundaries

act 2 does not:

- implement audio playback or choose between Godot audio and miniaudio;
- add or import music or sound-effect assets;
- add title, acknowledgements, contents, problem prose, tile names, catalog
  names, difficulty labels, remaining area, or remaining-problem display;
- add player-facing text, arrows, prompts, tutorials, or hints which explain
  scroll navigation;
- add buttons, keyboard shortcuts, scrollbars, page dots, minimaps, wrapping,
  completion gating, or automatic problem advance;
- add save data or persistence across application launches;
- add runtime manifest editing, directory discovery, hot reload, lazy resource
  loading, background loading, or resource eviction;
- assume the current manifest has exactly 24 entries in application logic;
- change canonical level geometry, palette order, supplies, colors, blueprint,
  filenames, or exact play rules;
- change `State`, `Session`, command legality, proposal legality, completion
  authority, or the authored witness/player-state boundary;
- redesign the editor;
- redesign exporting, add platforms, sign, notarize, publish, or create a
  release;
- update the mutable final-exam roadmap;
- modify an earlier immutable contract;
- commit, push, merge, rebase, squash, or rewrite history; or
- begin later visual, document, audio, or release work.

## acceptance summary

act 2 is complete exactly when:

```text
one ordered manifest of current authored levels
    ↓ eager public loading
one stable exact Session per problem

ordinary boot
    ↓
manifest index 0 displayed as problem n

scroll on playable canvas
    ↓
fully ready adjacent canvas
    ↓
gapless synchronized vertical handoff
    ↓
destination becomes current

palette + bottom controls remain fixed
    ∧ only one steady-state canvas remains
    ∧ returning restores exact state and selection

clickable shape previews
    ∧ large outlined exact completion
    ∧ backend-neutral semantic hooks
    ∧ no audio implementation

all regressions and new integration proofs pass
    ∧ manual transition shows no pop
    ∧ no commit
```
