#!/usr/bin/env python3
"""Run the level checker over authored levels and report one table.

    scons levelcheck && python3 tools/level_check/check_levels.py

With no arguments it checks every .tres under godot-project/levels/. Named
paths check exactly those. The exit status is nonzero when any level fails, so
this drops straight into a pre-commit hook or CI step.
"""

import glob
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
CHECKER = os.path.join(ROOT, "build", "tools", "level_check")
LEVELS = os.path.join(ROOT, "godot-project", "levels", "*.tres")

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import decode_level  # noqa: E402


def main(argv):
    if not os.path.exists(CHECKER):
        sys.stderr.write(
            "level checker not built. run:\n\n    scons levelcheck\n\n")
        return 2

    paths = argv[1:] if len(argv) > 1 else sorted(glob.glob(LEVELS))
    if not paths:
        sys.stderr.write("no levels to check\n")
        return 2

    failed = []
    for path in paths:
        name = os.path.splitext(os.path.basename(path))[0]
        try:
            decoded = decode_level.decode(path)
        except Exception as error:  # noqa: BLE001 - report and keep going
            print("%-20s FAILED to decode: %s" % (name, error))
            failed.append(name)
            continue
        result = subprocess.run(
            [CHECKER, name], input=decoded, capture_output=True, text=True)
        sys.stdout.write(result.stdout)
        sys.stderr.write(result.stderr)
        if result.returncode != 0:
            failed.append(name)

    print()
    if failed:
        print("%d of %d levels FAILED: %s" % (len(failed), len(paths), ", ".join(failed)))
        return 1
    print("all %d levels pass" % len(paths))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
