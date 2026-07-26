# testing authored levels

a level can compile perfectly and still be unplayable. the editor and the player
do not offer the same moves, so an arrangement that was easy to *author* is not
automatically one a player can *rebuild*. this document describes the checker
that closes that gap, and what its answers mean.

## running it

```sh
scons levelcheck                              # build the checker
python3 tools/level_check/check_levels.py     # check every authored level
```

named paths check exactly those:

```sh
python3 tools/level_check/check_levels.py godot-project/levels/zooble.tres
```

the exit status is nonzero when any level fails, so it drops into a hook or a CI
step unchanged. a full pass over the current campaign takes about twenty seconds.

## what it reports

```text
zooble               ok   tiles=12  solvable=yes seed-safe=yes  options=52  overlap=4  deferrals=7.3
```

### the two play properties that must pass

**`solvable`** — can the authored solution be rebuilt from an empty board using
only moves the player is actually offered?

this is the one that catches levels the editor let you build but the player
cannot. it is not a claim that the level has a solution: the blueprint *is* a
solution, and compiling it already proved that. it is the claim that the
solution is **reachable** through the player's proposal rule.

**`seed-safe`** — can any legal opening move strand part of the level?

the checker opens with each authored placement in turn and grows from it. a
level is seed-safe when every one of those openings reaches the whole solution.
a level that is not seed-safe can be lost on move one, which the player has no
way to see coming.

a failure on either prints the reason underneath the row, and fails the run.

### the two that describe the palette

these are not about play at all. they are about the palette telling the truth
about the level, and they fail the run.

**no unused palette entries** — every entry is a tile the authored solution
actually places.

an entry the witness never uses is usually a helper tile left behind after the
tiling was rebuilt around it. it is not harmless: it is offered to the player,
takes up a palette row, and asks them to rule it out by hand. it also inflates
`options` and `overlap`, so the level measures as fiddlier than it plays.

**finite supply equals compiled witness usage** — a retained entry's supply is
exactly how many of that prototile the witness places.

this makes the palette part of the problem statement rather than a suggestion:
the countdown beside each row is the real number of pieces the solution needs.
supply above the witness count is slack nobody authored, and an unlimited supply
is not a statement about how many pieces a solution takes.

### the three that describe feel

these are never failures. they are how a level *plays*, and they are here so a
new level can be compared against the ones that already feel right.

**`options`** — the most candidate positions offered for one palette selection
at any point during the level. a large number is not a problem by itself;
candidates spread out across the region rather than stacking.

**`overlap`** — the most candidates that claim one single point. *this* is what
decides whether aiming is fiddly. the player's ghost prefers a candidate whose
footprint contains the pointer, so 1 means aiming is exact and larger numbers
mean the tie-break is doing work. the current campaign tops out at 5.

**`deferrals`** — averaged over 40 random build orders, how many times the tile
the player wanted next was not yet offered.

this is the level asking the player to think about order instead of dropping
tiles wherever they obviously go. it is the main difficulty signal, and
`seed-safe` is what guarantees a deferral is only ever a *wait*, never a loss.
for scale, in the current campaign `izjl` sits at 0.6 and `snake_tri_hex` at
148.4 — a very high number means a level is mostly a sequencing puzzle, which
may or may not be what was intended.

## the rules a level is checked against

everything except one piece is the real shipped code, linked directly: palette
compilation, blueprint compilation, region derivation, containment, supply,
legality, and completion all come from `src/core`, `src/content`, and
`src/engine`.

the one exception is the **proposal rule** — which positions the player is
offered — because `LevelPlayer` is Godot-coupled and cannot be linked into a
plain executable. it is therefore stated twice:

- `LevelPlayer::rebuild_proposals` in `src/game/LevelPlayer.cpp`
- `is_offered` and `proposals_for` in `tools/level_check/LevelCheck.cpp`

**changing one means changing the other.** if they drift, the checker is
measuring a game nobody is playing. the rule they both state is:

> a candidate position is offered when some vertex of the candidate polygon
> lands exactly on an anchor vertex, the engine accepts the resulting
> placement, and — unless the board is empty — the footprint shares a length
> of boundary with something already placed.
>
> the anchors are every placed footprint vertex together with every region
> boundary vertex.

two consequences worth knowing while authoring:

- **an opening move always exists.** a convex corner of a region can only be
  covered by a tile carrying a vertex exactly there, and region boundary
  vertices are anchors. no level needs anything authored at the world origin.
- **the first move is free and every later one must join.** this is a rule of
  play, not of legality — the engine would happily accept a second disconnected
  island. it is where a level's difficulty comes from, and it is why `deferrals`
  is nonzero.

## when a level fails

**`FAILED at region: tiles meet at a point`** — two tiles touch only at a
corner on the coverage boundary, so the arrangement does not describe one
region. this is a real authoring constraint, not a checker limitation: fix the
tiling.

**`FAILED at region: coverage is more than one connected piece`** — the
blueprint has separated islands. a level is one connected region; a second area
needs to be joined or dropped.

**`FAILED at blueprint compilation, record N`** — record `N` names something the
palette does not offer, exceeds a supply, or overlaps an earlier record.

**`solvable=NO`** — the arrangement is a valid tiling that the player cannot
rebuild. in practice this means it was authored against a helper tile that was
later deleted, so some placement's position is not nameable from the tiles that
remain. re-author the affected pieces so each one's position is determined by a
neighbour or by the region boundary.

**`seed-safe` short of the tile count** — some opening move strands the level.
the row names how many openings are safe and how far the worst one gets.

**`FAILED at palette: prototile N is never placed by the witness`** — the
palette carries an entry the solution does not use. it reports the configured
supply and the witness count, which is zero. delete the entry: both its
`PaletteEntryResource` block and its reference in the palette's `entries` array.
the remaining entries keep their order and their ids are never renumbered.

**`FAILED at palette: prototile N supply=S witness=W`** — the entry's supply
disagrees with the solution. set `supply = W`. an unlimited supply reports as
`supply=unlimited` rather than as a number, because "as many as you like" is a
different authored statement from any particular count, and printing `W` there
would read as though the level already said it.

both palette failures are reported before the level's geometry is examined, so a
level can be normalized without solving anything else first. deleting an unused
entry may lower that level's `options` and `overlap`, because those maxima range
over the palette — that is the measurement catching up with the content, not a
change in the level's geometry.

## what is not checked

- whether a level is *fun*, or well-paced, or fairly ordered within the campaign
- whether the palette's order matches its intended place in the countdown
- anything visual: colours, scale, framing, or how the region reads as a shape
- whether solutions other than the authored one exist — many usually do, and
  the domain treats every legal exact cover as a solution

the checker proves a level is playable. everything else is still playtesting.
