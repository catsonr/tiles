# project hail mary act 1 — implementation notes

Notes from implementing domain-aware exact palettes: one `GeometryDomain`, a
catalog which admits three new hex-12 identities and answers domain membership,
one canonical orientation compiler, one checked precompiled `PaletteEntry`
factory, and one domain-aware palette resource compiler. Everything below is
verified against the built test executable, a default GDExtension build, both
headless integration scenes, and an ordinary headless load.

## verification

| step | result |
| --- | --- |
| `scons tests` | builds clean |
| `./build/tests/tiles_tests` | **9707 checks, 0 failed across 390 cases** |
| standalone c++17 / `-fno-exceptions` / `-Wall -Wextra -Wpedantic` | no warnings |
| `scons` | GDExtension builds, no warnings or errors |
| `res://tests/resource_integration.tscn` | **270 checks passed** |
| `res://tests/level_editor_integration.tscn` | **218 checks passed** |
| ordinary headless load | `[tiles] level editor ready: 34 catalog rows`, no error lines |
| `git diff --check` | clean |

Baselines were re-established before editing and are exactly preserved:

```text
native                7634 checks / 0 failed / 355 cases   →  9707 / 0 / 390
resource integration  220 checks passed                    →  270
level editor          218 checks passed                    →  218 (unchanged)
ordinary run          34 catalog rows                      →  34 catalog rows
```

So `355 + 35 = 390` cases (14 catalog, 11 palette, 10 in the new content test
file) and `7634 + 2073 = 9707` checks. No existing test was
removed, weakened, renamed to evade discovery, or changed to accept a different
result. Two categories of existing test *were* edited, both explicitly permitted
and both preserving their claim:

- lattice expectations that indexed `catalog->entries()` now index
  `catalog->entries_for(GeometryDomain::lattice)`, which is the same 34 entries
  in the same order; and
- every `compile_palette_resource` call passes the now-explicit
  `GeometryDomain::lattice`.

One existing test changed its *expected value*, unavoidably and by spec — see
"deliberate behaviour changes" below.

`SConstruct` needed no edit: both globs discovered the new files.

The worktree was dirty on arrival with concurrent formalizer work
(`PROJECT-HAIL-MARY-ROADMAP.md` status lines, plus untracked act-2 and act-2-1
plans). None of it overlaps this act's files and all of it is preserved
untouched. No commit was made.

## files

New:

```text
src/content/GeometryDomain.h
src/content/CanonicalOrientationCompiler.h
src/content/CanonicalOrientationCompiler.cpp
tests/test_canonical_orientations.cpp
```

Modified:

```text
src/content/PrototileCatalog.h        .cpp
src/engine/Palette.h                  .cpp
src/game/resources/ResourceCompiler.h .cpp
src/game/LevelEditor.h                .cpp
src/game/testing/ResourceIntegrationRunner.h .cpp
src/game/testing/LevelEditorIntegrationRunner.cpp
src/game/Editor.cpp
tests/test_prototile_catalog.cpp
tests/test_palette.cpp
```

`src/game/Editor.cpp` gained exactly one line: a `PrototileCatalogStage::hex12`
case in its diagnostic `describe`, without which the build warns `-Wswitch`. The
old standalone editor and `TetrominoState` are otherwise untouched, as the act
directs.

`tests/test_canonical_orientations.cpp` is the "one focused content test" the act
permits. It holds the canonical orientation compiler cases and the downstream
domain-blindness cases; keeping them out of the already 1000-line catalog test
file was the only reason.

Nothing in `src/core/` changed at all. No Godot dependency entered `src/core/`,
`src/content/`, or `src/engine/`.

## the exact new public surface

```cpp
// content/GeometryDomain.h
enum class GeometryDomain : std::uint8_t { lattice, hex12 };

// content/PrototileCatalog.h
bool CanonicalPrototile::supports(GeometryDomain) const;
std::optional<Hex12RegularPolygon> CanonicalPrototile::hex12_polygon() const;
std::vector<const CanonicalPrototile *>
    PrototileCatalog::entries_for(GeometryDomain) const;
// CanonicalDefinition gains: std::optional<Hex12RegularPolygon> hex12_polygon;
// PrototileCatalogStage gains: hex12
// PrototileCatalogErrorCode gains: missing_geometry_source,
//                                  reference_geometry_mismatch
// PrototileCatalogError gains: std::optional<Hex12CompilationError> hex12_error;

// content/CanonicalOrientationCompiler.h
Result<std::vector<OrientedPrototile>, CanonicalOrientationCompilationError>
    compile_canonical_orientations(GeometryDomain, const CanonicalPrototile &);

// engine/Palette.h
static Result<PaletteEntry, PaletteEntryCompilationError>
    PaletteEntry::make_compiled(Supply, std::vector<OrientedPrototile>);

// game/resources/ResourceCompiler.h
Result<engine::Palette, PaletteResourceError> compile_palette_resource(
    content::GeometryDomain,
    const godot::Ref<PaletteResource> &,
    const content::PrototileCatalog &);
```

