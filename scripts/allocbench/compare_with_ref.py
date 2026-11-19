#!/usr/bin/env python3
"""Compare working-tree files with their versions at a git ref.

    compare_with_ref.py REF PATH [PATH ...]

Directories are expanded recursively. Prints one line per file:
  same      identical to REF
  DIFFERENT exists at REF with different content
  NEW       not present at REF
Exit status 1 if any file is DIFFERENT (i.e. holds edits REF doesn't have).
"""

import os
import subprocess
import sys


def files(paths):
    for p in paths:
        if os.path.isdir(p):
            for root, _, names in os.walk(p):
                for n in sorted(names):
                    yield os.path.join(root, n)
        else:
            yield p


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    ref, paths = sys.argv[1], sys.argv[2:]
    different = 0
    for f in files(paths):
        r = subprocess.run(["git", "show", "%s:%s" % (ref, f)], capture_output=True)
        if r.returncode != 0:
            print("NEW       %s" % f)
            continue
        with open(f, "rb") as fh:
            same = fh.read() == r.stdout
        print("%s %s" % ("same     " if same else "DIFFERENT", f))
        different += not same
    sys.exit(1 if different else 0)


if __name__ == "__main__":
    main()
