# project hail mary act 2-1 beta — implementation notes

Notes from replacing the region-first resource graph with one versioned,
proof-carrying level artifact: a new blueprint record resource, a rewritten
one-way compiler, a fresh encoder, compile-before-save export, consumer
loading, and a rewritten headless runner.

## verification

| step | result |
| --- | --- |
| `scons tests` | builds clean |
| `./build/tests/tiles_tests` | **9921 checks, 0 failed across 427 cases** (unchanged) |
| standalone c++17 / `-fno-exceptions` / `-Wall -Wextra` | every modified unit clean; `LevelEditor.cpp`'s one `-Wunused-function` on `number` predates this act and is absent from the scons build |
| `scons` | GDExtension builds, no warnings or errors |
| `res://tests/resource_integration.tscn` | **414 checks passed** (was 270; subject replaced) |
| `res://tests/level_editor_integration.tscn` | **151 checks passed** (unchanged) |
| ordinary headless load | `[tiles] level editor ready: 37 canonical identities, choose lattice or hex12`, zero error lines |
| `git diff --check` | clean |

Baselines re-established before editing, and what they are now:

```text
native                9921 / 0 / 427  →  9921 / 0 / 427 (unchanged)
resource integration  270 checks      →  414 (beta replaces its subject)
level editor          151 checks      →  151 (unchanged)
ordinary run          37 identities   →  37 identities (unchanged)
GDExtension build     clean           →  clean
```

The resource-integration total is deliberately not comparable across the act:
its old checks tested polygon quantization, authored region vertices, region
resource compilation, `.res` persistence, and save-as path takeover — surfaces
this act deletes. Nothing under `src/core/`, `src/content/`, `src/engine/`, or
`tests/` was touched at all, which is why the native total is bit-for-bit
unchanged.

The one `never_written.tres` engine error line printed by the resource run is
the pinned loader's own diagnostic for the deliberate "load a path that was
never written" check, exactly as before this act.

The worktree was clean on arrival. No commit was made.

## files

Modified:

```text
src/game/resources/LevelResources.h            .cpp
src/game/resources/ResourceCompiler.h          .cpp
src/game/resources/LevelPersistence.h          .cpp
src/game/testing/ResourceIntegrationRunner.h   .cpp   (rewritten)
src/register_types.cpp
godot-project/tests/fixtures/canonical_level.tres     (replaced)
src/game/LevelEditor.cpp                              (two lines, see below)
```

No file was added or deleted. `src/game/PrototilePreview.*`,
`src/game/LevelEditor.h`, `godot-project/main.tscn`, and
`LevelEditorIntegrationRunner.*` are untouched.

## the one boundary crossing, authorised

The act forbids editing `LevelEditor`. The domain-specific palette size limit
needs a typed alternative, which means one new `PaletteResourceErrorCode`
enumerator — and `LevelEditor.cpp:408`'s `describe(PaletteResourceError)`
switches exhaustively over that enum, so the addition raised `-Wswitch` in a
file beta may not touch. There is no honest way around it: `LevelResourceError`'s
seven codes are fixed by the act, and `palette_invalid` must nest a complete
`PaletteResourceError`.

I stopped and asked. Carson authorised the edit. It is two lines:

```cpp
case PaletteResourceErrorCode::too_many_entries:
    return "the palette lists more tiles than this geometry domain has";
```

Nothing else in `LevelEditor` changed, it consumes no new beta surface, and the
string is unreachable from the editor anyway: its palette rows come from
`entries_for(domain)`, so an editor-built palette can never exceed that count.

## the resource graph

```text
LevelResource
    ├── format_version   int, default 1, accepted exactly 1
    ├── geometry_domain  int, 0 lattice | 1 hex12, default 0
    ├── palette          PaletteResource
    │     └── entries    Array[PaletteEntryResource]   (id, supply, color)
    └── blueprint        Array[BlueprintPlacementResource]
                             ├── prototile_id       default 0
                             ├── orientation_step   default 0
                             ├── orientation_order  default 1
                             ├── translation_x_raw  default 0
                             └── translation_y_raw  default 0
```

`PolygonResource` and `RegionResource` are gone — types, properties,
registration, compilers, error types, fixture graph, and checks. The runner
asserts their absence through `ClassDB::class_exists` and asserts `LevelResource`
publishes no `region` property. There is no migration path and old `.tres`
compatibility is explicitly absent.

Two constants live beside the schema in `LevelResources.h`:

