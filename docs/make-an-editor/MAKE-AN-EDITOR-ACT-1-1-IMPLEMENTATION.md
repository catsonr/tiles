# make an editor act 1-1: embedded palette-building editor

## status and supersession

this is the immutable implementation plan for the editor which should actually
be implemented.

it supersedes the earlier
`MAKE-AN-EDITOR-ACT-1-IMPLEMENTATION.md` wherever that plan treats palette
authoring as external. the earlier plan remains immutable history and remains
the base contract for:

- region drawing;
- exact loop validation;
- camera, grid, snapping, and projection;
- one-shot accepted boundaries;
- transactional region replacement;
- file dialogs;
- save and play compilation gates;
- typed play request;
- diagnostics;
- headless integration;
- manual interaction verification; and
- all unrelated scope exclusions.

do not implement the obsolete external-palette workflow and then layer this
act on top. implement one `LevelEditor` directly from the combined contract,
using this document when the two plans conflict.

the superseded decisions are:

```text
old:
  new from PaletteResource
  palette must compile before document acceptance
  palette is an unchanged external reference
  palette is summarized but not edited

required:
  new level owns an embedded mutable PaletteResource
  palette may be temporarily empty while authoring
  LevelEditor builds palette from the complete canonical catalog
  each included entry chooses supply and per-level color
```

## implementor context seed

act 0-3 must be complete before this act. the editor saturates its palette panel
from the exact 34-entry canonical catalog and stores color in the expanded
`PaletteEntryResource`.

before editing:

1. perform every context-seeding and baseline step required by the base act-1
   plan;
2. read this document completely;
3. read the completed act-0-3 plan and implementation notes completely;
4. inspect the final catalog, palette-entry resource, palette compiler,
   persistence, and integration APIs rather than relying on either conceptual
   plan spelling;
5. inspect the pinned Godot 4.7 APIs for `ScrollContainer`, `CheckBox`,
   `OptionButton`, `SpinBox`, `ColorPickerButton`, no-alpha color hints,
   containers, focus, and dynamically created child controls; and
6. establish the complete act-0-3 baseline.

if act 0-3 is absent or incomplete, stop. do not retain catalog color as a
fallback, omit newer catalog entries, or invent temporary ids.

the implementation notes remain:

```text
docs/make-an-editor/MAKE-AN-EDITOR-ACT-1-IMPLEMENTATION-NOTES.md
```

because act 1-1 supersedes act 1 before implementation; there is one editor
implementation and one final implementation record.

## goal

the complete internal authoring workflow is:

```text
boot or new
      │
      ▼
embedded empty PaletteResource
      │
      ├── include catalog entries
      ├── choose unlimited or positive finite supply
      └── choose one opaque color per included entry
      │
      ▼
draw one outer boundary and optional holes
      │
      ▼
compile complete LevelResource
      │
      ├── save/save as
      └── typed play request
```

palette and region authoring are independent:

- a region may be drawn while the palette is empty;
- a palette may be completed before any region exists;
- neither erases or rebuilds the other; and
- save and play require both to compile together.

the editor prevents every ordinary palette error structurally:

```text
duplicate id       impossible: one row per canonical entry
unknown id         impossible: rows come only from catalog
invalid supply     impossible: controls admit unlimited or positive finite
noncanonical order impossible: resource rebuild follows catalog order
empty palette      representable draft, but save/play disabled
```

## document ownership

a new document owns:

```text
LevelResource
    ├── embedded unpathed PaletteResource
    │       └── embedded unpathed PaletteEntryResource values
    └── null or accepted RegionResource
```

“embedded” means the palette and its entries have no independent resource path.
ordinary `ResourceSaver` therefore persists them as subresources of the level.

the editor never asks the author to:

- create a palette file;
- choose a palette file;
- save a palette separately;
- manage a shared palette dependency; or
- understand nested resource ownership.

the active level remains the one authoritative mutable transport graph.
palette UI state is a direct authoring view used to rebuild its palette member;
it is not another serialized format.

## revised startup and new behavior

replace the base act-1 empty startup with immediate construction of a new
unsaved document.

on successful `_ready()`:

1. construct the canonical catalog;
2. construct an empty correctly typed `PaletteResource`;
3. construct a `LevelResource` containing that palette and a null region;
4. install row state for every catalog entry;
5. mark the document dirty and unsaved;
6. show the complete palette panel;
7. center the canvas on game origin;
8. leave boundary drawing idle;
9. disable save and play because both palette and region are incomplete; and
10. show instructions equivalent to:

    ```text
    choose at least one tile, then draw the outer boundary.
    ```

the toolbar action is renamed:

```text
new level
```

it constructs the same empty document after the base act's one ordinary discard
confirmation when the current document is dirty.

remove:

- `new from palette…`;
- the palette-open `FileDialog`;
- “no document” as a normal post-initialization state; and
- the compact read-only palette-summary label.

initialization failure may still leave no usable document and must remain
non-crashing.

## palette panel layout

the main scene uses a simple two-part body:

```text
┌──────────────── palette panel ───────────────┬──── region canvas ────┐
│ scrollable canonical rows                   │ integer grid          │
│                                              │ accepted region       │
│ include · shape · name · supply · color     │ active loop           │
└──────────────────────────────────────────────┴───────────────────────┘
```

retain the base act's top toolbar, instructions, status, dialogs, and ordinary
Godot controls.

the palette panel:

- has a fixed comfortable width near `360` logical pixels;
- uses one `ScrollContainer`;
- follows canonical catalog order;
- creates exactly one row per catalog entry;
- remains usable at the project’s normal window size;
- scrolls rather than shrinking rows into illegibility; and
- does not cover the region canvas.

use standard visible labels. do not rely only on icons, color, hover text, or a
hidden selection mode.

## prototile preview

each catalog row includes a small shape preview because mirrored pentominoes
cannot be chosen reliably from names alone.

add one small reusable presentation control if needed:

```text
src/game/PrototilePreview.h
src/game/PrototilePreview.cpp
```

`PrototilePreview`:

- derives from `godot::Control`;
- receives one exact canonical polygon through a C++-only setter or owned value;
- measures its complete exact bounding box;
- projects it with cartesian y upward;
- uniformly fits and centers it in its assigned rectangle;
- draws one neutral fill and clear outline;
- never performs input, lookup, mutation, or resource compilation; and
- tolerates absent geometry.

it may be reused later by `LevelPlayer`, but act 1-1 adds no speculative
selection or animation behavior.

do not rasterize icons to files, add one scene per prototile, cache Godot polygon
geometry as authority, or infer the shape from its display name.

## palette row

each row visibly contains:

```text
[include] [shape preview] [display name]
          [supply mode] [finite amount] [color]
```

exact controls:

- one `CheckBox` for membership;
- one shape preview;
- one ordinary display-name label;
- one `OptionButton` with:
  - `unlimited`;
  - `finite`;
- one integer-step `SpinBox` for finite amount;
- one no-alpha `ColorPickerButton`.

behavior:

- excluded rows disable supply and color controls but retain their last local
  choices;
- including a row restores those choices;
- newly initialized rows default to unlimited supply;
- newly initialized finite amount is `1`;
- finite amount is constrained to an integer in `[1, 1_000_000]`;
- the spinbox is enabled exactly in finite mode on an included row;
- the color picker is enabled exactly on an included row;
- color alpha is always forced to `1`;
- changing any active control rebuilds the resource palette once; and
- row controls have short ordinary tooltips only as supplements to visible
  labels.

the editor’s finite upper bound is an authoring convenience, not a resource or
engine invariant. resources authored elsewhere may still encode any positive
signed 64-bit supply admitted by act 0-2.

do not add drag reordering, multi-select, search, filters, family tabs,
presets, copy/paste, randomize buttons, keyboard-only row commands, or a second
details inspector.

## default row colors

canonical content owns no color.

initialize each row with one deterministic opaque generated color based only on
its zero-based catalog position. use an evenly distributed hue construction
equivalent to:

```text
hue = fractional_part((catalog_index + 1) × 0.6180339887498948)
saturation ≈ 0.65
value ≈ 0.95
alpha = 1
```

record exact constants in implementation notes.

this is an editor convenience:

- it produces immediately distinguishable defaults;
- it is not stored in canonical content;
- it does not affect identity;
- it does not change when unrelated rows are included or excluded; and
- the author may replace every color per level.

do not use nondeterministic random state, global themes, family tables, or the
temporary construction-fixture color function.

## palette row state

conceptually:

```haskell
data PaletteRow = PaletteRow
  { catalogIndex :: CatalogIndex
  , included     :: Bool
  , supply       :: Unlimited | Finite PositiveEditorAmount
  , color        :: OpaqueColor
  }
```

store rows in exact catalog order. ids and display names continue to come from
the catalog entry rather than being copied into mutable row identity.

resource reconstruction is:

```haskell
buildPaletteResource rows =
  [ PaletteEntryResource
      { prototileId = catalog[row.index].id
      , supply      = encode row.supply
      , color       = row.color
      }
  | row <- rows
  , row.included
  ]
```

the list-comprehension order is normative.

do not preserve click order, sort by name, group by area in the resource, or
store excluded entries with zero supply.

## palette publication

after one row change:

1. create a new unpathed `PaletteResource`;
2. create one new unpathed `PaletteEntryResource` per included row;
3. populate id, encoded supply, and opaque color;
4. append entries in catalog order to one correctly typed array;
5. assign the typed array to the candidate palette;
6. if no rows are included:
   - assign the empty candidate palette to the active level;
   - clear `compiledPalette`;
   - show a neutral “choose at least one tile” status;
   - mark dirty;
   - refresh save/play state; and
   - do not report an error-level diagnostic;
7. otherwise call `compile_palette_resource`;
8. on expected success:
   - assign the candidate palette to the active level;
   - install the returned exact palette;
   - mark dirty;
   - clear stale palette errors;
   - refresh controls; and
   - redraw previews only when visually necessary;
9. on unexpected compiler failure:
   - do not assign the candidate;
   - restore row controls from the last published palette state;
   - preserve the active resource and dirty state;
   - display the complete typed error; and
   - report one concise internal failure.

construct-and-replace avoids mutating an externally pathed palette which may be
present in an older opened resource. the first successful palette edit
therefore detaches the level into a new embedded palette snapshot.

do not mutate the live entry array and attempt rollback after compiler failure.

## opening existing levels

replace the base act's “palette must be valid and nonempty” draft-open rule.

raw draft loading:

1. load and dynamically cast `LevelResource` exactly as the base act specifies;
2. if palette is null:
   - create an empty embedded palette;
   - initialize default excluded rows;
   - mark the opened document dirty because its graph was repaired;
3. if palette exists:
   - accept an empty typed entry array as an editable incomplete palette;
   - otherwise compile it through `compile_palette_resource`;
   - reject unknown ids, duplicates, invalid supplies, null entries, or other
     compiler failures;
   - map every entry to its unique catalog row;
   - preserve its unlimited/finite supply and complete authored color;
   - leave unlisted rows excluded with default local choices;
4. preserve the base act's null-or-valid-region rule;
5. install only after every required check succeeds; and
6. preserve the current document on rejection.

an opened nonempty palette may use any authored entry order accepted by the
resource compiler. display rows still follow catalog order. if the author makes
any palette edit, candidate reconstruction publishes canonical catalog order.
opening and saving without a palette edit preserves the original palette
reference and order.

if an opened palette entry has non-opaque color, display its rgb with alpha
forced to one in row state. do not mutate the resource until the author changes
the palette. act 1-1-authored palettes are always opaque.

## region independence

replace the base act conceptual `EditorDocument` requirement that a compiled
palette always exists:

```haskell
data EditorDocument = EditorDocument
  { resource        :: Ref LevelResource
  , compiledPalette :: Maybe Palette
  , compiledRegion  :: Maybe Region
  , paletteRows     :: [PaletteRow]
  , openLoop        :: Maybe OpenLoop
  , dirty           :: Bool
  }
```

region controls require a document, not a valid palette.

the complete base region workflow is unchanged:

- integer-grid outer and hole drawing;
- exact region compiler;
- failed closure preserves loop and accepted resource region;
- accepted boundaries are one-shot;
- restart replaces the complete region transactionally; and
- region edits do not touch palette rows or palette resources.

palette edits likewise preserve region resource, compiled region, active loop,
camera, and snapped cursor.

## revised controls and instructions

the top toolbar is:

```text
new level
open draft…
restart region
add hole
save
save as…
play
```

save and play require:

```text
no open loop
∧ at least one included palette row
∧ compiledPalette exists
∧ compiledRegion exists
∧ compile_level_resource succeeds when invoked
```

the instruction label combines only the currently actionable facts:

```text
empty palette, no region:
  choose at least one tile, then draw the outer boundary.

valid palette, no region:
  palette ready — choose “restart region” to draw the outer boundary.

empty palette, valid region:
  region ready — choose at least one tile before saving or playing.

valid palette and region:
  level ready — add a hole, restart the region, save, or play.
```

active-loop instructions and pan/zoom instructions remain exactly as the base
act specifies.

do not display raw resource validity jargon during ordinary incomplete states.

## save and play gates

retain the base act algorithms exactly:

```text
compile complete LevelResource immediately before save
compile complete LevelResource immediately before play signal
```

additional requirements:

- an empty palette disables buttons and is rechecked in handlers;
- per-entry color is already stored in the resource passed to saver or signal;
- save never writes a separate palette file;
- play emits the exact current `LevelResource` with its embedded palette;
- color changes mark dirty;
- supply changes mark dirty;
- membership changes mark dirty; and
- palette-only changes never create region history or an open loop.

the engine `Level` returned during validation contains no color. do not treat
that as lost state; the emitted/saved resource is the presentation authority.

## scene and source delta

the combined act-1 implementation adds the base plan files plus, if used:

```text
src/game/PrototilePreview.h
src/game/PrototilePreview.cpp
```

`godot-project/main.tscn` adds:

- palette-panel container;
- scroll container;
- rows container; and
- any stable labels surrounding dynamically populated rows.

