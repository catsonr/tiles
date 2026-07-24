#!/usr/bin/env python
import os
import sys

libname = "tiles"
projectdir = "godot-project"

localEnv = Environment(tools=["default"], PLATFORM="")

customs = ["custom.py"]
customs = [os.path.abspath(path) for path in customs]

opts = Variables(customs, ARGUMENTS)
opts.Update(localEnv)

Help(opts.GenerateHelpText(localEnv))

env = localEnv.Clone()

cpp_dir = os.path.join("vendor", "godot-cpp")
if not (os.path.isdir(cpp_dir) and os.listdir(cpp_dir)):
    print(
        "error: vendor/godot-cpp is empty. initialize the submodule first:\n"
        "\n"
        "    git submodule update --init --recursive\n",
        file=sys.stderr,
    )
    Exit(1)

# The api target defaults to Godot 4.7: that is the version bundled by the
# pinned vendor/godot-cpp, so plain `scons` needs no extra argument. Passing
# `custom_api_file=` on the command line still overrides it.
env = SConscript(os.path.join(cpp_dir, "SConstruct"), {"env": env, "customs": customs})

env.Append(CPPPATH=["src/"])

# Recursively discover every authored .cpp under src/ in deterministic order so
# adding a node never requires editing this file.
sources = []
for root, _dirs, _files in sorted(os.walk("src")):
    sources += sorted(Glob(os.path.join(root, "*.cpp")))

# Derive the artifact name from the selected godot-cpp environment so it agrees
# exactly with godot/tiles.gdextension. `.dev`/`.universal` don't affect
# compatibility, so they're stripped from the filename.
suffix = env["suffix"].replace(".dev", "").replace(".universal", "")
lib_filename = "{}{}{}{}".format(
    env.subst("$SHLIBPREFIX"), libname, suffix, env.subst("$SHLIBSUFFIX")
)

# Write the shared library directly into the Godot project so the local
# editor/debug runtime can load it without a separate install step.
library = env.SharedLibrary(
    "{}/bin/{}/{}".format(projectdir, env["platform"], lib_filename),
    source=sources,
)

Default(library)

# Native core-and-engine tests: a dependency-free executable built from
# src/core/, src/engine/, and tests/ with a plain toolchain environment,
# deliberately unaware of godot-cpp. Invoke explicitly with `scons tests`; it is
# never part of the default build. Object files use the default `.o` suffix, so
# they never collide with the shared library's `.os` objects for the same
# sources.
test_env = Environment(tools=["default"])
test_env.Append(CPPPATH=["src/"])
test_env.Append(CXXFLAGS=["-std=c++17"])

test_sources = []
for tree in ("src/core", "src/engine", "tests"):
    for root, _dirs, _files in sorted(os.walk(tree)):
        test_sources += sorted(Glob(os.path.join(root, "*.cpp")))

test_program = test_env.Program(
    target="build/tests/tiles_tests",
    source=test_sources,
)

test_env.Alias("tests", test_program)