```cpp
constexpr std::int64_t LEVEL_RESOURCE_FORMAT_VERSION = 1;
constexpr std::int64_t ENCODED_GEOMETRY_DOMAIN_LATTICE = 0;
constexpr std::int64_t ENCODED_GEOMETRY_DOMAIN_HEX12   = 1;
```

The domain encoding constants are an interpretive addition, not a new concept:
the act fixes `0 | 1` in prose, and spelling it once keeps the compiler and the
encoder from drifting. `MAX_BLUEPRINT_PLACEMENTS` lives in `ResourceCompiler.h`,
beside the two operations which enforce it.

Every setter is transport-only: it preserves the exact signed value it is given
(extrema included), validates and compiles nothing, returns early when the value
is unchanged, and emits `changed` exactly once for an actual change.

## the exact new public surface

```cpp
// ResourceCompiler.h
constexpr std::size_t MAX_BLUEPRINT_PLACEMENTS = 64;

enum class PaletteResourceErrorCode { ..., too_many_entries, ... };   // one new code
enum class BlueprintResourceErrorCode {
    too_many_placements, missing_placement, negative_prototile_id,
    orientation_step_out_of_range, orientation_order_out_of_range,
    invalid_orientation,
};
enum class LevelResourceErrorCode {
    missing_resource, unsupported_format_version, unsupported_geometry_domain,
    palette_invalid, blueprint_resource_invalid,
    blueprint_arrangement_invalid, arrangement_region_invalid,
};
enum class LevelResourceEncodingErrorCode {
    unsupported_geometry_domain, color_count_mismatch,
    prototile_id_not_representable, supply_not_representable,
    too_many_placements, blueprint_prototile_id_not_representable,
};

struct CompiledLevelResource {
    content::GeometryDomain domain;
    std::vector<engine::BlueprintPlacement> blueprint;
    Arrangement arrangement;
    engine::Level level;
};

Result<engine::Palette, PaletteResourceError> compile_palette_resource(...);
Result<std::vector<engine::BlueprintPlacement>, BlueprintResourceError>
    compile_blueprint_resource(const TypedArray<BlueprintPlacementResource> &);
Result<CompiledLevelResource, LevelResourceError> compile_level_resource(...);
Result<Ref<LevelResource>, LevelResourceEncodingError> make_level_resource(
    GeometryDomain, const Palette &, const std::vector<Color> &,
    const std::vector<BlueprintPlacement> &);

// LevelPersistence.h
Result<String, ExportLevelError> export_level_resource(resource, path, catalog);
struct LoadedLevelResource { Ref<LevelResource> resource; CompiledLevelResource compiled; };
Result<LoadedLevelResource, LoadLevelError> load_level_resource(path, catalog);
```

Error payload discipline follows the tree's existing convention — optional whole
payloads, exactly the alternative the code selects, nothing flattened to text.
Three payload decisions the act left to the implementor:

1. **`too_many_entries` carries `entry_count` and `maximum_entry_count`** and no
   entry index, so "answered before any entry was inspected" is legible in the
   value itself. The runner proves it with an array of nothing but null entries.
2. **`invalid_orientation` carries only the `OrientationError`.** The act says a
   record failure carries "only the signed encoded values *or* exact
   `OrientationError` relevant to that code"; for a `make` failure the factory's
   own refusal is the relevant evidence, and the two encoded components are
   already in the resource the caller holds.
3. **Encoding count failures reuse `actual_count` / `expected_count`.** For the
   color mismatch, expected is the palette's entry count; for the blueprint,
   expected is `MAX_BLUEPRINT_PLACEMENTS`. Both are documented at the field.

## precedence, as implemented

```text
compile_level_resource      null → version → domain → palette → records
                            → arrangement → region

compile_blueprint_resource  64-limit → per record:
                            null → negative id → step range → order range
                            → Orientation::make → raw translation

compile_palette_resource    null → domain → domain size limit → per entry

make_level_resource         domain → color count → every id → every supply
                            → 64-limit → every record id → construct

export_level_resource       null → empty path → non-.tres → compile → saver
load_level_resource         empty path → non-.tres → loader → type → compile
```

Every one of these orders has at least one runner check which coexists two
defects and asserts which answers.

Two consequences worth stating:

- **An empty blueprint is a region failure, not a blueprint failure.**
  `compile_blueprint` still succeeds on an empty record vector by act-2 spec, so
  emptiness surfaces as `arrangement_region_invalid` carrying
  `ArrangementRegionErrorCode::empty_arrangement`, exactly as alpha's notes
  predicted.