`GeometryDomain` has a fixed underlying type for the same reason
`Hex12RegularPolygon` does: the act requires an invalid value produced by an
integer cast to be rejected, and without a fixed underlying type
`static_cast<GeometryDomain>(200)` is *unspecified* in C++17, so the requirement
would not be soundly testable. Every public switch over it handles the invalid
case by falling out of the switch to a `false` / empty / typed-rejection path —
never an assertion, fallback, or silent selection of the other domain. The tests
exercise `2`, `7`, `9`, and `200`.

Nothing else gained a domain. `OrientedPrototile`, `Placement`, `Arrangement`,
`PaletteEntry`, `Palette`, `Level`, `State`, and every engine command are exactly
as they were.

## master table order carries both presentation orders

The act allows the shipped table to be *arranged or annotated*. It is arranged.
Master storage order is:

```text
1 … 26, 35, 27, 28 … 34, 36, 37
```

Filtering by lattice membership yields ids `1..34` in their established order;
filtering by hex-12 membership yields `35, 27, 36, 37`. Both presentation orders
therefore fall out of one table with no annotation field, no second table of
playable ids, no duplicated unit-square definition, and no sorting by id at any
call site. The catalog stores the two filtered index lists (not pointers) so a
copied or moved catalog yields views into *itself*; the tests assert exactly that
by bounds-checking view pointers against the copy's own `entries().data()` range.

This is the one visible consequence: `catalog->entries()` is no longer id-ordered
and no longer 34 long. Any future consumer wanting "the playable list" wants
`entries_for(domain)`, not `entries()`.

## the shared unit square

Id 27 is one `CanonicalPrototile` with one stored `Prototile`, present in both
views, found once by `find`, and counted once in the master table. Catalog
construction builds *both* references — the lattice one through
`whole_game_units` → `Polygon::make` → `Prototile::make`, the hex-12 one through
`compile_hex12_orientations(id, square, { reference() })` — proves their ids
agree and their polygons satisfy `same_boundary`, and only then stores the
lattice one. They do agree: the hex-12 square's phase zero walks `d0, d3, d6, d9`
= `(±q, 0)`, `(0, ±q)`, which is exactly the axis-aligned `q × q` lattice square
after canonicalization. The mismatch path is reachable and tested by handing the
same definition a side-two ring.

The domain selects only which orientations that identity offers: one lattice
group versus three hex-12 groups. Both native and headless tests assert that
pairing directly.

Hex-only references (35, 36, 37) are taken from
`compile_hex12_orientations(id, polygon, { reference() })` and nothing else. No
triangle, hexagon, or dodecagon vertex is authored, copied from a test constant,
or reconstructed anywhere in `src/content/`.

## deliberate behaviour changes

Two, both required by the act, both worth stating plainly:

1. **A definition describing no geometry now fails differently.** It used to be
   `stage = definition` with `CellBoundaryErrorCode::empty` (the old
   "exactly one of ring/cells" rule reported both-or-neither identically). The
   act splits them: *both* ring and cells keeps the existing typed definition
   failure, *neither* source is now the new catalog-table failure
   `PrototileCatalogErrorCode::missing_geometry_source` at
   `stage = catalog`, carrying the entry id. The existing test
   `"catalog construction rejects a definition with no geometry description"`
   was updated to the new expectation — this is the one place an existing test's
   expected value changed, and the act mandates it.

2. **`compile_palette_resource` takes a domain and can no longer be called
   without one.** `PaletteResourceError::orientation_error` changed type from
   `LatticeOrientationError` to `CanonicalOrientationCompilationError`, which
   nests the lattice error unchanged. Anyone reading the old field now reads one
   more level down.

## one interpretive decision, recorded

`make_compiled`'s specified precedence reads:

```text
2. for each index i from one upward:
   1. reject the first id inconsistent with index zero…
   2. reject the first reference boundary inconsistent with index zero…
```

Two readings exist when defects coexist: check *both* per index as the loop
advances, or scan all ids first and only then all boundaries. Given

```text
[ok, wrong-boundary, wrong-id]
```

the first reports boundary at index 1; the second reports id at index 2.

**Implemented: per index.** The nesting is the same one this act uses for the
resource-entry precedence ("each authored entry in order: missing entry;
negative id; unknown id; …"), which is unambiguously interleaved per entry, so
the sub-list under "for each index i" reads the same way. The test
`"precompiled entry precedence is exact when several defects coexist"` asserts
that reading explicitly, including that a single index carrying *both* defects
answers as identity. If the formalizer meant the other reading, this is the one
line to change and the one test to flip.

Everything else about the factory is mechanical: five checks in the specified
order, `orientation` populated by every failure except `empty_orientations`, and
`conflicting_orientation` populated by exactly the three pair failures.

## the legacy lattice factory really is unreachable

`PaletteEntry::make` now compiles through the lattice core and delegates to
`make_compiled`. That delegation cannot fail, because one call to
`compile_lattice_orientations` establishes all six conditions at once: it returns
a nonempty vector of one prototile's groups, in strictly increasing
representative order, with each requested angle recorded in exactly one group and
each group carrying a distinct canonical boundary. The result is still inspected
rather than assumed and the invariant is `assert`ed; the unreachable branch
returns `LatticeOrientationErrorCode::empty_orientation_set` purely because the
act forbids widening the established lattice error type and the function must
return *something* under `NDEBUG`. It is commented as unreachable at the site.

The convenience path's observable behaviour is unchanged, and a test compares it
field by field against `make_compiled` over the same compiled vector.

## what the tests actually assert

**Catalog and domains** (`tests/test_prototile_catalog.cpp`, +14 cases): 37
unique identities; ids `1..34` keep their names, areas, geometry, and orientation
counts; ids `35..37` have the specified names and exact side counts `3 / 6 / 12`;
both views exact and in order; invalid-domain views empty; membership exclusive
except id 27; the hex-only references equal their compiler's reference
prototiles; the shared square agrees with an independently rebuilt unit ring
*and* with the module's phase zero; no two ids inside one view are congruent
under that view's own rotations (quarter turns for lattice, all twelve twelfth
turns for hex-12); and every construction failure — missing source, both lattice
sources, unsupported hex-12 polygon, reference mismatch, duplicate id, empty
name — with its exact stage, id, and payload.

The `unsupported_polygon` case matters: an invalid `Hex12RegularPolygon` admitted
through the test seam reaches the ordinary hex-12 compiler and comes back as a
complete `Hex12CompilationError`, not a catalog string or an assertion.

**Canonical orientation compiler** (`tests/test_canonical_orientations.cpp`): all
34 lattice entries compile to exactly what `compile_lattice_orientations`
returns, compared group by group including equivalence labels and boundaries; the
hex-12 view compiles to `4 / 3 / 2 / 1` with the complete act-0 label tables
(`{0,4,8}…`, `{0,3,6,9}…`, `{0,2,4,6,8,10}…`, `{0…11}`) authored in the test;
every product equals the ordinary hex-12 compiler's output unchanged; each
identity compiles only through a domain it supports; invalid domains answer
`unsupported_domain` for both a lattice and a hex identity, proving the domain is
checked before membership; and payload optionals are empty for both membership
failures.

**Checked palette entry** (`tests/test_palette.cpp`, +11 cases): every defect is
synthesized only out of values real compilers published — there is no unchecked
`OrientedPrototile` construction seam and none was added. The interesting
fixtures:

- *duplicate label*: the whole first triangle class (labels `0/1, 1/3, 2/3`)
  followed by the same class recompiled from `{4/12, 8/12}` (labels `1/3, 2/3`).
  Distinct increasing representatives, shared labels — and the label failure is
  answered before the shared boundary the same pair also has.
- *duplicate boundary*: triangle phase `0` and phase `4/12` compiled separately.
  Disjoint labels, increasing representatives, identical geometry.

