# final exam act 1: export integration and authored content

## immutable implementation contract

this document is immutable once handed to an implementor. do not revise it to
match implementation choices. stop and ask carson if a genuine contradiction
or behavior-changing ambiguity remains.

record results in:

```text
docs/final-exam/FINAL-EXAM-ACT-1-IMPLEMENTATION-NOTES.md
```

do not mention this plan or project act in code comments or runtime
documentation. do not commit.

## goal

settle the two concrete pieces of infrastructure already prepared for the
remaining game:

```text
tested export-test history
    ↓
current main
    ↓
pushes to main build the existing desktop artifacts

every authored level
    ↓
remove palette entries unused by its witness
    ↓
finite supply equals witness usage
    ↓
the level checker preserves that convention
```

this act does not redesign exporting, add platforms, build an exam runtime, or
add audio. the three desktop paths have already been tested. integrate them;
do not replace them.

## prerequisite and branch topology

before editing:

1. inspect and preserve concurrent work;
2. read `DEFINITIONS.md`, `COORDINATES.md`, `level-notes.txt`,
   `docs/TEST-LEVELS.md`, and the complete final-exam roadmap;
3. read the complete export workflow and export presets from both branches;
4. read the level decoder, level checker, every file under
   `godot-project/levels/`, and the player integration expectations for
   `act0-level.tres`; and
5. establish the current native, GDExtension, resource-integration,
   editor-integration, player-integration, level-checking, ordinary-startup,
   and `git diff --check` baselines.

the known branch relationship at formalization is:

```text
merge base:       e448e56  final-exam act-0 formalize
main contains:    2b324de  final-exam roadmap
export-test tip:  3cc84b5  unify desktop export workflow
```

`export-test` carries eleven commits after the merge base. they establish the
windows x86_64, linux x86_64, and macos universal paths and finish with one
unified workflow. the current `main` carries the implemented player, authored
levels, level checker, and roadmap which must survive integration.

require a clean tracked worktree and index before merging. do not stash,
discard, or overwrite concurrent work to manufacture that condition.

if `export-test` no longer resolves to `3cc84b5`, if either required history is
missing, or if the actual merge reports a conflict, stop and report the exact
topology or conflict. do not guess at conflict resolution.

## merge first

the export history must be integrated before any content normalization or
post-merge editing:

```text
git merge --no-commit --no-ff export-test
```

`--no-commit` is required because the implementor must not commit. leave the
result for carson to commit after implementation and verification.

inspect the complete merge result before continuing. it must:

- add the unified desktop export workflow;
- add the linux preset and retain the macos and windows presets;
- retain the unsigned-credential macos configuration;
- retain the portable integral formatting change in `LevelEditor`;
- preserve every later `main` level, font, player, checker, scene, test, and
  startup change; and
- introduce no deletion or replacement of later `main` work.

do not recreate the export branch as a hand-copied patch, squash its history,
rebase either branch, or merge only selected export commits.

## existing desktop export workflow

preserve the workflow from `3cc84b5` except for its source branch:

```yaml
on:
  workflow_dispatch:
  push:
    branches:
      - main
```

the push branch must change from `export-test` to `main`. both manual dispatch
and push dispatch remain.

preserve the existing:

- Godot 4.7.1 setup and export templates;
- recursive submodule checkout;
- windows x86_64 mingw build and `Windows Desktop` export;
- linux x86_64 build and `Linux` export;
- macos universal debug-validation build, release build, and `macOS` export;
- release GDExtension targets and paths;
- individual platform artifact names and directory shapes;
- combined `tiles-desktop` artifact;
- two-day artifact retention;
- read-only workflow permissions; and
- per-ref concurrency cancellation.

do not add another workflow, restructure the job matrix, add tests to the
workflow, add an exported-executable smoke launch, change action versions,
change runners, change Godot versions, change artifact retention, add signing
or notarization, or add another platform. these paths are already tested; this
act only makes the integrated workflow follow `main`.

the implementor cannot complete remote acceptance without committing and
pushing. record that limitation honestly. after carson commits and pushes, the
acceptance run is the run for that exact `main` commit, not an older successful
`export-test` run.

## authored-level normalization

normalize every direct `.tres` level artifact under:

```text
godot-project/levels/
```

this includes the temporary `act0-level.tres` while it remains shipped startup
content. do not normalize test fixtures elsewhere in the project.

for each level:

1. load and compile its current resource through the established public
   consumer path or the dependency-free checker's equivalent real compilers;
2. compile its authored blueprint into the exact witness arrangement;
3. count witness placements by exact `PrototileId`;
4. remove every palette entry whose count is zero; and
5. set every retained entry to a positive finite supply exactly equal to its
   witness count.

the law after normalization is:

```text
palette ids = distinct prototile ids used by the compiled witness

for every palette id p:
    configured finite supply(p)
        = number of witness placements whose prototile id is p
```

there are no deliberate supply exceptions in this act. no level under
`godot-project/levels/` retains unlimited supply.

when removing an unused palette entry, remove both its reference from the
palette's ordered entry array and its now-unused `PaletteEntryResource`
subresource block. preserve the relative order of every retained entry. do not
renumber or reinterpret prototile ids.

normalization must not otherwise change:

- geometry domains;
- blueprint membership, placement order, orientations, or exact translations;
- retained palette-entry order;
- retained colors, except for the exact glider change below;
- level filenames;
- the compiled target region;
- solvability, seed safety, proposal density, overlap, or deferral results; or
- the runtime rule that the witness is authoring evidence rather than player
  state.

