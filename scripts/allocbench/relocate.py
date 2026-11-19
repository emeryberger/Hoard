#!/usr/bin/env python3
"""Move benchmark artifacts out of /tmp (tmpfs on Linux: lost on reboot).

    relocate.py [--dry-run] DEST_DIR SRC [SRC ...]

Each SRC is moved to DEST_DIR/<basename of SRC>. Refuses to overwrite an
existing destination. A SRC that is a git worktree (a `.git` *file*) is moved
with `git worktree move`, so the main repository's worktree records stay
valid; everything else uses shutil.move (copy + delete across filesystems).
Missing SRCs are reported and skipped.
"""

import argparse
import os
import shutil
import subprocess
import sys


def is_worktree(path):
    return os.path.isfile(os.path.join(path, ".git"))


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--dry-run", action="store_true")
    p.add_argument("dest")
    p.add_argument("srcs", nargs="+")
    args = p.parse_args()

    dest_dir = os.path.abspath(os.path.expanduser(args.dest))
    if not args.dry_run:
        os.makedirs(dest_dir, exist_ok=True)
    failures = 0
    for src in args.srcs:
        src = os.path.abspath(os.path.expanduser(src))
        dst = os.path.join(dest_dir, os.path.basename(src.rstrip("/")))
        if not os.path.lexists(src):
            print("skip   %s (missing)" % src)
            continue
        if os.path.lexists(dst):
            print("REFUSE %s -> %s (destination exists)" % (src, dst))
            failures += 1
            continue
        how = "git worktree move" if os.path.isdir(src) and is_worktree(src) else "move"
        print("%-6s %s -> %s  [%s]" % ("would" if args.dry_run else "move", src, dst, how))
        if args.dry_run:
            continue
        if how == "git worktree move":
            r = subprocess.run(["git", "-C", src, "worktree", "move", src, dst],
                               capture_output=True, text=True)
            if r.returncode != 0:
                print("FAILED: %s" % r.stderr.strip())
                failures += 1
        else:
            shutil.move(src, dst)
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