- **An equivalent-but-nonrepresentative orientation decodes and then fails.**
  `Orientation::make` canonicalizes, so `2/8` becomes `1/4` at the transport
  boundary; the palette then refuses it as `orientation_not_in_palette`. Both
  halves are checked.

## export is an export, not persistence

`export_level_resource` saves with `FLAG_BUNDLE_RESOURCES` and deliberately
without `FLAG_CHANGE_PATH`, and never calls `take_over_path`. The runner proves
that a successful export leaves the root resource's path empty, every
subresource's path empty, every pointer relationship intact, and the `changed`
count at zero — and that the written file contains no `[ext_resource` line, so
it is self-contained by inspection as well as by the fact that it reloads under
a deep cache bypass.

`.res` is now refused as an unsupported extension, along with everything else
that is not `.tres` case-insensitively. Save, save-as, pathless save, and path
takeover are gone.

Honest limit, as the act requires: nothing here promises that an operating-system
or engine I/O failure after `ResourceSaver` has begun leaves no partial file. The
saver's exact `godot::Error` is reported and that is all. Extension and
compilation refusals write nothing, and the runner asserts the file's absence in
both cases.

## the fixture

```text
version 1, lattice
palette   id 1 supply 3 color (0.75, 0.5, 0.25)
          id 2 supply -1 color (0.25, 0.75, 0.5)
blueprint id 1 @ 0/1 at raw (0, 0)
          id 2 @ 1/4 at raw (562949953421312, 0)   = (2, 0) game units
```

The o tetromino covers `[0,2]²`; the i tetromino's quarter-turn representative
covers `[2,3] × [0,4]`. Coverage is one connected six-vertex region, no holes,
exact area 8. It carries two entries with different supplies and different
colors, two records, a nonreference rational orientation, and one nonzero raw
translation, and it depends on no external resource.

The fixture's exact geometry was established by running the real compilers
rather than by hand-deriving it, then written into the `.tres` as raw integers.
The runner composes the same graph programmatically and asserts the two compile
to exactly equal palettes, witnesses, arrangements, and regions, so the text
fixture introduces no second representation.

Hex-12, holes, raw extrema, malformed transport, and both size limits stay
programmatic, as the act allows.

## what the headless runner proves

414 checks, in this order: registration and the absence of the old types;
defaults; property metadata; setter notifications for all four resources;
lattice palette compilation; domain palette compilation (hex-12 groups
`4/3/2/1`, membership refusals, invalid domain, shared unit square); the
domain-specific palette size limit; blueprint decoding; level compilation;
region derivation; compilation purity; encoding; the authored fixture; export
and consumer loading; temporary-file removal.

Specific act-mandated claims and where they live:

- **64 succeeds, 65 fails before inspection** — the 65th record is null and the
  error is still `too_many_placements` with both counts and no record index.
- **Palette limit is domain-specific** — five null entries are refused in hex-12
  (`maximum 4`) and reach entry inspection in the lattice (`missing_entry` at 0);
  thirty-five are refused in the lattice (`maximum 34`).
- **Zero order** — reaches `Orientation::make` and preserves
  `OrientationError::zero_order`; it is never treated as an out-of-range signed
  transport value.
- **Raw extrema** — `INT64_MIN` / `INT64_MAX`, ±1 raw unit, a negative whole
  unit, and a half unit all decode bit-exactly.
