# final exam act 2 implementation notes

## what exists now

```text
exam.tscn                  the application: manifest, problem states, fixed shell
  Viewport (clipped)         one moving canvas in steady state, two mid-handoff
  Palette                    fixed, clickable shapes, current problem only
  Status                     fixed, unchanged control text

level_player.tscn          one moving problem canvas: number, target,
                           arrangement, ghost, completion
```

`Exam` owns the ordered manifest, the canonical catalog, one `ProblemState` per
manifest entry at a stable address, the current index, navigation, and the one
visible shell. `LevelPlayer` became the moving problem canvas: it owns nothing
persistent, borrows the `ProblemState` it presents, and applies every mutation to
that problem's real `Session`.

new files:

```text
src/game/ProblemState.h                        persistent per-problem state
src/game/Exam.{h,cpp}                          the application
src/game/testing/ExamIntegrationRunner.{h,cpp} the exam proof
godot-project/exam.tscn                        the main scene
godot-project/tests/exam_integration.tscn
```

## ownership

`ProblemState` holds exactly three things: the exact `engine::Session`, the
persistent `PaletteSelection`, and the authored colors in palette order. Supply,
arrangement, undo depth, `next_id()`, and completion are read out of the session
every time they are needed — nothing is mirrored, so nothing can drift.

The slots live in a `std::vector<std::unique_ptr<ProblemState>>`, so the vector
may grow without moving what a canvas points at. A canvas holds a raw
`ProblemState *` and is created and destroyed freely around it.

`LevelPlayer` no longer has `level_path`, no longer loads anything, and no longer
owns a catalog, colors, a session, or a selection. `bind(state, number, observer)`
replaces `load()`. Everything transient — proposals, the active proposal, the
pointer, the projection — is rebuilt at bind time from the persistent state.

### the one seam back to the shell

The palette rows and the status line are the shell's, but the operations which
make them stale are the canvas's. `ProblemStateObserver` is a two-line pure C++
interface with one method, `on_problem_presentation_changed()`. The canvas calls
it after any operation which changed what the shell shows; the shell re-derives
everything from the problem state.

It is deliberately not a signal. The act document is explicit that hooks observe
results and that no part of ordinary play may depend on them, and the palette
refresh is ordinary play. A canvas bound with a null observer — which is exactly
what the player integration runner does — plays identically.

## input

The exam is the single input router. Canvases are `MOUSE_FILTER_IGNORE` and
handle no input of their own, because two of them exist during a handoff and
neither should be an input target while it is moving.

```text
key   → Exam::_gui_input → current canvas operation
click → Exam::_gui_input → viewport rect test → canvas-local → operation
wheel → Exam::_gui_input → viewport rect test → navigate(±1)
```

Scroll navigation is refused unless the event position is inside the playable
viewport rect, which is what keeps a scroll over the palette or the bottom line
from navigating. That is an explicit rect test rather than a reliance on child
mouse filters, because `Label` defaults to `MOUSE_FILTER_IGNORE` and a scroll
over the status line would otherwise fall through to the exam.

Both `InputEventMouseButton` wheel events and `InputEventPanGesture` are handled;
macOS emits wheel events with a fractional `factor` for trackpads, and may emit
both forms for one physical gesture. One accepted scroll therefore sets a short
cooldown (0.18 s) which begins when a handoff completes: one wheel detent is one
problem, a duplicate event for the same gesture is dropped, and the momentum tail
of a flick cannot queue three more problems. The cooldown gates *raw scroll
input only* — `navigate()` itself is gated only by the endpoints and by whether a
transition is already running.

The control hint is byte-identical to what it was. The completion line now reads
`complete! (scroll down for next problem)` — carson changed that string after
implementation. The act document's "discovery is intentional" rule bound the
implementor, not the author; it is recorded here rather than silently reverted,
because it is the one piece of player-facing copy which now names the navigation.

## the palette rows

Each row is a `PanelContainer` holding one centered `HBoxContainer`:

```text
┌─ 244 × 120 ─────────────────────────┐
│        ┌──────────────┐             │
│        │  144 × 112   │  6          │   ← selected: row tinted
│        │  the shape   │             │
│        └──────────────┘             │
└─────────────────────────────────────┘
```

The shape is the button; the only text is the remaining supply, as a bare
number. The `× ` prefix is gone — `godot::String(const char *)` reads its bytes
as Latin-1 rather than UTF-8, so the multiplication sign arrived as `Ã—`. Any
non-ASCII literal reaching a `godot::String` that way will do the same thing.

The selected row's `StyleBoxFlat` is filled with `LevelPlayer::region_fill_color()`
— literally the color the exact target is drawn with, exposed as one static
accessor so the palette and the canvas cannot drift apart. Every other row's
background is fully transparent. The unselected shapes stay dimmed by modulate as
well, so selection reads twice.

