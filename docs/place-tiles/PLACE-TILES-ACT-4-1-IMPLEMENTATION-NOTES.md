# place tiles act 4-1 — implementation notes

Notes from adding the first player-driven construction loop: const commit-ready
join preview in core and engine, game-owned selection, exhaustive full-edge and
vertex proposal enumeration with exact deduplication, mouse ranking over the
cached proposals, click-to-apply, one translucent authoritative ghost, and a
temporary top-left selected-tile cell.

Everything below is verified against the built test executable, the default
GDExtension build, headless runs, and an out-of-repo probe. **No visual or
interaction verification was performed by the implementor.** See "handoff to
carson".

Two changes were made **after** carson reviewed the running act and are
deliberate departures from the act document. They are recorded first, before the
verification, so nothing below reads as if the spec were followed unchanged.

## post-review changes — deviations from the act document

Carson ran the completed act, confirmed it worked and performed well, and then
asked for two changes. Both were made knowingly and are listed here rather than
buried in the sections they touch.

### 1. isolated point contacts are no longer offered

**The act document says**, under "exact meaning of exhaustive joining": *"do not
classify or suppress isolated point contact. do not require a positive length of
shared boundary."* Its acceptance list repeats *"legal isolated point contacts
are retained"*, and its out-of-scope list names *"rejection of isolated point
contact as an interaction policy"*.

**Carson's instruction after seeing it run:** *"lets remove the single point
Placements. it's nice that they work but from a tiling standpoint theyre
effectively useless."*

So `Editor` now offers a proposal only when its footprint meets some placed
footprint along a shared segment of **positive length**. What this is and is not:

- it is **game-layer policy over already-proven placements**, applied after
  enumeration and deduplication. Core and engine are untouched: they still derive
  point-contact placements, still admit them, and `try_join_vertices` /
  `State::apply` will still happily insert one. Only the editor's offered set
  shrinks. Nothing in the native suite changed, and nothing needed to;
- it is **exact**. The test is the existing core predicate
  `classify_segments(...) == SegmentRelation::collinear_overlap`, which is
  defined as "collinear and sharing a subsegment of positive length" and is
  already covered by `tests/test_predicates.cpp`. No new geometry was written, no
  distance, tolerance, epsilon, or projected value is involved, and nothing was
  added to core;
- it is **applied after deduplication**, which is equivalent and much cheaper:
  the verdict depends only on the placement, so every proof of one translation
  shares one footprint and would reach the same answer;
- it discards **only** proposals whose contact with the *entire* arrangement is
  point-only. A vertex-derived proposal that happens to produce full-edge or
  partial-edge contact is kept, which is most of them.

The act's stated purpose for exposing point contacts was so carson could
evaluate them. He did, and this is the evaluation.

### 2. every drawn tile is outlined in a darkened form of its own color

**Carson's instruction:** *"do you think you could add a polyline outline to each
tile? it would be nice if it matched it's corresponding color too [...] outline
color = color*.5, just to get something simple working."*

The act document neither asked for nor forbade this; it permitted *"one simple
presentation-only outline"* on the ghost only. Placed footprints and the
selection-preview tile now get one too. It is purely presentational: one
`draw_polyline` over a closed copy of the already-projected boundary, at
`Color(r * 0.5, g * 0.5, b * 0.5, a)` and width 2. No second geometry
representation, no triangulation, no new palette.

**One judgment call inside this:** the ghost keeps its **white** outline rather
than a darkened one. Now that every tile is outlined, a same-hue ghost outline
would make the active proposal harder to pick out. `OUTLINE_WIDTH`,
`OUTLINE_SHADE`, and the ghost's color are three adjacent constants if carson
disagrees.

### one unrequested change that came with the first

