# initial scaffold — implementation notes

Notes from implementing `docs/INITIAL-SCAFFOLD.md`. The scaffold builds, runs,
and prints its native `_ready()` line on Apple Silicon macOS with zero
GDExtension load/symbol/registration errors.

## pins

- **godot-cpp**: `vendor/godot-cpp` submodule pinned at commit
  `87c6629ffe0c89af48bf4df866a3772b53da7a05` (current `master`).
  - No `4.7` branch or `godot-4.7-stable` tag exists yet on godot-cpp; the
    latest published branch is `4.5`. `master` is where the 4.7 API lives.
  - That commit bundles `gdextension/extension_api.json` =
    `Godot Engine v4.7.stable.official`, `precision: single` — matching the
    local `Godot.app` (4.7.1.stable).
  - The gitlink pins this exact commit; there is no floating branch dependency.

## api target

- godot-cpp has **no** `godot_version=` build option (verified against
  `tools/godotcpp.py`). The API version is fixed by the bundled
  `extension_api.json`, i.e. by the submodule pin. So plain `scons` targets 4.7
  with no extra argument, purely by virtue of the pinned commit.
- The only override is `custom_api_file=<path>` on the command line.

## build

- `SConstruct` loads `vendor/godot-cpp/SConstruct`, appends `src/` to the
  include path, and **recursively** globs `src/**/*.cpp` in sorted, deterministic
  order (`os.walk` + sorted `Glob`). Adding a `.cpp` never requires a build edit.
- Guards against an uninitialized submodule with a clear error + `Exit(1)`.
- The artifact filename is derived from the godot-cpp environment
  (`$SHLIBPREFIX` + `libname` + `env["suffix"]` (with `.dev`/`.universal`
  stripped) + `$SHLIBSUFFIX`) so it agrees byte-for-byte with the descriptor.
- Writes the library **directly** to `godot-project/bin/<platform>/`. The
  upstream `godot-cpp-template` builds to a root `bin/` and then `env.Install`s a
  copy into the project; that two-step is intentionally collapsed per spec.
- `scons compiledb=yes compile_commands.json` works (compiledb support comes
  from godot-cpp's SConstruct); the database contains the authored sources
  `src/register_types.cpp` and `src/nodes/GDExtensionSmokeTest.cpp`.

### macOS: universal, not arm64-only

- godot-cpp defaults macOS `arch` to `universal`, so plain `scons` produces a
  fat `x86_64 + arm64` binary named `libtiles.macos.template_debug.dylib`
  (the `.universal` token is stripped from the filename).
- `arch=arm64` roughly halves the first full compile but renames the output to
  `libtiles.macos.template_debug.arm64.dylib`, which would **not** match the
  descriptor's `macos.single.debug` entry. We chose universal so plain `scons`
  "just works" and the Mac build already covers Intel. Revisit if the one-time
  godot-cpp compile becomes a friction point (the ongoing `src/`-only rebuild is
  instant either way).

## descriptor

- `godot-project/tiles.gdextension` uses the modern feature-tag key format
  (`macos.single.debug`, `linux.x86_64.single.debug`, …), not BEATBOXX's older
  `<platform>.<target>` scheme. macOS uses a plain `.dylib`, not a `.framework`.
- `entry_symbol = tiles_library_init`, `compatibility_minimum = "4.7"`,
  `reloadable = true`. Single precision (`.single.` tags) matches the project.
- Desktop-only entries, debug + release each: macOS, Windows x86_64,
  Linux x86_64, Linux arm64. Paths are `res://bin/<platform>/…`. Only the macOS
  outputs are built so far; the Linux/Windows names follow the standard template
  convention and will match native builds on those hosts.

## registration

- Boundary in `src/register_types.cpp`: `initialize_tiles_module` /
  `uninitialize_tiles_module`, both gated on
  `MODULE_INITIALIZATION_LEVEL_SCENE`; `extern "C" tiles_library_init` installs
  them on `GDExtensionBinding::InitObject` with min level SCENE.
- Adding a node = add its `.h`/`.cpp` under `src/`, `#include` it here, add one
  `GDREGISTER_CLASS(tiles::…)` call. Namespace is `tiles`.

## project layout deviations from the spec

- The Godot project directory is **`godot-project/`**, not `godot/`. Carson
  renamed it and initialized it through the editor (GL Compatibility renderer,
  Jolt physics, default icon) for correct project setup. `SConstruct`,
  `.gitignore`, and `README.md` all reference `godot-project/`.
- An empty vestigial `godot/` directory remains at the repo root (left in place,
  not deleted).
- `godot-project/main.tscn` is hand-written; its root is `GDExtensionSmokeTest`
  referenced by node **type**, with no `uid`.

## open item: the generated `.uid`

- Opening the project caused Godot to auto-generate
  `godot-project/tiles.gdextension.uid`. Nothing references it (the scene uses
  the node type; `project.godot` uses a plain `res://` path). It is currently
  **untracked and not ignored**, so it would be committed as-is. The spec leans
  against committing `.uid` files where plain paths suffice — left for Carson to
  decide (ignore vs. commit).

## the disposable node

- `tiles::GDExtensionSmokeTest` exists only to prove the toolchain: `GDCLASS`,
  empty `_bind_methods()`, and a `_ready()` that prints
  `[tiles] gdextension smoke test ready`. It is not architecture — expect it to
  be deleted once real nodes land.
