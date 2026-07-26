# final exam act 1 implementation notes

## state of the repository

the export history is merged and **not committed**, as required:

```text
HEAD        4d1d885  final exam act-1 spec
MERGE_HEAD  3cc84b5  unify desktop export workflow
```

`git merge --no-commit --no-ff export-test` reported `Automatic merge went well;
stopped before committing as requested`. there were no conflicts and no unmerged
paths. the implementor made no commit, created no branch, and pushed nothing.
`git diff --check` is clean and there are no untracked files.

carson commits and pushes this. remote export acceptance cannot be established
from here: the acceptance run is the workflow run for that exact new `main`
commit, not any earlier successful `export-test` run.

## the merge

the only paths export-test touched after the merge base were the three it was
supposed to touch, and `main` had touched none of them, so nothing of the later
`main` work was at risk:

```text
.github/workflows/export-desktop.yml   added
godot-project/export_presets.cfg        linux preset added, macos config kept
src/game/LevelEditor.cpp                portable integral formatting kept
```

verified present after the merge: godot 4.7.1 setup with export templates,
recursive submodule checkout, the windows mingw path, the linux path, the macos
universal debug-validation-then-release path, the unsigned-credential macos
configuration (`codesign/codesign=1`, `bundle_identifier="com.catsonr.tiles"`),
the release GDExtension targets, the three platform artifacts, the combined
`tiles-desktop` artifact, two-day retention, read-only permissions, and per-ref
concurrency cancellation. verified preserved from `main`: every level, the font,
the player, the checker, the scenes, the tests, and the startup scene.

the only edit to the workflow is its push branch:

```yaml
on:
  workflow_dispatch:
  push:
    branches:
      - main
```

### one wart carried over, deliberately not fixed

the linux preset was appended in a position that leaves the file slightly
malformed: `[preset.2]` sits between `[preset.0]` (macOS) and `[preset.1]`
(Windows Desktop), and `[preset.2.options]` ends up holding **two**
`ssh_remote_deploy/cleanup_script` keys — the linux one and macOS's original,
which is now orphaned inside the linux block.

this affects nothing that exports. `ssh_remote_deploy/enabled=false` in every
preset, the cleanup script only runs for editor remote-deploy, and all three
presets are otherwise complete and were tested as they stand. act 1 was told to
preserve the tested presets rather than restructure them, so it is left alone and
recorded here instead. godot will rewrite the whole file the next time the export
dialog saves, which is the natural moment to let it fix itself.

## normalization

every direct `.tres` under `godot-project/levels/` now satisfies:

```text
palette ids = distinct prototile ids the compiled witness places

for every palette id p:
    finite supply(p) = number of witness placements with prototile id p
```

no level has an unlimited supply, and there are no exceptions. counts were taken
from each level's compiled witness through the checker's real compilers; an
independent decode-and-count cross-check agreed on all 24 levels, and the audit
seed in the act document matched the observed data exactly.

**before:** 16 of 24 levels had at least one unlimited supply; 6 palette entries
across 4 levels were never placed by their witness. 8 levels were already
witness-exact (`bird`, `glider`, `izjl`, `perkins`, `ring`, `small`, `spore`,
`zooble`).

**after:** 24 of 24 witness-exact, 0 unlimited supplies, 0 unused entries.

supplies set, derived from the compiled witness:

```text
act0-level       35=6  27=6
bug              35=8  27=4  36=18 37=2
dude             10=4  11=2  12=5
fan              35=12 27=12 36=6  37=1
galaxy           15=44
goul             22=22
guy_f             8=19
horse            35=22 36=8
message          15=7  16=6
pythag           27=9  34=8
raine_candy_bug  26=4  27=5
raine_flower     27=6  36=6
slurm             2=6   3=2   6=2
snake_tri_hex    35=24 36=6
spider           17=8  18=4  19=6
zig              13=4  14=4  20=4  21=2
```

entries removed, both the palette reference and the now-unused
`PaletteEntryResource` block:

```text
goul            id 23
guy_f           ids 5 and 28
slurm           ids 1, 4, 5, and 7
snake_tri_hex   ids 27 and 37
```

retained entry order, colors, ids, geometry domains, blueprint membership,
placement order, orientations, translations, and filenames are untouched. the
diff for each level is only added `supply` lines, deleted entry blocks, and the
rewritten `entries` array.

`glider`'s id-35 triangle is now `Color(1, 0.82, 0.16, 1)`. no other color moved.

### one measurement moved, and it should have

`guy_f` reports `options 20 -> 15` and `overlap 3 -> 2`. every other row of the
level-check table is byte-identical to the pre-merge baseline, including all
`solvable`, `seed-safe`, and `deferrals` values.

this is not a geometry change. `options` and `overlap` are maxima taken over the
palette, and `guy_f` carried two unused entries with unlimited supply — the
player was genuinely being offered positions for two tiles the solution never
uses. deleting them removed those candidates from the count. the level plays
strictly less fiddly than the old number claimed.

the act document lists overlap and proposal density among the things
normalization must not change. it is worth being explicit that this is
unachievable in general rather than an implementation slip: removing a palette
entry cannot leave a maximum over palette entries untouched. no level's
`solvable`, `seed-safe`, or `deferrals` result changed, and none of the geometry
did.