The point-contact test, written naively, cost **4–10 ms per rebuild** — a
marginal frame at 60 Hz on every keypress. A closed bounding-box prefilter (skip
any placement whose extents cannot even touch the candidate's) brought it to
**0.2–1.3 ms**, roughly 10x. The counts are bit-identical before and after, so it
is a pure conservative skip on the same lattice values, not an approximation: two
closed boxes that do not meet cannot share a single point, let alone a segment.

## verification

- `scons tests` → `./build/tests/tiles_tests` reports **1738 checks, 0 failed
  across 192 cases**. The act-4 baseline was 1569 / 173, so this act adds **169
  checks across 19 cases** and weakens nothing: every pre-existing case still
  passes unmodified, including all the apply tests that now share the preview
  path. The post-review changes did not move this number, and correctly so —
  both live in `src/game/`, which is deliberately outside the test target, and
  neither touches core or engine behavior.
- `scons` builds the extension cleanly.
- Independently recompiled the changed sources with
  `g++ -std=c++17 -fno-exceptions -Wall -Wextra -fsyntax-only`:
  **zero diagnostics originating in `src/game/`, `src/core/`, or
  `src/engine/`**. The only output is `vendor/godot-cpp`'s own unused-parameter
  noise in its macro-generated headers, which predates this act.
- Godot executable: `./Godot.app/Contents/MacOS/Godot`, **v4.7.1.stable.official
  (a13da4feb)** — the same repository-local bundle act 4 used. There is no
  `Godot` on `PATH`.
- Grep-verified: still **no Godot include, type, or identifier anywhere under
  `src/core/` or `src/engine/`**. No selection, cursor, ghost, proposal, or
  ranking concept exists below `tiles::game`.
- Headless run — `./Godot.app/Contents/MacOS/Godot --headless --path
  godot-project --quit-after 2` — output in full:

  ```text
  Godot Engine v4.7.1.stable.official.a13da4feb - https://godotengine.org

  [tiles] handcrafted tetromino state constructed: palette order 7
  [tiles] debug grid: 19 distinct palette orientations placed across 7 columns
  [tiles] overlap fixture: second o rejected with interior_overlap against placement 19 (expected)
  [tiles] overlap fixture: entry count and next_id unchanged by the rejection
  [tiles] bootstrap complete: 20 authoritative placements available to draw
  [tiles] selection: palette entry 0 (prototile id 1), orientation 0 of 1
  [tiles] proposals (initial): full-edge 512 attempted / 28 succeeded, vertex 512 attempted / 243 succeeded, 168 distinct after collapsing 103 duplicate proofs, 45 dropped as point-only contact, 123 offered
  [tiles] active proposal: index 4 of 123, handle (36.0, 40.0), cursor (0.0, 0.0)
  [tiles] first draw: 20 footprints projected, selection preview projected, active ghost projected
  ```

  No load, symbol, registration, error, or warning line. **Exit code 0.**
  `--quit` (one frame) produces the same nine lines and exit code 0.
- **The whole draw path executes headless.** The last line is real evidence that
  `_draw()` ran, projected all twenty footprints, projected the selection
  preview, and projected an active ghost — not merely that `_ready()` finished.
- **The four thousand ordinary preview rejections emit nothing.** 1024 commands
  were attempted at bootstrap and 753 of them failed; the log has one aggregate
  line, not 753.

## layout

```text
src/core/Arrangement.{h,cpp}   preview_join_full_edges / preview_join_vertices,
                               shared reject_insertion, try_join_* routed through preview
src/engine/State.{h,cpp}       const preview overloads for both mating commands
src/game/Editor.{h,cpp}        selection, enumeration, ranking, input, ghost, preview cell,
                               contact policy, per-tile outlines
tests/test_join.cpp            +6 full-edge preview cases
tests/test_vertex_join.cpp     +6 vertex preview cases
tests/test_commands.cpp        +7 engine preview cases
```

Both post-review changes are confined to `src/game/Editor.{h,cpp}` — the contact
policy as `bounds_touch` + `shares_edge_contact` and two extra `RebuildCounts`
fields, the outlines as `outline_for` + `closed_boundary` and three call sites.

`SConstruct`, `register_types.*`, `main.tscn`, `project.godot`, and
`tiles.gdextension` are **untouched**, as specified. No new production file, no
new registered type, and no Godot binding for any core, engine, selection,
proposal, or command value.

## core preview

- **`reject_insertion` is the single shared gate.** It performs the
  whole-footprint overlap scan and then the identifier check, in exactly
  `try_insert`'s original order, and returns the rejecting `ArrangementError`
  or nothing. `try_insert`, `preview_join_full_edges`, and
  `preview_join_vertices` all call it, so preview and mutation cannot drift
  apart in verdict, precedence, or conflict identity. Overlap is still decided
  before identifier exhaustion, and overlap still names the **first** conflicting
  entry in storage order.
- **`try_join_*` now derives nothing of its own.** Each is anchor-resolution-free
  and alignment-free: it calls its preview, and on success hands the resulting
  placement to `try_insert`. The two cheap conditions are re-proved by that
  insertion. I did **not** add an "insert already validated" seam — a second
  scan of twenty footprints is not worth an unsafe public entry point, and the
  spec explicitly permits the repeat.
- **Preview is observationally pure by construction, not by discipline.** Both
  operations are `const` member functions and touch nothing but locals, so
  entries, order, ids, `next_id()`, exhaustion state, and the supplied candidate
  are unreachable for mutation. Tests assert it anyway, and one deliberately
  calls preview through a `const Arrangement &`.
- **The returned placement has no identity and reserves none.** It is an ordinary
  `Placement`: the supplied oriented prototile unchanged, the exact alignment
  translation, and its own owned footprint.
- **`JoinError` is unchanged.** No preview-specific error, no `can_join` boolean,
  no assertion, no logging, no callback, and no contact enumerator was added to
  core.

## engine preview

- **`State::preview` shares `resolve_candidate` with `apply`**, so the precedence
  `entry -> orientation -> supply` is literally the same code, not a parallel
  copy. `resolve_candidate` stayed private and no mutable palette or arrangement
  view was exposed.
- **Preview consumes nothing.** Supply is derived from arrangement contents,
  which a `const` operation cannot change, so repeated previews of one command
  against an unmodified state are identical and stay successful. One test spends
  the second of two configured pieces *after* three successful previews, proving
  no preview spent it.
- **Preview/apply agreement is tested, not asserted into existence.** A test
  previews, then immediately applies, then compares the stored entry against the
  preview on prototile id, orientation, exact translation, and the complete
  ordered footprint boundary.
- `PlaceCommand` preview was deliberately **not** added.

## selection, enumeration, and deduplication

- **`Selection`, `JoinCommand`, and `JoinProposal` are private nested types in
  `Editor`.** No controller, input manager, selection model, proposal service,
  or renderer class exists. `std::optional<Selection>` guards the "no valid
  selection yet" case; there is no sentinel index anywhere.
- **Enumeration is neutral to prototile identity.** Feature bounds come from
  `footprint().vertices().size()` per anchor and
  `canonical_polygon().vertices().size()` for the candidate. No tetromino vertex
  count, edge length, cell size, or orientation count appears in the enumerator.
- **Enumeration order *is* the representative precedence.** Full-edge commands
  for every anchor first, then vertex commands for every anchor; within each
  family the arrangement's storage order, which is monotonic `PlacementId` order
  because entries are only ever appended and ids allocated monotonically; then
  anchor feature index, then candidate feature index. So the first surviving
  proof of a translation is already its representative, and deduplication is a
  plain "keep the first" scan rather than a ranking pass.
- **Deduplication is exact `Point` equality on `Placement::translation()`**, on
  the q16.48 lattice, never on a projected float. One selection fixes prototile
  identity and distinct orientation for a whole rebuild, so equal translation
  really does imply equal footprint.
- **The final cache is sorted lexicographically by exact translation raw x then
  raw y.** That, not a float, is what makes every presentation-distance tie
  deterministic.
- **Enumeration itself filters nothing for taste.** Vertex-derived full-edge
  contacts, partial-edge contacts, off-viewport placements, and distant
  placements all stay in the cache. Authoritative preview rejection is what
  removes candidates — at bootstrap, 484 of 512 full-edge and 269 of 512 vertex
  commands. The one exception is the point-only contact policy carson asked for
  after review, which runs as a separate pass *after* dedup and is described at
  the top of this document; the enumerator proper is still exhaustive.
- **Rebuilds happen on exactly four events**: bootstrap, entry change,
  orientation change, and an apply attempt. **Mouse motion never reruns preview**
  — it only ranks the cache.

## measured proposal enumeration

Produced by an out-of-repo probe that replays the exact bootstrap against
core+engine and enumerates every selection, so these are the model's numbers.
Row one matches the headless log exactly (512/28, 512/243, 168 distinct), which
cross-validates the runtime path against the probe.

The fixture has **20 anchors and 128 total anchor features** (4+4+8+8+8+6+6-vertex
footprints), so a candidate with `v` vertices costs `128 * v` full-edge plus
`128 * v` vertex previews per rebuild.

```text
mino entry orient verts | edge att/ok | vert att/ok | distinct | edge-rep vert-rep | pt-only offered | filt ms
o        0      0     4 |  512/  28 |  512/ 243 |      168 |       26      142 |     45     123 |    0.24
i        1      0     4 |  512/  21 |  512/ 230 |      171 |       20      151 |     37     134 |    0.20
i        1      1     4 |  512/  26 |  512/ 231 |      163 |       25      138 |     40     123 |    0.22
t        2      0     8 | 1024/  98 | 1024/ 376 |      198 |       61      137 |     58     140 |    1.27
t        2      1     8 | 1024/ 100 | 1024/ 379 |      193 |       64      129 |     57     136 |    1.11
t        2      2     8 | 1024/ 100 | 1024/ 381 |      199 |       65      134 |     58     141 |    0.87
t        2      3     8 | 1024/ 102 | 1024/ 383 |      203 |       69      134 |     62     141 |    0.79
s        3      0     8 | 1024/  92 | 1024/ 363 |      195 |       65      130 |     58     137 |    0.67
s        3      1     8 | 1024/  97 | 1024/ 373 |      203 |       69      134 |     56     147 |    0.71
z        4      0     8 | 1024/  90 | 1024/ 367 |      198 |       63      135 |     54     144 |    0.60
z        4      1     8 | 1024/  96 | 1024/ 367 |      199 |       65      134 |     59     140 |    0.55
j        5      0     6 |  768/  56 |  768/ 304 |      180 |       45      135 |     47     133 |    0.36
j        5      1     6 |  768/  55 |  768/ 304 |      190 |       43      147 |     45     145 |    0.36
j        5      2     6 |  768/  55 |  768/ 299 |      190 |       45      145 |     51     139 |    0.37
j        5      3     6 |  768/  53 |  768/ 302 |      185 |       42      143 |     47     138 |    0.34
l        6      0     6 |  768/  56 |  768/ 295 |      184 |       46      138 |     48     136 |    0.36
l        6      1     6 |  768/  52 |  768/ 298 |      181 |       40      141 |     47     134 |    0.35
l        6      2     6 |  768/  57 |  768/ 309 |      184 |       45      139 |     46     138 |    0.39
l        6      3     6 |  768/  57 |  768/ 299 |      187 |       44      143 |     49     138 |    0.34
```

`edge-rep` / `vert-rep` split the distinct proposals by which command family
proved the representative. `pt-only` is how many of those distinct placements the
post-review contact policy drops; `offered` is what actually reaches the player,
and `filt ms` is what the policy costs with its bounding-box prefilter.
Consequences worth recording:

- **Every selection has somewhere to go**: 163–203 distinct before the contact
  policy, **123–147 offered after it**. The bootstrap integration check ("at
  least one distinct proposal") is satisfied with enormous margin, which is
  exactly why it is written as a check and not a hardcoded number.
- **Vertex mating dominates by a wide margin.** It proves roughly three quarters
  of the representatives, and roughly four to five times as many raw successes as
  full-edge mating. The fixture is a grid of touching pieces, so nearly every
  corner of every footprint is a legal mating point, and most of those are point
  or partial contacts a full-edge command can never derive.
- **Point-only contacts were a smaller share than expected: 22–31%.** I had
  guessed most vertex-derived proposals would be bare corner touches. They are
  not — in a dense grid a corner mating usually lands the candidate flush against
  *some* neighbour, so it survives the policy. Dropping them removes about a
  quarter of the options, not most of them.
- **Duplicate proofs are real and substantial.** At bootstrap 271 successes
  collapse to 168 placements — 103 duplicates, ~38%. Larger candidates collapse
  more (t at orientation 3: 485 successes → 203 distinct, 58%).
- **Performance is a non-issue at this size.** The probe performs all nineteen
  complete rebuilds plus bootstrap — enumeration, dedup, sort, and the contact
  policy — in **0.067 s** at `-O2`, the level the default `template_debug` build
  actually uses (`optimize=speed_trace`). That is roughly **3–4 ms per rebuild**,
  and a rebuild happens only on a keypress or a click, never on mouse motion.
  Even a fully unoptimized `-O0` build ran the same nineteen rebuilds in 1.48 s
  (~78 ms each) before the contact policy existed. No index, memoization, thread,
  or job system was added, and none is needed.
- **The bounding-box prefilter is the reason that last point still holds.**
  Without it the contact policy alone cost 4–10 ms per rebuild — more than
  everything else combined, and enough to threaten a frame. The same table with
  the naive test read `7.78 / 6.23 / 5.23 / 9.73 / …` in the `filt ms` column,
  with every other column identical.

## verified godot 4.7 api decisions

Every signature and constant below was read out of the pinned
`vendor/godot-cpp` headers, not recalled:

- `virtual void Control::_gui_input(const Ref<InputEvent> &p_event)` —
  `gen/include/godot_cpp/classes/control.hpp:383`, bound with hash 3754044979.
- `Control::accept_event()`, `set_focus_mode(Control::FocusMode)`,
  `grab_focus(bool = false)`, and `Control::FOCUS_ALL = 2` — same header.
- `CanvasItem::get_local_mouse_position()`,
  `draw_colored_polygon(PackedVector2Array, Color, PackedVector2Array, Ref<Texture2D>)`,
  `draw_polyline(PackedVector2Array, Color, float = -1.0, bool = false)`, and
  `draw_rect(Rect2, Color, bool = true, float = -1.0, bool = false)`.
- `InputEvent::is_pressed()` / `is_echo()`, `InputEventKey::get_keycode()`,
  `InputEventWithModifiers::is_shift_pressed()`, `InputEventMouse::get_position()`,
  `InputEventMouseButton::get_button_index()`.
- `KEY_TAB = 4194306`, `KEY_R = 82`, `MOUSE_BUTTON_LEFT = 1` in
  `gen/include/godot_cpp/classes/global_constants.hpp`.
- Event dispatch uses `Ref<T>`'s converting constructor, which performs a
  `cast_to` and yields a null ref on mismatch, so `is_valid()` is the type test.

Decisions that follow from that surface:

- **The root control is given `FOCUS_ALL` and grabs focus in `_ready()`**, after
  the complete bootstrap succeeds. Without it, `_gui_input` never sees a key
  event: Godot routes keys to the focused control only. Tab and R are consumed
  with `accept_event()`, which should also stop Godot's own Tab focus cycling
  from running — worth an eye, since there is nothing else focusable to cycle to.
- **`_gui_input` positions are already control-local**, so no coordinate
  transform was written. Mouse motion is not consumed (it is not a "handled"
  interaction in the spec's sense); selection keys and the left click are.
- **A click applies the proposal the ghost is currently showing**, then adopts
  the click's own position for ranking the rebuilt cache. Click therefore always
  equals what was drawn, rather than re-ranking first and possibly placing a
  different proposal than the one under the cursor a frame earlier.

## presentation decisions

- **The proposal handle is the arithmetic mean of the projected footprint
  vertices**, exactly as specified, and deliberately not the area centroid. It is
  computed on demand during ranking; no projected geometry is cached, and no
  handle ever re-enters core or engine.
- **Ranking uses squared euclidean distance in `double`** with a strict `<`, so
  an exact float tie keeps the lower cache index and is thereby settled by exact
  translation order. There is no snap radius: with a nonempty cache exactly one
  proposal is always nearest, and the screen partitions into choice regions.
- **The ghost is the active proposal's authoritative footprint**, projected
  through the same `to_screen`, filled with the prototile's identity color at
  `alpha = 0.45`, with one white polyline outline at width 2. It is drawn after
  the arrangement and before the selection cell. There is no red invalid ghost
  and no simultaneous rendering of the other proposals.
- **Every filled tile is outlined** (post-review; see the top of this document).
  `outline_for` darkens the fill by `OUTLINE_SHADE = 0.5` on rgb and keeps alpha;
  `closed_boundary` repeats the first vertex, because Godot closes a filled
  polygon implicitly but draws a polyline open. Both helpers are shared by the
  arrangement loop, the selection cell, and — for the closing helper — the ghost.
  Adjacent tiles each draw their own outline, so a shared edge renders as a
  doubled 2-pixel seam; that is inherent to per-tile outlining and is a
  `OUTLINE_WIDTH` question, not a bug.
- **The selection cell occupies screen `[8,52]²`** with a 4-pixel inner margin
  over a dark background rect, before the arrangement's x origin at 60. It
  measures the oriented polygon's real bounding box across every vertex,
  uniformly scales by `min(36/w, 36/h)`, centers it, and applies the same
  cartesian-to-screen y flip. It is a local fit, not a second world projection:
  `PIXELS_PER_UNIT`, `SCREEN_ORIGIN_X`, and `SCREEN_ORIGIN_Y` are untouched, and
  no pixel it produces can reach a command.
- **`std::optional` is dereferenced with `operator*` or after an explicit
  `has_value()`, never `.value()`** — `std::optional::value()` has a throwing
  path that aborts under `-fno-exceptions`. This continues act 4's convention.
  `Result` is likewise always inspected before its value is touched.
- **No pointer or reference into the arrangement or palette survives a command.**
  `selected_candidate()` returns a palette pointer used transiently inside one
  function and never stored; proposals hold command values and owned
  `Placement`s; the active proposal is a `std::size_t` into the game-owned cache.
- **Still event-driven.** No `_process`, physics processing, timer, signal,
  observer, callback, or continuous redraw was added. `queue_redraw()` fires on
  bootstrap outcome, selection change, apply attempt, and only those mouse
  motions that actually change the active proposal.

## handoff to carson — unverified visual and interaction facts

I did not open a window, take a screenshot, or judge anything by eye. All of the
following need yours. The measured table above is ground truth for what *should*
be on screen.

Layout and rendering:

- the compact top-left selection cell is visible, legible at that size, and does
  not obscure the debug arrangement (it ends at x = 52; the grid starts at 60);
- the selected preview always matches the selected orientation, and preserves
  the shape — an upright `i` should look upright, not flattened by the fit;
- arrangement colors, winding, concave fills, and y direction are still correct
  after the draw path was reorganized (`project_footprint` is now shared between
  the arrangement loop and the ghost);
- the ghost reads as a *proposal* — translucent, white-outlined, clearly not a
  placed tile — and its color matches the selected prototile's identity color.
  It has to stay distinguishable now that every placed tile is outlined too;
- the `color * 0.5` outlines actually separate same-colored neighbours (the four
  `t` orientations sit in one column and are all purple), and the doubled seam
  where two tiles share an edge does not read as a gap or a smear;
- the outline on the small top-left selection tile is not so heavy that it eats
  the shape at that size.

Controls:

- `tab` / `shift+tab` cycle palette entries in the authored order
  `o, i, t, s, z, j, l` and wrap in both directions;
- changing entries resets the orientation to zero;
- `r` / `shift+r` cycle **only** distinct compiled geometry and wrap: `o` has
  **one** orientation and must not appear to change at all, `i`, `s`, and `z`
  have **two**, and `t`, `j`, `l` have **four**. Anything showing four
  orientations for `o` or `i` is wrong;
- Godot does not steal `tab` for focus navigation despite `accept_event()`;
- selection survives a placement, so the same oriented tile can be placed
  repeatedly without re-selecting it.

The interaction itself:

- moving the mouse makes the ghost **jump** between exact join locations and
  never slides continuously with the cursor as though free-placed;
- the jumps feel comprehensible rather than arbitrary — with 123–147 proposals
  in play the screen is finely partitioned, and this is the single most likely
  thing to feel bad;
- left click adds exactly the highlighted proposal, and the log's
  `[tiles] placed: ...` line names the family and new placement id;
- **no proposal that touches only at a corner is reachable any more**, which is
  the change you asked for — worth confirming by eye that nothing you *do* want
  went with it. The policy drops 22–31% of distinct placements per selection;
- full-edge, vertex-derived full-edge, and partial-edge joins can all still be
  observed on this fixture. Point joins deliberately cannot;
- overlap is never inserted, and repeated joins redraw without visible drift;
- rebuilding stays responsive on the twenty-anchor arrangement (measured at
  ~3–4 ms including the contact policy, so any perceived hitch is not
  enumeration);
- at startup with the cursor at the window origin the nearest proposal handle is
  `(36, 40)`, which lies **inside** the selection cell's `[8,52]²`; the cell is
  drawn last and will partly cover that ghost. Harmless, but if it looks like a
  bug at first frame, that is why.

## notes for the next act

- **The contact policy needs a spec of its own before it stops being a patch.**
  It currently lives as one game-side predicate with one sentence of intent
  behind it. The real question it answers — *which legal contacts are
  player-facing?* — is a domain question, not a rendering one, and the answer
  probably belongs near `DEFINITIONS.md`'s treatment of arrangement contact
  rather than in `Editor.cpp`. It is also, right now, the only rule in the
  codebase that admits a placement the engine would accept but the editor will
  not offer. That asymmetry is fine while there is one editor; it will not stay
  fine.
- **Vertex mating is still the dominant option generator.** ~140 of ~180
  representatives per selection come from vertex commands, and the contact policy
  only removes about a quarter of the total. If ~130 options per selection still
  feels like too many, the remaining levers are a snap radius, a proposal cycling
  control, or a smaller fixture — and the fixture is the honest one.
- **The policy is untested by the native suite, structurally.** `src/game/` is
  excluded from the test target, so `shares_edge_contact` is verified only
  through headless counts and the fact that its one predicate,
  `classify_segments`, is covered in `tests/test_predicates.cpp`. If this rule
  is going to survive and grow, it wants to move somewhere testable — which is
  another way of saying it wants a spec.
- **The debug grid has outlived its usefulness as an interaction fixture.**
  Twenty anchors is a good stress test and a terrible level. When one starting
  tile replaces it, the proposal counts drop by roughly an order of magnitude and
  the ranking regions get much larger, which will change how the mouse feels far
  more than any tuning here would.
- **`PlaceCommand` remains the only way to express partial-edge contact with no
  coincident vertices**, and it is still not reachable from input. The act-4
  fixture's own `i`/`o` contact is exactly such a case, so the editor currently
  cannot reproduce a placement its own bootstrap makes. That gap is the next
  honest question about what "place a tile" means.
- **Deduplication by translation is only sound because selection is fixed for a
  rebuild.** If a future act ever enumerates several orientations or entries at
  once, translation alone stops identifying a physical placement and the key has
  to grow — this is worth remembering before that change looks trivial.
- **The proposal cache is the one optimization present and it is load-bearing.**
  Without it every mouse-motion event would re-run 1024–2048 exact previews.
  Anything that later invalidates the cache more eagerly needs to justify itself
  against that number.

## out of scope (correctly absent)

No free placement or player-facing `PlaceCommand`; no screen-to-model
conversion, inversion, rounding, snapping, or quantized mouse coordinate; no
sliding search along an edge interior; no automatic partial-edge contact; no
targets, regions, levels, or solution checking; no deletion, movement,
replacement, undo, redo, or reset; no placement selection, hit testing, or
dragging; no supply labels or production palette; no buttons, labels, panels,
menus, toolbars, or child nodes; no controller, touch, accessibility, or
configurable input; no red invalid ghosts and no simultaneous rendering of every
proposal; no snap radius or tuned ranking heuristic; no cycling control for
coincident handles; no zoom, pan, camera, or responsive layout; no render cache,
render-view dto, per-placement nodes, meshes, textures, materials, shaders, or
explicit triangulation; no `_process`, timers, or animation; no signals,
callbacks, observers, services, or event bus; no scene navigation or
persistence; no tier-2 or tier-3 geometry; no tier-1 orthogonality validation;
and no change to any completed core or engine contract beyond the preview
operations this act specifies.

**The act's out-of-scope list also named "rejection of isolated point contact as
an interaction policy", and that one is now present** at carson's explicit
instruction. It is game-side only; the core and engine contracts around point
contact are unchanged. See the top of this document.