## the handoff

`navigate(±1)` prepares the destination completely before anything moves:

```text
instantiate the canvas scene
add it to the clipped viewport
give it the exact final viewport size
bind it to its persistent problem   ← fits, projects, rebuilds proposals,
                                       sets its number, sets completion
place it one full viewport above or below
```

Readiness is established synchronously rather than deferred: by the time the
canvas has a position it has already fitted, projected, and rebuilt everything it
draws, so there is no frame on which an empty or wrongly sized canvas is inside
the viewport.

`advance_transition(delta)` is the production step, called from `_process` with
the frame delta. Both canvases move by the same offset (cubic ease-in-out, 0.28 s),
so the edge they share never opens; both are opaque over their whole rect and the
viewport clips them. At completion, in this order: destination to zero, index
becomes current, palette rebuilt from the destination, `problem_changed` emitted,
*then* the source detached and freed, then the scroll cooldown armed.

A resize during a transition finishes it immediately. That is the whole resize
policy: it leaves one valid current problem, a correctly fitted canvas, and no
dangling borrowed state, and it touches no session.

Tests reach the same completed handoff by calling `advance_transition(1.0)` — the
production function, run past its own duration. There is no test-only navigation
path and no sampling of arbitrary tween frames.

## semantic hooks

Eight signals, on `Exam`:

```text
palette_selected(entry)              orientation_changed(entry, orientation)
placement_succeeded(entry)           removal_succeeded()
undo_succeeded()                     active_proposal_changed(present, proposal)
victory_reached()                    problem_changed(index, number)
```

The seven gameplay hooks are declared and emitted by `LevelPlayer` and forwarded
by `Exam`, which connects them on every canvas it mounts. Both surfaces exist on
purpose: the canvas is where an operation knows it succeeded, and the exam is the
stable node a future audio consumer connects to once instead of reconnecting on
every navigation.

Payloads carry only integers and booleans. No native pointer, geometry, state, or
command leaves the C++ side, and nothing names a sound, cue, variant, volume, or
backend.

Emission rules as implemented:

- selection emits only when the selected entry actually changes; reselecting the
  same entry is silent, and click and tab route through the same `select_entry`;
- rotation emits only when the orientation index actually changes, so cycling an
  entry with one distinct orientation is silent;
- `select_entry` does not emit `orientation_changed` even though it resets
  orientation to 0. Selecting a tile should not fire the rotate cue;
- active-proposal change is detected by comparing the ghost's **exact
  translation** and presence across the operation, not its index — a rebuilt
  proposal list can reuse index 0 for a completely different ghost;
- victory is the unsolved→solved transition of the bound state. `bind()` reads
  the current solved value without emitting, so remounting a solved problem is
  silent and undoing then re-solving emits again;
- `problem_changed` is emitted once, after the destination is installed, never on
  request and never for a refused navigation.

Nothing emits during `bind()`.

## the manifest

One inspector-authored `PackedStringArray` on the exam scene, in the order the
act document names — the same order `level-notes.txt` records, `act0-level` first
through `perkins` last. No per-problem resource, no metadata, no schema, no second
copy in code.

`problem_number(i) = manifest size - i`. The number 24 appears nowhere in
`src/`: it is in `exam.tscn` and in the integration runner's restatement of the
authored content it checks. The runner also builds two- and three-problem exams
through exactly the same loading path and proves they number themselves 3, 2, 1
and 2, 1.

A manifest which is empty, contains an empty path, repeats a path, or names
content which does not load fails with the path and its typed codes and exposes
no exam at all: no canvas is mounted, `problem_count()` is 0, and
`ready_to_play()` is false.

## one judgment call

The canvas number reads `problem 24`, not a bare `24`. The act document asks the
canvas to present "the problem number derived from the manifest" and forbids
"problem prose", which I read as the mathematical statement — the thing act 4 and
act 5 will typeset. A bare number in the corner of a puzzle reads like a score;
the roadmap itself writes the countdown as `problem n`. One line in
`LevelPlayer::bind` changes it if that reading is wrong.

## verification

with the Godot editor closed throughout:

| check | baseline | after |
| --- | --- | --- |
| `scons tests` + `tiles_tests` | 9921 checks, 0 failed, 427 cases | **unchanged** |
| `scons levelcheck` + `check_levels.py` | all 24 pass | **all 24 pass, every row byte-identical** |
| `scons` | clean | **clean, no warnings** |
| `resource_integration.tscn` | 414 | **414** |
| `level_editor_integration.tscn` | 308 | **308** |
| `level_player_integration.tscn` | 32 | **36** |
| `exam_integration.tscn` | — | **166** |
| `--quit-after 2` | boots `guy_f` in the player | **boots the exam: 24 problems, problem 24** |
| `git diff --check` | clean | **clean** |