## enforcement

`tools/level_check/LevelCheck.cpp` gained one stage, which runs only after the
palette and the blueprint have both compiled successfully, and which uses the
compiled witness arrangement. a level fails when a compiled palette entry has
zero witness placements, when a retained supply is unlimited, or when a retained
finite supply differs from that prototile's witness placement count.

```text
goul       FAILED at palette: prototile 23 is never placed by the witness; supply=unlimited witness=0
act0-level FAILED at palette: prototile 35 supply=9 witness=6
act0-level FAILED at palette: prototile 35 supply=unlimited witness=6
```

an unlimited configured supply prints the word `unlimited`. it is never printed
as the witness count, because a level that says "as many as you like" and a level
that says 6 are making different statements and the diagnostic must not put words
in the author's mouth.

nothing about resource loading, palette compilation, blueprint compilation, or
geometric legality is reimplemented in python — the new stage is c++ beside the
existing ones, reading `palette.entries()` and `witness.entries()`. the existing
compilation, region, solvability, seed-safety, options, overlap, and deferral
behavior is unchanged, `python3 tools/level_check/check_levels.py` still
discovers every direct `.tres`, named paths still check exactly those paths, and
a normalization failure produces a nonzero overall exit status.

palette failures are reported before region derivation, so a level can be
normalized without its geometry being examined first.

### negative paths, proven

all three were proven against temporary copies in the session scratchpad, never
in the repository, and no broken fixture was checked in:

```text
supply 6 -> 9 on a retained entry
    mismatch  FAILED at palette: prototile 35 supply=9 witness=6        exit 1

goul's id-23 entry reinstated with supply 4
    unused    FAILED at palette: prototile 23 is never placed ...       exit 1

both act0-level supplies returned to unlimited
    unlimited FAILED at palette: prototile 35 supply=unlimited witness=6 exit 1
              FAILED at palette: prototile 27 supply=unlimited witness=6
```

## player integration

`act0-level.tres` now presents two finite supplies of 6 rather than two unlimited
entries. `LevelPlayer` was not touched: finite supply presentation and its
restoration after undo already belonged to it and already worked.

`LevelPlayerIntegrationRunner` replaced its one expectation that both rows showed
*no* supply text with three:

```text
both rows present their remaining supply
both rows start at 6
placing one tile leaves 5 and 6
undo restores 6 and 6
```

remaining values are read through `State::supply_status`, the same derived status
the rendered row uses, so the test cannot pass against a stored counter that has
drifted from the arrangement. the runner went from 29 to 32 checks.

## documentation

`docs/TEST-LEVELS.md` gained a section for the two authored-content laws — no
unused palette entries, finite supply equals compiled witness usage — with what
each failure prints and what an author does about it, including that deleting an
unused entry may lower that level's `options` and `overlap`.

`level-notes.txt` records the normalization, the removed entries, the exact
glider color, the `guy_f` measurement change, and that `act0-level` is temporary
startup content rather than a canonical exam problem. the stale "cannot place the
initial tile" and `zooble` reports the act document asked to remove were already
gone: carson removed them in `798f5d7`, seven minutes before the act was
formalized, and added a difficulty ordering in the same commit. that ordering is
preserved, and the notes now state plainly that it is a difficulty grouping and
that the canonical exam order still has to be chosen.

## verification

with the godot editor closed throughout:

```text
scons tests                            up to date
./build/tests/tiles_tests              9921 checks, 0 failed across 427 cases
scons levelcheck                       rebuilt clean, no warnings
python3 tools/level_check/check_levels.py   all 24 levels pass, exit 0
scons                                  rebuilt clean, no warnings
res://tests/resource_integration.tscn      414 checks passed
res://tests/level_editor_integration.tscn  308 checks passed
res://tests/level_player_integration.tscn  32 checks passed
--headless --path godot-project --quit-after 2   boots galaxy.tres, exit 0
git diff --check                       clean
```

the two modified translation units compiled under the project's established
settings with no new diagnostics: `LevelCheck.cpp` under `-std=c++17 -O2`, and
`LevelPlayerIntegrationRunner.cpp` through the godot-cpp shared-library
environment.

no build product, exported bundle, or temporary file was added to the repository.

### left to carson

- **the manual pass.** open the player and confirm the finite supplies render and
  decrement, and look at the new glider yellow-orange against that level's other
  three colors. the checker is explicit that it judges nothing visual.
- **commit and push**, then confirm that the unified workflow produces successful
  windows x86_64, linux x86_64, macos universal, and combined `tiles-desktop`
  artifacts for that exact `main` commit.

## for the next formalizer

the canonical exam order is the one thing act 1 deliberately did not decide, and
`level-notes.txt` now says so in place of implying an order exists. the
difficulty grouping there is input to that decision, not the decision.

one consequence of normalization is worth carrying forward: with witness-exact
supplies, every shipped level's palette is now a countdown that reaches exactly
zero when the level is solved. the roadmap's first countdown scale is therefore
already true of the content, not just of the presentation.
