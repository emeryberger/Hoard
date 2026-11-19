#!/usr/bin/env python3
"""Build Hoard at given git refs, side by side, for allocbench comparisons.

    build_hoard.py NAME=REF [NAME=REF ...] [--repo URL] [--jobs N]

REF: a branch (`master`), a commit, or `pr:N` for GitHub pull request N's
head. Each build goes to ${WORK}/builds/NAME (a git worktree of a shared bare
clone at ${WORK}/src/Hoard.git); ${WORK} is $ALLOCBENCH_WORK or ~/hoard-bench.
Builds the library (Release) and benchmarks/larson, then reports the Hoard
commit, the Heap-Layers and alloc8 commits CMake fetched, and the stack frame
of the malloc/free entry points (a cheap check that NO_INLINE is honoured).
"""

import argparse
import os
import platform
import subprocess
import sys

IS_MAC = platform.system() == "Darwin"
LIB = "libhoard.dylib" if IS_MAC else "libhoard.so"
ENTRY = ["replace_malloc", "replace_free"] if IS_MAC else ["malloc", "free"]


def sh(*cmd, cwd=None, log=None):
    if log:
        with open(log, "w") as f:
            r = subprocess.run(cmd, cwd=cwd, stdout=f, stderr=subprocess.STDOUT)
        if r.returncode != 0:
            with open(log) as f:
                tail = f.read()[-3000:]
            sys.exit("FAILED: %s\n(see %s)\n%s" % (" ".join(cmd), log, tail))
        return ""
    r = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True)
    if r.returncode != 0:
        sys.exit("FAILED: %s\n%s" % (" ".join(cmd), r.stderr.strip()))
    return r.stdout.strip()


def git_head(path):
    if not os.path.isdir(path):
        return "(missing)"
    r = subprocess.run(["git", "-C", path, "log", "-1", "--format=%h %cs %s"],
                       capture_output=True, text=True)
    return r.stdout.strip() or "(not a git checkout)"


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("builds", nargs="+", metavar="NAME=REF")
    p.add_argument("--repo", default="https://github.com/emeryberger/Hoard.git")
    p.add_argument("--jobs", type=int, default=os.cpu_count() or 8)
    args = p.parse_args()

    work = os.environ.get("ALLOCBENCH_WORK", os.path.expanduser("~/hoard-bench"))
    bare = os.path.join(work, "src", "Hoard.git")
    if not os.path.isdir(bare):
        os.makedirs(os.path.dirname(bare), exist_ok=True)
        sh("git", "clone", "-q", "--bare", args.repo, bare)

    specs = []
    for b in args.builds:
        if "=" not in b:
            sys.exit("expected NAME=REF, got %r" % b)
        name, ref = b.split("=", 1)
        specs.append((name, ref))

    # Fetch every branch plus the requested PR heads, refreshing local refs.
    refspecs = ["+refs/heads/*:refs/heads/*"]
    for _, ref in specs:
        if ref.startswith("pr:"):
            n = ref[3:]
            refspecs.append("+refs/pull/%s/head:refs/pull/%s/head" % (n, n))
    sh("git", "-C", bare, "fetch", "-q", "--prune", args.repo, *refspecs)

    here = os.path.dirname(os.path.abspath(__file__))
    for name, ref in specs:
        gitref = "refs/pull/%s/head" % ref[3:] if ref.startswith("pr:") else ref
        sha = sh("git", "-C", bare, "rev-parse", "--verify", gitref + "^{commit}")
        dest = os.path.join(work, "builds", name)
        if os.path.isdir(dest):
            sh("git", "-C", dest, "checkout", "-q", "--detach", sha)
        else:
            sh("git", "-C", bare, "worktree", "add", "-q", "--detach", dest, sha)
        build = os.path.join(dest, "build")
        print("== %s (%s) -> %s" % (name, ref, dest), flush=True)
        # A fresh configure each time so FetchContent re-resolves Heap-Layers
        # and alloc8 (both track their default branches).
        sh("rm", "-rf", build)
        sh("cmake", "-S", dest, "-B", build, "-DCMAKE_BUILD_TYPE=Release",
           log=os.path.join(dest, "cmake.log"))
        sh("cmake", "--build", build, "-j", str(args.jobs), log=os.path.join(dest, "build.log"))
        # Only the plain `larson` target: allocbench supplies the allocator,
        # and the default target also links a `larson-hoard` against
        # ../../src, which a CMake build does not produce.
        sh("make", "-C", os.path.join(dest, "benchmarks", "larson"), "larson",
           log=os.path.join(dest, "larson-build.log"))
        print("   hoard:       %s" % git_head(dest))
        print("   heap-layers: %s" % git_head(os.path.join(build, "_deps", "heap-layers-src")))
        print("   alloc8:      %s" % git_head(os.path.join(build, "_deps", "alloc8-src")))
        print("   lib:         %s" % os.path.join(build, LIB))
        frames = subprocess.run([sys.executable, os.path.join(here, "allocbench.py"), "frame",
                                 os.path.join(build, LIB)] + ENTRY,
                                capture_output=True, text=True).stdout
        for line in frames.splitlines():
            print("   " + line)


if __name__ == "__main__":
    main()
