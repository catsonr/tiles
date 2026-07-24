# domain definitions

this document defines the game's mathematical vocabulary and invariants. it is
not an implementation plan or roadmap.

## type sketch

the notation is illustrative haskell, not a commitment to an implementation:

```haskell
type Triangle = NondegenerateTriple Point
type Triangulation = ExactInteriorDecomposition Triangle

data Polygon = Polygon
  { vertices      :: ValidatedCyclicSequence Point
  , triangulation :: Triangulation
  }

data Prototile = Prototile
  { identity :: PrototileId
  , polygon  :: Polygon
  }

data Placement = Placement
  { prototile  :: Prototile
  , orientation :: Orientation
  , translation :: Point
  }

type Arrangement = FiniteCollection Placement

data Supply
  = Finite PositiveInt
  | Unlimited

type Palette = NonEmptyMap Prototile Supply

data RotationRule
  = Uniform OrientationSet
  | PerPrototile (Map PrototileId OrientationSet)

data Region = Region
  { outerBoundary   :: Polygon
  , innerBoundaries :: [Polygon]
  }

type Target = NonEmptySet Region

data Level = Level
  { palette      :: Palette
  , target       :: Target
  , rotationRule :: RotationRule
  }
```

## polygon

a **polygon** is a closed, filled, hole-free geometric shape represented by a
finite cyclic sequence of vertices and a triangulation of its interior.

a polygon:

- has at least three vertices and nonzero area;
- has no zero-length edges;
- has no self-intersections;
- has no redundant vertex strictly inside a straight edge;
- may be convex or concave.

the sequence determines which vertices are connected. it is not an unordered
set. non-simple loops are invalid input, not another kind of polygon.

a polygon with `n` vertices carries exactly `n - 2` nondegenerate triangles.
their vertices are polygon vertices, their interiors are pairwise disjoint,
and their union is the polygon. the triangulation is derived evidence, not
part of polygon identity.

coordinate representation is deliberately unspecified. the definition does
not choose floating point, fixed point, or an integer grid.

> "vertices are the correct abstraction!"

**motivation:** vertices remain the shared rendering abstraction, while
triangles give geometry a certified decomposition into simple convex pieces.
curves may be quantized into polygons before entering the model.

## prototile

a **prototile** is a named, particular polygon admitted as a playable tile
type. a polygon is reusable geometry; a prototile gives that geometry identity
within the game.

prototiles are considered equivalent under translation and rotation, but not
under scaling or reflection:

- translation and rotation do not produce new prototiles;
- a differently scaled polygon is a different prototile;
- reflecting a chiral polygon produces a different prototile;
- reflecting an achiral polygon does not produce distinct geometry.

different names do not make congruent polygons geometrically distinct or
increase a palette's order.

> "a prototile is some polygon [...] no holes. and for now no decoration
> either. no edge connection rules. just a polygon."

**motivation:** prototiles contain no decoration, curves, holes, or adjacency
rules. legality is determined by geometry, supply, and rotation alone.

## placement and footprint

a **placement** is a prototile together with one orientation and one
translation:

```text
placement = prototile × orientation × translation
```

the transformation contains no scaling or reflection.

the **footprint** of a placement is the closed, filled polygon produced by that
transformation:

```haskell
footprint :: Placement -> Polygon
```

**motivation:** a placement is game state; its footprint is geometry.
intersection, containment, and union operate on footprints.

## arrangement and coverage

an **arrangement** is a finite collection of placements whose interiors are
pairwise disjoint:

```text
for every distinct a and b:
interior(footprint(a)) ∩ interior(footprint(b)) = ∅
```

footprints may touch along edges, partial edges, or individual points. this is
necessary because polygons are closed and adjacent tiles share boundaries.

an arrangement is unordered mathematically, even if an implementation stores
it in a sequence.

the **coverage** of an arrangement is the union of its footprints:

```haskell
coverage :: Arrangement -> PolygonalSet
coverage = union . map footprint
```

## palette and order

a **palette** is a finite, nonempty mapping from distinct prototiles to their
available supplies.

a supply is either:

- a positive finite number; or
- unlimited.

a zero supply means the prototile is absent from the palette.

the **order** of a palette is its number of prototile types, not its total
number of available pieces:

```text
order(palette) = number of keys in palette
```

> "a pallete may or may not have infinitely many of any prototile"

**motivation:** limited pieces are useful level-design constraints, while order
continues to measure the countdown's distinct tile types.

## orientation and rotation rule

an **orientation** is an angle modulo one complete turn:

```text
θ ≡ θ + kτ
```

an **orientation set** is finite and nonempty.

a **rotation rule** assigns every prototile in a palette an orientation set. it
is either:

- `Uniform`, assigning one set to every prototile; or
- `PerPrototile`, exhaustively assigning a set to each prototile.

the normalized meaning is always:

```haskell
allowedOrientations
  :: RotationRule
  -> Prototile
  -> NonEmptySet Orientation
```

the set is mathematically unordered. an interface may cycle through it in
angular order.

**motivation:** a level may share one rule across its palette or permit
different rotations for different prototiles without permitting arbitrary
rotation.

## region and target

a **region** is one bounded, connected, closed polygonal area. it consists of
one outer polygon and zero or more holes.

each hole:

- lies strictly inside the outer polygon;
- does not overlap or touch another hole;
- does not touch the outer boundary.

the region contains the outer polygon's filled area minus each hole's interior.
all outer and inner boundaries remain part of the region.

holes are part of the domain even if the earliest levels do not use them.

a **target** is a finite, nonempty set of pairwise-disjoint regions. its area is
their union.

> "if we want them disjoint we just ... introduce a new region to the level"

**motivation:** a region retains one connected spatial meaning. a target can
still ask the player to cover multiple separated areas.

## level, legality, and solution

a **level** is the product of a palette, target, and rotation rule:

```text
Level = Palette × Target × RotationRule
```

an arrangement is **legal for a level** exactly when:

- its placement interiors are pairwise disjoint;
- every placement uses a prototile from the palette;
- every placement obeys its prototile's rotation rule;
- no prototile's finite supply is exceeded;
- every footprint lies within the target.

an arrangement **solves a level** exactly when it is legal and its coverage
equals the target area:

```text
solves(level, arrangement) ⇔
    legalFor(level, arrangement)
    ∧ coverage(arrangement) = targetArea(level)
```

a level is **well-formed** only if at least one arrangement solves it.

> "it's GREAT if a given region has many tiling solutions!"

**motivation:** a known solution proves that authored content is possible, but
it is not privileged at runtime. every legal arrangement with exact coverage is
a solution.

## excluded concepts

the domain currently excludes:

- reflections of placed prototiles;
- scaling;
- curved edges;
- holes within prototiles;
- decoration and edge-matching rules;
- constraints on translation.

these are absent from the model, not deferred assumptions hidden inside it.
