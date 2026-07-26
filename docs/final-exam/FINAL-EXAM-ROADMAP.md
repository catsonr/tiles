# final exam roadmap

## status of this document

this roadmap is mutable.

it exists to preserve continuity between the separately formalized and
implemented acts of `final-exam`. it records the direction we have agreed upon,
the boundaries which later work should respect, and the questions which are
still deliberately open.

this is not an implementation contract. each act receives its own formalization
before implementation, and that act document—not this roadmap—must resolve the
details needed by its implementor. once formalized, an act remains immutable
under the project rules; this roadmap may instead be carefully updated as the
larger design develops.

do not treat an example name, diagram, or possible mechanism below as a hidden
specification.

## current foundation

`final-exam` act 0 produced one complete mechanical level-playing slice:

```text
authored level
    ↓
exact empty Session
    ↓
select + rotate + place + remove + undo
    ↓
exact State::solved()
    ↓
visible completion
```

the project now contains a growing authored campaign and a level-checking suite.
at the time this roadmap was written, all 24 level artifacts passed the current
mechanical checks. that number is not the campaign's fixed size: more levels
will be authored, checked, and inserted into a canonical order.

the remaining project is primarily application structure, presentation, audio,
content normalization, and release cohesion. the exact geometry and play rules
remain the foundation rather than being reimplemented by the exam surface.

## the countdown

the game jam theme is `count down`. the final game expresses it at three nested
scales:

```text
one palette entry
    remaining supply = configured supply − placed instances

one problem
    remaining area = total region area − covered area

the complete exam
    remaining problems = total problems − completed problems
```

problems themselves are numbered in descending canonical order:

```text
problem n
problem n − 1
problem n − 2
...
problem 1
```

`n` and every displayed problem number are derived from the canonical exam
manifest. no implementation may assume that the campaign contains exactly 24
levels.

there is no exam-time countdown. the earlier timer and timer-derived difficulty
idea is discarded.

## act 1 — export integration and authored content

this act collects the small, concrete work which should become settled project
infrastructure.

### desktop export

- merge `export-test` onto `main`;
- preserve the already tested macos, windows, and linux desktop export paths;
- make the export workflow produce builds from the newest intended `main`; and
- leave the repository in a state where pushing the newest game can produce the
  current desktop artifacts without reconstructing the export work.

the act formalizer must inspect the actual branch relationship and current
workflow before specifying the integration. this roadmap authorizes no blind
conflict resolution or loss of later `main` work.

### level normalization

the ordinary palette supply for an authored level is the number of instances of
that prototile in the level's compiled witness arrangement. a level may
deliberately specify another supply, but the exception must be intentional and
recorded.

palette entries unused by the authored witness should ordinarily be removed.
known visual or content notes should be resolved alongside normalization rather
than hidden by the campaign presentation.

[`level-notes.txt`](../../level-notes.txt) is the human source of continuity for:

- the canonical level order;
- the default witness-derived supply convention;
- deliberate supply exceptions;
- unfinished level-specific fixes; and
- new levels which have not yet entered the canonical exam.

the level-checking workflow should continue to make newly authored content easy
to validate. whether and how it mechanically enforces campaign ordering,
unused-entry detection, or supply conventions belongs to act formalization.

## act 2 — audio

tiles will use one persistent miniaudio-based audio engine, informed by the
working implementation in:

```text
~/Desktop/wrkspc/rhythm-game/
```

that project is a reference and source of proven pieces, not a component to copy
wholesale. its conductor, beat data, track-editing behavior, global context, and
other rhythm-game-specific machinery are outside tiles.

the tiles audio surface must support:

- one background music track;
- looping music playback;
- sound-effect cues with multiple authored variants;
- suitable ownership for overlapping short effects;
- packaged `res://` assets through the Godot-aware miniaudio vfs;
- clean initialization and shutdown; and
- the desktop export platforms established by act 1.

the required sound-effect cues are:

```text
tab / palette selection
rotate
place
remove or undo
active ghost hover change
victory
```

each cue may contain variants. the exact resource types, variant-selection
policy, voice policy, volume controls, failure behavior, and semantic trigger
rules belong to the act 2 formalization.

one application-level audio engine must survive changes in visible problem
content. individual problem views must not create competing audio engines or
restart the background music as they mount and unmount.

## act 3 — the exam and reusable problem pages

the central application model is an `Exam` containing the canonically ordered
problems and their persistent play state.

the names below are provisional, but the ownership direction is not:

```text
Exam
├── ordered problem manifest
├── one persistent state slot per problem
└── document-wide completion state

ProblemPage
├── problem number and text
├── palette presentation
├── interactive level content
├── remaining-area presentation
└── completion / proof presentation
```

`ProblemPage` is presently preferred over `LevelPage` because `Level` already
names engine content while a mathematical problem page contains that content,
its proposition, and its presentation.

### persistent state

the `Exam` owns the state which must survive movement through the document.
a problem view only borrows or binds to that state.

the intended law is:

```text
destroying a ProblemPage does not destroy problem progress
```