- **Coverage shapes** — no holes (fixture), one hole (3×3 ring of unit squares),
  two ordered holes (5×3 less two interior cells), partial-edge contact (unit
  square against half of the o tetromino's right edge), and a hex-12 dodecagon
  deriving its twelve-vertex region. Each asserts exact hole count, exact
  canonical hole vertices, and exact doubled area.
- **The rich product** — domain, complete witness in record order with exact
  orientations and translations, arrangement entry count, palette order, region
  vertices and area, all from one compiled value.
- **Color is not geometry** — two levels differing only in color compile to
  exactly equal palettes, arrangements, and regions.
- **Purity** — with `changed` connected to the level, its palette, one entry, and
  one record: zero notifications for a successful compile, zero for a failing
  one, and every property, pointer, and path unchanged.
- **Encoding** — every error and every precedence pair, fresh graphs in exact
  palette and record order, `-1` for unlimited, canonical `step()/order()`, raw
  translations written directly, no pathed child, no reused subresource, and an
  uncompilable (empty) blueprint still encoding — the encoder proves nothing
  about its own output.
- **Round trip** — one artifact exported and reloaded through the public
  operations, with ids, order, supplies, colors, rational turns, raw
  translations, arrangement, region, holes, and exact area all compared against
  the pre-export compilation. Holes get their own exported-and-reloaded case.
- **Replay** — eight distinct compiled products are replayed into fresh
  `engine::State`s and each reaches `solved()`.

Two things the runner cannot reach and does not fake:

- **`prototile_id_not_representable` and `supply_not_representable`** need a
  palette no catalog identity can produce. They are built through the ordinary
  checked factories — `Polygon::make` → `Prototile::make` →
  `compile_lattice_orientations` → `PaletteEntry::make_compiled` →
  `Palette::make` — with a `2^63` id and a `2^63` finite supply. No unchecked
  construction seam was added.
- **`ExportLevelErrorCode::saver_failed`** is not forced. Provoking a real
  `ResourceSaver` failure means an unwritable path or a full filesystem, and
  fabricating one would test the fixture rather than the code. The path is one
  `if` over the saver's returned error and is stated here rather than claimed as
  tested.

## interpretive decisions, recorded

1. **`State::solved()`, not `is_solved()`.** The act names `State::is_solved()`;
   the engine's actual accessor has always been `solved()`. Same predicate, same
   exact doubled-area comparison, untouched by this act.
2. **`load_level_resource` uses `Object::cast_to<LevelResource>`** for the "not
   exact `LevelResource` type" rejection. No subclass of `LevelResource` exists
   or is registered, so the cast is exact in practice, and it keeps the pinned
   loader's behaviour the act inherited.
3. **The domain encoding constants are published** rather than spelled `0` and
   `1` at three call sites. It is a spelling, not a new concept, and it carries
   no enum, registry, or dispatch.
4. **`compile_palette_resource` keeps its size limit internally.** The act places
   the limit "after a domain is known"; the palette compiler is the first place
   that is true and is the operation which would otherwise do the unbounded work.
   The check runs after the domain and before the entry loop, so `missing_entry`
   still answers for any array within the limit.

## boundaries respected

Nothing under `src/core/`, `src/content/`, `src/engine/`, or `tests/` changed.
No presentation geometry is derived or rendered; the editor still cannot load a
level; no independently authored region vertices survive anywhere; no player
state, progress, history, or completion enters a resource; `engine::State` still
constructs from an empty arrangement and receives no witness placement; nothing
privileges the witness over another legal solution; no old resource is migrated;
`.res`, JSON, and bespoke formats are refused; no geometry domain was added; and
alpha, blueprint, palette, level, state, command, and completion semantics are
untouched. Gamma's export UI and delta's binary are absent.

## for the gamma implementor

Five things.

1. **The complete resource boundary is the four operations above.**
   `make_level_resource` takes exact values only — an exact `Palette`, one
   `godot::Color` per entry in palette order, and the exact
   `std::vector<BlueprintPlacement>`. `LevelEditor::palette()`,
   `entry_color(i)`, and `blueprint()` are already exactly those three, so gamma
   needs no conversion layer and no draft resource type.
2. **Export compiles for you.** `export_level_resource` runs the complete
   compiler before touching the saver and returns the exact `LevelResourceError`
   on refusal, so gamma's export path is: encode → export → describe the typed
   failure. Calling `compile_level_resource` first is legitimate but redundant.
3. **Alpha's three expected region failures are what gamma must present.**
   `empty_arrangement`, `nonmanifold_boundary_vertex`, and
   `disconnected_coverage` reach gamma nested inside
   `LevelResourceErrorCode::arrangement_region_invalid`. They are ordinary
   authoring states. `ArrangementRegionErrorCode::internal_invariant_failure` is
   not, and deserves louder treatment.
4. **The 64-record limit is enforced twice**, at encoding and at decoding, with
   different error types. A gamma editor which lets an author place a 65th
   placement will fail at `make_level_resource`, before any file dialog.
5. **`too_many_entries` now exists in `PaletteResourceError`.** Its describe
   string is already in `LevelEditor.cpp`; the case is unreachable from the
   editor's own rows and is there so the switch stays exhaustive.

## manual acceptance: not performed

**No visual or manual acceptance is claimed.** Everything above is headless or
native evidence. Nothing in this act changes what is on screen — the editor's
surface, controls, and rendering are byte-identical apart from one unreachable
error string — so there is nothing new to look at until gamma adds the export
button.