**Downstream domain blindness**: a hex-12 palette built through
catalog → `compile_canonical_orientations` → `make_compiled` → `Palette::make`,
dropped into an ordinary `State` over a plain lattice square region. It places a
hexagon at exact origin, then mates a triangle onto **each of its six edges**
through `MateFullEdgesCommand`, requiring that each edge accepts exactly one
`(variant, edge)` pair — 6 joins, 0 ambiguities, 7 placements, every footprint
inside the region. A second case mates a unit square onto a dodecagon through
`MateVerticesCommand`. Supply is read back through the existing derived
`supply_status`: hexagon `used 1 / remaining 1`, triangle `used 6 / unlimited`,
square `used 1 / remaining 2`. `State` gained nothing to make this possible.

**Godot integration** (`ResourceIntegrationRunner`, +50 checks): the four-entry
hex-12 palette compiles in exact presentation order with counts `4 / 3 / 2 / 1`,
exact ids, supplies, reference boundaries, representatives, equivalence labels,
and oriented boundaries — all compared against an independent
`compile_hex12_orientations` call rather than read back out of the path being
checked; authored colour changes nothing; a lattice-only id in `hex12` and a
hex-only id in `lattice` both report `prototile_unavailable_in_domain` before
orientation compilation; an invalid domain reports
`unsupported_geometry_domain` with no entry index and no nested error, *after* a
missing resource answers first; and a failure at the second entry leaves the
resource entries, their supplies and colours, and the catalog exactly as they
were, with no partial palette.

## the visible editor is unchanged and explicitly lattice-only

`LevelEditor` gained one file-local constant, `EDITOR_DOMAIN =
GeometryDomain::lattice`, and one private helper, `lattice_entries()`. Every row
build, row count, ready diagnostic, row-to-id lookup, palette restore, palette
publication, and row bound check goes through the lattice view; every
`compile_palette_resource` call passes the domain explicitly. `PaletteRow`'s
`catalog_index` now means "index into the lattice view", which is the same
`0..33` it always meant.

No domain selector, hex-12 row, new document workflow, blueprint, or new visual
control was added. It still prints `34 catalog rows`, and
`LevelEditorIntegrationRunner` still passes 218 checks unchanged. **No visual or
manual acceptance is claimed: this act deliberately leaves the visible editor
lattice-only and unchanged.**

## for the act-2 formalizer

Five things, none of them decisions made here.

1. **`entries_for` returns a fresh vector of borrowed pointers per call.** It is
   cheap (37 entries, two small index vectors) and safe across copy and move,
   but it is a *view*, not a cached list. An editor that rebuilds rows on every
   frame should hold the vector for the duration of the rebuild, as `LevelEditor`
   now does, rather than calling it per row.

2. **Distinct orientation counts differ per prototile and per domain.** A hex-12
   dodecagon has exactly one variant, a hexagon two, a square three, a triangle
   four; the *same* unit square has one in the lattice and three in hex-12. Any
   "rotate" affordance must read `orientations().size()` from the compiled entry.
   This is act 0's warning, now made concrete at the palette level.

3. **Changing domain must discard the palette, not translate it.** Ids 1..26 and
   28..34 simply do not exist in hex-12, and 35..37 do not exist in the lattice.
   The only id surviving a domain change is 27, and even it changes its
   orientation set. The roadmap already says discard; nothing here makes
   translation cheaper.

4. **The level resource is still lattice-only, on purpose.** `LevelResource`
   serializes no domain and `compile_level_resource` hardcodes
   `GeometryDomain::lattice` with a comment pointing at act 2-1. The existing
   `canonical_level.tres` fixture and every persistence test still pass
   unchanged. When act 2-1 adds the serialized domain, that one call site and the
   `LevelResourceError` nesting are what change.

5. **`PaletteEntryCompilationError` is reachable only from a defective source
   compiler or a hand-assembled sequence.** Through
   `compile_canonical_orientations` it cannot fire, because that function calls
   exactly one compiler once. If act 2 ever assembles a palette entry from
   several compilations — it should not — this factory is what catches it, and
   `palette_entry_construction_failed` is the resource-level code it surfaces
   through.

## not done (correctly out of scope)

No new editor and no domain chooser; no serialized geometry domain; no blueprint
placements, arrangement-to-region construction, or level export under a new
resource graph; no player or play session; no mixed-domain palettes; no
square/octagonal geometry, penrose, spectres, or `Tile(1,1)`; no reflections,
scaling, curves, decoration, or matching rules; no arbitrary source compilers, no
compiler registration, and no type erasure; no runtime domain check after palette
compilation; no target generation; no polygon union; no migration of old
resources; and no campaign or production-ui work.