The exam runner prints three deliberately invalid manifests before their expected
errors, in the same style the resource runner announces its expected refusal. It
exits 0.

No commit was made. No build product, exported bundle, or temporary file was
added: the one temporary harness described below lived in `godot-project/.godot/`,
which is gitignored, and was deleted.

### what headless cannot prove, and what I did about it

**Headless Godot routes no GUI mouse input at all.** I verified this directly: a
plain `Button` in a plain `Control`, sent a motion and a click through
`Viewport.push_input`, never fires, and `gui_get_hovered_control()` stays null.
Keyboard input does reach `_gui_input` normally.

So the C++ exam runner drives mouse and wheel events by calling `_gui_input`
directly. That exercises the real router — the viewport rect test, the wheel
branch, the transition lock, the coordinate mapping — but not Godot's delivery of
a mouse event to the exam. Delivery is what the manual pass covers. What makes it
credible in the meantime: keyboard delivery to the exam is proven in a real tree,
and every node's mouse filter was read back from a real instantiated scene —
exam `STOP`, viewport `IGNORE`, canvas `IGNORE`, number `IGNORE`, completion
`IGNORE`, palette `STOP`, status `IGNORE`, palette button `STOP` with
`FOCUS_NONE`, palette shape `IGNORE` with `FOCUS_NONE`. A click over the play
area has nothing in front of the exam, and a scroll over the palette is stopped
by the palette.

The frame-driven animation is not something the runner can reach either, since it
finishes transitions in one deterministic step. I proved it separately with a
temporary GDScript harness in `res://.godot/`, with `navigate` temporarily bound
to ClassDB, and watched a real scroll-free navigation animate across real frames:

```text
frame 1    children=2  [problem 24 y=0.0]      [problem 23 y=1108.0]
frame 6    children=2  [problem 24 y=-394.3]   [problem 23 y=713.7]
frame 14   children=2  [problem 24 y=-931.8]   [problem 23 y=176.2]
HOOK problem_changed index=1 number=23
frame 40   children=1  [problem 23 y=0.0]
```

The two canvases are exactly one viewport height apart on every sampled frame,
the motion eases, and the handoff settles to one canvas at zero. Both the
temporary binding and the harness were removed and the library rebuilt; `grep`
for the marker returns nothing.

The completion label was also read back from a real instance:
`get_parsed_text()` is exactly `complete!`, so every bbcode tag — `center`,
`outline_size`, `outline_color`, `wave`, `rainbow` — parsed rather than rendering
as literal text, and the font size override is 80.

## left to carson

The whole manual windowed pass, which is the only thing that can judge any of
this. Specifically the items the act document lists — and, most of all:

- **does the transition feel right?** 0.28 s with a cubic ease-in-out is a guess.
  One constant, `TRANSITION_SECONDS` in `Exam.cpp`;
- **does one scroll move one problem on your trackpad?** the cooldown is the other
  guess, `SCROLL_COOLDOWN_SECONDS = 0.18`. If a flick still advances two problems,
  raise it; if a deliberate second scroll feels ignored, lower it;
- **the comically large completion.** 80 pt, 24 px near-black outline, over the
  rainbow wave;
- **the palette shapes are buttons now.** They are flat and focus-free, but a
  `Button` still draws a hover highlight. If that reads badly, it is a theme
  override, not a structure change.
- **the palette row group is centered, not the shape alone.** The shape's own
  center sits about 7 px left of the palette's, because the supply number is
  inside the centered group. If you want the shape dead-center with the count
  hanging off its right, that is one expanding spacer in `build_palette`.

## for the next formalizer

- **audio has a place to plug in and nothing to do yet.** Eight signals on the
  exam, emitted from results. An audio node connects once and never learns that
  canvases are created and destroyed. The cue list in the roadmap maps onto them
  one-to-one, except that the roadmap's "remove or undo" is two separate hooks
  here, which a consumer may collapse.
- **the shell is now the thing to redesign, not the player.** `Exam` owns the
  palette rows, the status line, and the playable rect in one function,
  `update_shell_layout()`. The canvas owns only what moves with a problem. Act 4's
  document host replaces the viewport and the transition; it does not need to
  touch play.
- **`ProblemState` is deliberately small.** If persistence across launches is ever
  wanted — carson has said no — it is the one thing that would need serializing,
  and `Session` is the only part that is hard.
- **the exam has no notion of document-wide completion**, no remaining-area and no
  remaining-problem display, because act 2 was told not to. Two of the three
  countdown scales in the roadmap are therefore still unpresented; the third, the
  palette supply, is exactly what the fixed palette shows.
