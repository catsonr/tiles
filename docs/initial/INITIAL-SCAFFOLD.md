# initial gdextension scaffold

## goal

scaffold this repository as a minimal Godot 4.7.1 project whose native code is
written in c++ through gdextension.

the finished workflow is:

```text
edit src/ → scons → run or reload Godot → repeat
```

this implementation ends when one disposable c++ node compiles, registers,
instantiates, and runs. it must not begin implementing the tiling game.

use `/Users/carson/Desktop/wrkspc/rhythm-game/` (`BEATBOXX`) as the local
reference for the intended scons, `godot-cpp`, and class-registration workflow.
use current Godot 4.7.1 and current `godot-cpp` conventions where that older
project has drifted.

## fixed structure

```text
.
├── .gitignore
├── .gitmodules
├── README.md
├── SConstruct
├── Godot.app/                         # existing local editor; ignored
├── docs/
│   └── INITIAL-SCAFFOLD.md
├── godot/
│   ├── project.godot
│   ├── main.tscn
│   ├── tiles.gdextension
│   └── bin/                           # generated; ignored
├── src/
│   ├── register_types.h
│   ├── register_types.cpp
│   └── nodes/
│       ├── GDExtensionSmokeTest.h
│       └── GDExtensionSmokeTest.cpp
└── vendor/
    └── godot-cpp/                     # git submodule
```

these directory meanings are stable:

```text
src/    = authored c++
vendor/ = third-party source
godot/  = the Godot project and its assets
```

do not add other nodes, subsystems, managers, utilities, or abstractions.

## `godot-cpp`

add `https://github.com/godotengine/godot-cpp.git` as the
`vendor/godot-cpp` submodule. use a current `master` commit that supports the
Godot 4.7 api; the resulting gitlink pins that exact commit. do not reuse
`BEATBOXX`'s old 4.5 checkout and do not configure a floating runtime dependency.

plain `scons` must target api version 4.7 without requiring an extra argument.
an explicit command-line override may still be supported.

do not add miniaudio. it will belong under `vendor/` in later work.

## build

the root `SConstruct` must:

- fail clearly when the submodule is uninitialized;
- load `vendor/godot-cpp/SConstruct`;
- default the api target to Godot 4.7;
- add `src/` to the include path;
- recursively discover all `.cpp` files under `src/` in deterministic order;
- build a shared library named `tiles`;
- write it directly to `godot/bin/<platform>/`;
- derive its filename from the selected `godot-cpp` environment so it agrees
  exactly with `godot/tiles.gdextension`;
- support both `scons compiledb=yes` and
  `scons compiledb=yes compile_commands.json`;
- make the default build usable by the local editor/debug runtime.

follow the current official `godot-cpp-template` artifact naming and
platform-directory convention. the initial implementation must compile and run
on apple silicon macos. the descriptor must also contain matching debug and
release entries for linux x86_64, linux arm64, and windows x86_64.

do not add cmake, ci, exports, mobile builds, or web builds.

## registration

use `tiles` as the c++ namespace and extension/library name. the conventional
registration boundary must provide:

```cpp
void initialize_tiles_module(godot::ModuleInitializationLevel p_level);
void uninitialize_tiles_module(godot::ModuleInitializationLevel p_level);
```

at `MODULE_INITIALIZATION_LEVEL_SCENE`, register
`tiles::GDExtensionSmokeTest` with `GDREGISTER_CLASS`. export
`tiles_library_init` with `extern "C"`, install the initializer and terminator
on `godot::GDExtensionBinding::InitObject`, and set its minimum initialization
level to `MODULE_INITIALIZATION_LEVEL_SCENE`.

keep registration centralized in `register_types.cpp`. adding a future node
must require only adding its `.h`/`.cpp`, including its header there, and adding
one registration call. adding a `.cpp` must not require editing `SConstruct`.

## disposable smoke test

implement:

```cpp
tiles::GDExtensionSmokeTest : public godot::Node
```

use `GDCLASS`, an empty `_bind_methods()`, and the smallest valid `_ready()`
implementation that prints exactly:

```text
[tiles] gdextension smoke test ready
```

this node proves the toolchain and nothing else. do not rename it `GameRoot`,
attach game state to it, or treat it as runtime architecture. use the direct
`.h`/`.cpp` class style already present in `BEATBOXX`.

## Godot project

create a minimal Godot 4.7 project named `tiles`:

- `res://main.tscn` is the main scene;
- its root is the registered `GDExtensionSmokeTest`;
- it has no children, script, or visual content;
- the project uses default single precision;
- it has no autoloads, plugins, gdscript, assets, or export presets.

`godot/tiles.gdextension` must use `tiles_library_init`, declare minimum
compatibility with Godot 4.7, set `reloadable = true`, and reference the exact
desktop outputs beneath `res://bin/`. use modern platform, target,
architecture, and precision feature tags where applicable.

do not create or commit `.uid` files merely to manufacture identifiers; use
plain resource paths where sufficient.

## repository hygiene

ignore at least:

```text
.DS_Store
Godot.app/
.scons_cache/
.sconsign.dblite
compile_commands.json
godot/.godot/
godot/bin/
```

also ignore c/c++ intermediate objects if any can appear outside already
ignored build directories. do not ignore authored source, the gdextension
descriptor, or the submodule declaration.

the existing `Godot.app` is the local Godot 4.7.1 editor. leave it untouched.

keep `README.md` short and operational. document:

```sh
git submodule update --init --recursive
scons
scons compiledb=yes compile_commands.json
./Godot.app/Contents/MacOS/Godot --editor --path godot
```

also document the Godot 4.7.1 + gdextension/c++ baseline and the three steps for
adding/registering another c++ node. document no speculative workflow.

## out of scope

do not implement or port `AudioEngine`, `SceneMachine`, miniaudio, gameplay,
geometry, input, ui, rendering, audio, save data, tests beyond the smoke check,
ci, exports, packaging, web support, formatting infrastructure, or licensing.

`BEATBOXX` already has mature components that may be borrowed deliberately
later. do not create simplified replacements now.

do not commit.

## acceptance

the implementor must:

1. initialize `vendor/godot-cpp`;
2. run a clean plain `scons` build;
3. confirm the library exists beneath `godot/bin/macos/`;
4. run the main scene with the local Godot 4.7.1 binary;
5. observe `[tiles] gdextension smoke test ready`;
6. observe no gdextension load, entry-symbol, registration, or library errors;
7. generate `compile_commands.json` through scons and confirm it contains the
   scaffold sources;
8. confirm generated Godot, scons, binary, and compilation-database artifacts
   are ignored by git;
9. confirm no out-of-scope machinery was added;
10. do not commit.

a suitable bounded headless run is:

```sh
./Godot.app/Contents/MacOS/Godot --headless --path godot --quit-after 2
```

if Godot 4.7.1 requires an equivalent bounded-execution flag, use it. the main
scene must actually run and its native `_ready()` output must be observed.

the result should be boring and legible: a fresh checkout with initialized
submodules can build with `scons`, run `godot/`, and gain c++ editor tooling
through `compile_commands.json`. nothing more belongs in this scaffold.