create the 34 catalog rows programmatically from the runtime catalog. do not
hand-author one scene node per stable id in `main.tscn`.

small private row-controller values may live in `LevelEditor`. do not register
one Godot class per row or create a palette MVC framework.

## integration coverage delta

extend the base act's `LevelEditorIntegrationRunner` to verify:

- ordinary startup owns one unsaved level with empty embedded palette;
- exactly 34 rows appear in catalog order;
- every row shows the correct display name and exact shape;
- mirrored pentomino previews are visibly/geometrically distinct through their
  exact preview input;
- all rows begin excluded, unlimited, finite amount `1`, and with deterministic
  opaque default colors;
- including one entry publishes exactly one resource entry;
- inclusion defaults to unlimited;
- finite mode encodes the selected positive amount;
- the finite control cannot produce `0`, a negative value, a fraction, or an
  amount above the editor maximum;
- color changes persist complete rgb with alpha one;
- excluding a row removes it rather than storing zero supply;
- excluding and re-including restores its local supply and color choices;
- including entries out of order still serializes catalog order;
- duplicates and unknown ids cannot be produced through row controls;
- empty palette is ordinary incomplete state and disables save/play;
- nonempty palette compiles and enables its half of the complete-level gate;
- palette edits preserve accepted region, open loop, and camera;
- region edits preserve palette entries, supplies, colors, and row state;
- `new level` restores all row defaults and null region after confirmation;
- open maps a valid resource palette into exact row state;
- open accepts an empty palette as editable;
- open repairs a null palette to empty and marks dirty;
- open rejects unknown, duplicate, null-entry, or invalid-supply palettes
  without replacing the current document;
- first palette edit after opening an externally pathed palette publishes a new
  embedded palette without mutating the external resource;
- save persists palette, colors, and region as one level graph;
- uncached load restores every row value and exact compiled level;
- play emits the exact resource containing current per-level colors; and
- the external-palette file dialog and summary-only UI do not exist.

keep all base region, save, play, cleanup, and manual-interaction checks.

## manual interaction delta

the visible pass must additionally establish:

1. the palette panel is understandable without separate instructions;
2. all 34 entries remain reachable through ordinary scrolling;
3. shape, name, membership, supply, and color belong visibly to the same row;
4. reflected pentominoes can be distinguished;
5. selecting several pieces does not require modal dialogs;
6. finite/unlimited switching is obvious;
7. color picking is ordinary Godot behavior and alpha is absent;
8. palette work does not resize, cover, or steal pointer input from the region
   canvas;
9. empty-palette feedback is clear rather than alarming; and
10. a complete colored level saves and reopens without color drift.

author one level palette containing:

- at least one tetromino;
- one chiral pentomino and its mirrored partner;
- the domino;
- `square 1`;
- the `o`/side-2 square; and
- one square of side `3` through `9`.

use both finite and unlimited supplies and choose visibly different colors.

## acceptance

the combined editor is complete only when all base act-1 acceptance conditions
hold after applying these replacements, and:

- startup immediately provides one new editable level;
- every canonical entry appears once with an exact shape preview;
- palette membership is built in-app;
- every included entry chooses unlimited or positive finite supply;
- every included entry owns one per-level opaque color;
- default colors are generated presentation values, not canonical metadata;
- palette entries serialize in catalog order;
- ordinary controls cannot create duplicate, unknown, zero-supply, or negative
  entries;
- empty palette remains editable but cannot save or play;
- new palettes and entries are embedded in the level;
- palette files and palette file dialogs are unnecessary;
- palette and region editing preserve one another;
- complete compilation gates every save and play request;
- saved and emitted resources retain exact per-level colors; and
- no color enters canonical or engine content.

## explicitly out of scope

in addition to the base act exclusions, act 1-1 does not implement:

- standalone palette-resource authoring;
- shared palette libraries;
- palette import/export;
- palette presets;
- palette drag reordering;
- duplicate catalog identities;
- excluded zero-supply entries;
- bulk color generation controls;
- color themes, gradients, materials, textures, or patterns;
- alpha editing;
- per-orientation colors;
- per-placement colors;
- catalog editing;
- runtime prototile creation;
- searching, filtering, or family tabs;
- finite supplies above the editor control maximum;
- color accessibility analysis;
- palette undo/redo; or
- a general inspector.

if 34 ordinary rows cannot remain usable at the normal project size, adjust
only row spacing, preview size, and palette-panel width before adding another
interaction mode. do not replace the visible scroll list with hidden search or
family navigation without a new decision.

if `ColorPickerButton` cannot enforce no-alpha opaque output under pinned Godot
4.7, normalize alpha to one in the editor callback and record the behavior. do
not add alpha to gameplay presentation.