the persistent surface includes at least the exact arrangement, undo history,
selection needed for a coherent return, supply state derived from the
arrangement, and completion. transient pointer and hover state need not survive
an unmount.

every canonical problem has a persistent slot, but that does not require every
level resource, exact runtime, or visible page to be constructed at startup.
loading and compilation may happen on demand. the exact state type, lifetime,
loading boundary, and way a `ProblemPage` binds to it belong to formalization.

unless a later decision adds save data, “persistent” here means persistent for
the lifetime of the running `Exam`, not necessarily across application
launches.

### reusable page content

the first presentation goal is one complete, host-neutral `ProblemPage`.
it must not assume that it fills the application window, lives in a scrolling
container, or is the only page in existence.

it composes the existing exact play mechanics with the mathematical statement
and the three relevant per-problem countdown presentations. it is the actual
game content; document navigation and visual polish are built around it later.

the defining behavioral proof is:

```text
mount one problem
    ↓
interact with its exact Session
    ↓
unmount it
    ↓
visit another problem
    ↓
remount the first
    ↓
its persistent player state is unchanged
```

act 3 should establish this boundary before committing the game to either
continuous scrolling or pagination.

## act 4 — the complete mathematical document

the complete game is presented as one mathematics document whose front matter
and problems share a single canonical order:

```text
title page
acknowledgements
table of contents
problem n
problem n − 1
...
problem 1
```

the table of contents shows the complete ordered problem set, communicates
completion, and links or navigates to each problem.

### interchangeable document host

the document host owns visible `ProblemPage` instances but not their persistent
play state.

continuous scrolling is the preferred experiment. in that form, the title,
acknowledgements, table of contents, and complete problem sequence inhabit one
vertically continuous document. problem views are mounted on demand around the
visible region and may be unmounted away from it.

pagination is the supported alternative if continuous scrolling does not prove
pleasant or reliable. it presents the same ordered document, front matter,
problem pages, state, and table of contents through one or a small number of
visible pages.

the architecture must keep this a document-host decision:

```text
Exam state
    ↓
reusable ProblemPage
    ↓
continuous host or paginated host
```

the continuous design must prove more than raw rendering. it must preserve
stable navigation destinations, avoid disruptive layout movement, cooperate
with puzzle input, and restore an unmounted problem without changing its state.
the formalized act will decide the exact feasibility proof and the conditions
for selecting the paginated host.

the loading policy, prefetch boundary, placeholder layout, synchronous or
threaded resource use, and resource-cache behavior remain implementation
questions. “unloaded” need not mean that every byte of an already visited level
is purged; it means that offscreen presentation work is not retained merely
because the level exists in the document.

## act 5 — mathematical-document presentation

after reusable problem content and document composition work, the game receives
one coherent visual and interaction pass.

the target is a dark mathematical paper or compiled-pdf feeling:

- a dark document and restrained light typography;
- clear problem hierarchy and typeset proposition text;
- authored prototile colors used as mathematical figures;
- palette supplies presented as part of the problem statement;
- remaining area and remaining problems integrated into the document;
- completion presented as a proof mark rather than the temporary player
  overlay; and
- title, acknowledgements, table of contents, and problems reading as one
  intentional object.

the exact fonts, text markup, margins, page boundaries, document chrome,
animation, and other aesthetic choices are not decided here. the goal is the
feeling of a latex mathematics document, not necessarily running latex or
embedding a literal pdf renderer.

presentation work must not replace exact model facts with rendered geometry.
lossy values may describe progress visually, but exact command legality and
completion remain owned by the existing engine.

## whole-project acceptance

each act formalization defines its own verification. across the finished
`final-exam` project, the following properties must remain true:

- every canonical level passes the authored-level validation expected at that
  point in development;
- the exam order, descending problem numbers, table of contents, and total
  problem countdown agree;
- palette supply, remaining area, removal, undo, and completion remain derived
  from authoritative play state;
- a problem may leave and re-enter the visible document without losing its
  persistent state;
- audio follows successful semantic game events without becoming part of their
  legality;
- the ordinary application boots into the intended exam experience; and
- macos, windows, and linux exports contain and load the required native
  extension, levels, fonts, music, and sound effects.

manual playtesting remains necessary. the mechanical checker cannot establish
that document navigation feels natural, a level is enjoyable, an audio cue is
pleasant, typography is readable, or the ordered exam has a satisfying arc.

## deliberately open questions

later formalizers should resolve only the questions required by their acts.
currently open questions include:

- the exact exam-manifest resource format and its relationship to
  `level-notes.txt`;
- how persistent problem state is detached from and rebound to a visible page;
- whether state persists across application launches; -- carson here, NO.
- how remaining area is formatted for human readers;
- the exact continuous-scroll mounting and unloading policy;
- whether the proven final host is continuous or paginated;
- the precise title, acknowledgements, and table-of-contents content;
- typography and font assets;
- completion animation and document-wide final completion behavior; and
- player-facing behavior when shipped content fails to load.

these are not omissions for an implementor to guess through. they are routing
markers for the formalization sessions which will follow.
