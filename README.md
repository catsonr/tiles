# tiles

`tiles` is a puzzle game conceived for the gmtk 2026 theme, "count down." each
level asks the player to arrange rotated and translated polygonal prototiles so
they exactly cover a bounded target without overlaps. the current campaign
pitch counts down the palette's order—the number of distinct prototile
types—from generous early palettes toward celebrated two- and one-prototile
tilings.

the game is still being designed. its shared vocabulary and current invariants
are defined in [DEFINITIONS.md](DEFINITIONS.md); everything else remains open to
playtesting and iteration.

## project structure

```text
src/            authored C++ (compiled into the GDExtension)
vendor/         third-party source (godot-cpp, pinned as a submodule)
godot-project/  the Godot project and its assets
```

The native library is built from `src/` and written into
`godot-project/bin/<platform>/`, where the `.gdextension` descriptor loads it.

## build

Prerequisite — fetch the `godot-cpp` submodule:

```sh
git submodule update --init --recursive
```

Build the library (plain `scons` targets the Godot API bundled by the pinned
`godot-cpp` and needs no extra arguments):

```sh
scons
```

`scons` recursively discovers every `.cpp` under `src/`, so adding a source file
never requires editing the build. Generate a compilation database for C/C++
editor tooling with:

```sh
scons compiledb=yes compile_commands.json
```