the known unused entries before normalization are:

```text
goul            id 23
guy_f           ids 5 and 28
slurm           ids 1, 4, 5, and 7
snake_tri_hex   ids 27 and 37
```

this list is an audit seed, not an alternate source of truth. derive the actual
result from each successfully compiled witness and stop if the observed
pre-normalization data disagrees.

## glider triangle color

in `godot-project/levels/glider.tres`, change the retained id-35 triangle entry
to exactly:

```text
Color(1, 0.82, 0.16, 1)
```

this bright yellow-orange is not used by another palette entry in that level.
do not recolor any other entry or level.

## level-checking enforcement

extend the existing authored-level checking path so normalization is a
mechanically enforced property, not a one-time edit.

the check must run only after the palette and blueprint have compiled
successfully. using the compiled witness arrangement, it must fail a level
when:

- a compiled palette entry has zero witness placements;
- a retained supply is unlimited; or
- a retained finite supply differs from the number of witness placements with
  that exact `PrototileId`.

diagnostics must name the level and offending prototile id and report the
configured supply and witness count. an unlimited configured supply must be
reported as `unlimited`, not encoded as a misleading finite value.

keep the existing compilation, region, solvability, seed-safety, options,
overlap, and deferral behavior. normalization checking must not reimplement
resource loading, palette compilation, blueprint compilation, or geometric
legality in python.

the ordinary no-argument command remains:

```text
python3 tools/level_check/check_levels.py
```

and still discovers every direct `.tres` file in `godot-project/levels/`.
named paths still check exactly those paths. any normalization failure produces
a nonzero overall exit status alongside the existing mechanical failures.

update `docs/TEST-LEVELS.md` to explain the two new authored-content laws:

```text
no unused palette entries
finite supply equals compiled witness usage
```

document how their failures are reported and how an author resolves them.

prove the negative paths during implementation with temporary copies outside
the repository:

1. change one retained supply above its witness count and observe the exact
   mismatch failure; and
2. add or retain one zero-use palette entry and observe the unused-entry
   failure.

do not leave deliberately broken checked-in fixtures or temporary resources.

## player integration consequence

`act0-level.tres` currently has two unlimited entries and is loaded by the
player integration runner. normalization makes both supplies finite at their
exact witness counts.

update only the integration expectations which directly describe that
artifact. assert that both palette rows expose the correct finite remaining
supply at startup and that placement and undo continue to derive the correct
remaining values.

do not change `LevelPlayer` behavior to accommodate the data change. finite
supply presentation and restoration already belong to the player.

## level notes

update the root `level-notes.txt` after the resources and checker pass:

- remove the stale “cannot place the initial tile” and `zooble` reports, which
  were resolved by the joining-rule work and are now covered by solvability and
  seed-safety checks;
- record that every current shipped level has witness-exact finite supplies;
- record that unused palette entries were removed;
- record the exact glider triangle color change; and
- retain a clear statement that `act0-level` is temporary startup content, not
  a canonical exam problem.

the repository does not presently contain a canonical exam order. do not
invent one, sort levels by filename, or create the runtime exam manifest in
this act. state plainly in `level-notes.txt` that canonical order remains to be
chosen before the `Exam` is formalized.

## verification

with the Godot editor closed before every native rebuild, run the established
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
./Godot.app/Contents/MacOS/Godot --headless --path godot-project --quit-after 2
```

also:

- record exact before/after totals and every level-check row;
- run the two temporary negative normalization checks;
- compile modified c++ translation units with the project's established c++17,
  no-exceptions, and warning settings where applicable;
- verify the workflow trigger names only `main`, plus manual dispatch;
- inspect the complete merge and implementation diff;
- run `git diff --check`;
- verify no temporary files, build products, or exported bundles were added;
- verify the repository remains in the intended uncommitted merge state; and
- verify the implementor made no commit.

manual acceptance is limited to opening the current player and confirming that
the finite palette supplies render and decrement normally. no cross-platform
manual export pass is required from the implementor.

after carson commits and pushes the completed merge to `main`, confirm that the
existing unified workflow produces successful windows x86_64, linux x86_64,
macos universal, and combined desktop artifacts from that exact commit.

## boundaries

act 1 does not:

- create the `Exam`, a problem manifest, reusable problem pages, navigation,
  save data, or campaign progression;
- choose canonical problem order or problem numbers;
- add audio, miniaudio, music, or sound effects;
- change exact geometry, engine legality, proposal rules, or completion;
- change a blueprint to make normalization convenient;
- grant extra supply beyond the compiled witness;
- preserve unused palette entries as hints, decoration, or alternate content;
- add supply exceptions;
- redesign the editor or player;
- redesign or expand the tested export workflow;
- add signing, notarization, releases, installers, or new platforms;
- commit, push, delete branches, rebase, squash, or rewrite history; or
- modify the immutable contracts of earlier acts.

## acceptance summary

act 1 is locally complete exactly when:

```text
export-test merged without a commit
    ∧ every later main change preserved
    ∧ workflow push branch = main

every godot-project/levels/*.tres
    → compiled witness
    → palette contains exactly its used prototile ids
    → every supply is finite and equals witness usage

glider triangle
    → Color(1, 0.82, 0.16, 1)

level checker
    → rejects unused entries
    → rejects unlimited or mismatched supplies
    → preserves every existing mechanical check

all local regressions pass
    ∧ merge remains uncommitted
```

remote export acceptance follows when carson commits and pushes that result and
the existing three-platform workflow succeeds for the exact new `main`.
