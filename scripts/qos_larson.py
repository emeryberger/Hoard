#!/usr/bin/env python3
"""
Larson QoS gate: compare Hoard's throughput against mimalloc and jemalloc.

Used both as a local benchmark and as a CI gate.

WHY A RATIO, NOT AN ABSOLUTE NUMBER
-----------------------------------
CI runners vary enormously in absolute speed (shared hardware, different
generations, noisy neighbours), so an absolute ops/sec threshold would either
flake constantly or be so loose it catches nothing. Every allocator is measured
in the SAME job on the SAME runner and Hoard is scored as a ratio to mimalloc,
which cancels most of that variation. The gate fails only if Hoard drops below
--min-ratio of mimalloc.

Runs are interleaved (round-robin over allocators, repeated) rather than
batched per allocator, so a runner that slows down partway through penalises
every allocator equally instead of whichever one happened to run last. Each
allocator's score is the MEDIAN of its runs, which ignores the occasional
outlier a shared runner will produce.

Interposition is VERIFIED, not assumed: a silently-not-preloaded library just
measures the system allocator, which would make the gate meaningless (and on
macOS, SIP strips DYLD_INSERT_LIBRARIES from system binaries -- see CLAUDE.md).

CALIBRATING --min-ratio
-----------------------
The floor is set from MEASURED runner variance, not guessed. Observed
hoard/mimalloc across repeated CI runs (report-only, reps=5):

    ubuntu-latest (4 cpu, x86_64):  1t 0.84-0.90    4t 0.94-0.97
    macos-latest  (3 cpu, arm64):   1t 0.93-1.03    4t 0.96-1.12

Linux is fairly tight. The macOS runners are shared and genuinely noisy: the
RATIO itself swings by ~0.1 between runs, with up to 35% min-max spread within
a single config. So the floor has to sit well below the worst observed value
(0.84), which is why it is 0.75 rather than something snug like 0.80 -- a gate
that flakes gets ignored, and an ignored gate is worse than none.

On top of that, --retry-on-fail re-measures from scratch before failing, so a
one-off noise dip has to happen twice independently to break the build. That is
what buys the gate its sensitivity: it reliably catches a real regression
(Hoard currently sits at ~0.9x mimalloc, so 0.75 trips on roughly a 17% drop)
without failing on runner noise.
"""

import argparse
import json
import os
import platform
import re
import statistics
import subprocess
import sys

IS_MAC = platform.system() == "Darwin"
PRELOAD_VAR = "DYLD_INSERT_LIBRARIES" if IS_MAC else "LD_PRELOAD"

THROUGHPUT_RE = re.compile(r"Throughput\s*=\s*([0-9]+)")


def run_larson(larson, args, lib):
    """One larson run under `lib` (None = system allocator). Returns ops/sec."""
    env = dict(os.environ)
    if lib:
        env[PRELOAD_VAR] = lib
    proc = subprocess.run([larson] + [str(a) for a in args],
                          env=env, capture_output=True, text=True, timeout=600)
    if proc.returncode != 0:
        raise RuntimeError(
            f"larson exited {proc.returncode} under {lib or 'system'}\n"
            f"stdout:\n{proc.stdout[-2000:]}\nstderr:\n{proc.stderr[-2000:]}")
    m = THROUGHPUT_RE.search(proc.stdout)
    if not m:
        raise RuntimeError(
            f"no Throughput line under {lib or 'system'}:\n{proc.stdout[-2000:]}")
    return int(m.group(1))


def verify_interposition(larson, lib, name):
    """Fail loudly if `lib` is not actually being loaded.

    A preload that silently does nothing measures the system allocator and
    would make every number here a lie.
    """
    env = dict(os.environ)
    env[PRELOAD_VAR] = lib
    if IS_MAC:
        env["DYLD_PRINT_LIBRARIES"] = "1"
    # A trivial run: 1 thread, tiny workload.
    proc = subprocess.run([larson, "1", "7", "8", "100", "1000", "1", "1"],
                          env=env, capture_output=True, text=True, timeout=300)
    err = proc.stderr
    if IS_MAC:
        # dyld prints the RESOLVED path, so libmimalloc.2.dylib (a symlink)
        # shows up as libmimalloc.2.1.dylib. Match the library stem.
        stem = re.match(r"(lib[a-zA-Z0-9_]+)", os.path.basename(lib))
        stem = stem.group(1) if stem else os.path.basename(lib)
        loaded = stem in err
    else:
        # glibc prints "cannot be preloaded" and carries on with the system
        # allocator; absence of that message means the preload took.
        loaded = "cannot be preloaded" not in err and proc.returncode == 0
    if not loaded:
        print(f"ERROR: {name} ({lib}) is NOT being interposed.", file=sys.stderr)
        print(f"stderr:\n{err[-2000:]}", file=sys.stderr)
        sys.exit(2)
    print(f"  interposition OK: {name}")


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--larson", required=True, help="path to the larson binary")
    p.add_argument("--hoard", required=True)
    p.add_argument("--mimalloc", required=True)
    p.add_argument("--jemalloc", default=None)
    p.add_argument("--reps", type=int, default=5,
                   help="runs per allocator per config (median is scored)")
    p.add_argument("--threads", default="1,4",
                   help="comma-separated thread counts to test")
    p.add_argument("--sleep", type=int, default=3, help="larson run seconds")
    p.add_argument("--min-ratio", type=float, default=0.0,
                   help="fail if hoard_median < min-ratio * mimalloc_median")
    p.add_argument("--retry-on-fail", action="store_true",
                   help="re-measure once before failing, to reject runner noise")
    p.add_argument("--report-only", action="store_true",
                   help="measure and report, never fail (used to calibrate)")
    p.add_argument("--json", default=None, help="write results here")
    args = p.parse_args()

    allocators = [("hoard", args.hoard), ("mimalloc", args.mimalloc)]
    if args.jemalloc:
        allocators.append(("jemalloc", args.jemalloc))

    print(f"Larson QoS ({platform.system()} {platform.machine()}, "
          f"{os.cpu_count()} cpus, {PRELOAD_VAR})")
    for name, lib in allocators:
        if not os.path.exists(lib):
            print(f"ERROR: {name} library not found: {lib}", file=sys.stderr)
            sys.exit(2)
        verify_interposition(args.larson, lib, name)
    print()

    def measure_and_report(label=""):
        """One full interleaved measurement pass. Returns (results, failures)."""
        results = {}
        for nthreads in [int(t) for t in args.threads.split(",")]:
            # mimalloc-bench canonical larson parameters.
            largs = [args.sleep, 7, 8, 1000, 10000, 1, nthreads]
            cfg = f"{nthreads}t"
            runs = {name: [] for name, _ in allocators}

            # Interleaved: rep-major, so runner drift hits all allocators equally.
            for _rep in range(args.reps):
                for name, lib in allocators:
                    runs[name].append(run_larson(args.larson, largs, lib))

            results[cfg] = {n: {"runs": r, "median": statistics.median(r)}
                            for n, r in runs.items()}

        print(f"{label}larson <sleep> 7 8 1000 10000 1 <threads>, "
              f"median of {args.reps} interleaved runs (Mops/sec)\n")
        hdr = f"{'config':>8}  " + "".join(f"{n:>12}" for n, _ in allocators) + \
              f"{'hoard/mi':>11}{'hoard/je':>11}"
        print(hdr)
        print("-" * len(hdr))

        failures = []
        for cfg, res in results.items():
            h = res["hoard"]["median"]
            mi = res["mimalloc"]["median"]
            je = res.get("jemalloc", {}).get("median")
            row = f"{cfg:>8}  " + "".join(
                f"{res[n]['median']/1e6:>12.1f}" for n, _ in allocators)
            r_mi = h / mi if mi else float("nan")
            row += f"{r_mi:>11.2f}"
            row += f"{h/je:>11.2f}" if je else f"{'-':>11}"
            print(row)
            if args.min_ratio and r_mi < args.min_ratio:
                failures.append((cfg, r_mi))

        # Spread, so a noisy runner is visible rather than silently shifting a median.
        print("\nspread (min-max as % of median):")
        for cfg, res in results.items():
            parts = []
            for name, _ in allocators:
                r = res[name]["runs"]
                med = res[name]["median"]
                parts.append(f"{name} {100*(max(r)-min(r))/med:.0f}%")
            print(f"  {cfg:>8}  " + "  ".join(parts))
        print()
        return results, failures

    results, failures = measure_and_report()

    # Confirm before failing: CI runners are noisy enough (see the calibration
    # note above) that a single dip below the floor is more likely to be a noisy
    # neighbour than a real regression. Re-measure from scratch and fail only if
    # it reproduces -- a flaky gate gets ignored, and an ignored gate is useless.
    if failures and args.retry_on_fail and not args.report_only:
        print(f"below the {args.min_ratio:.2f}x floor on "
              f"{', '.join(c for c, _ in failures)}; re-measuring to confirm "
              f"(this is noise-rejection, not a retry until green)...\n")
        results2, failures2 = measure_and_report(label="CONFIRMATION PASS: ")
        confirmed = {c for c, _ in failures} & {c for c, _ in failures2}
        if not confirmed:
            print("Did not reproduce: treating the first pass as runner noise.")
            failures = []
        else:
            failures = [f for f in failures2 if f[0] in confirmed]
            results = results2

    print()
    if args.report_only:
        print("report-only: not gating.")
        return 0
    if failures:
        for cfg, r in failures:
            print(f"FAIL: {cfg}: hoard is {r:.2f}x mimalloc, "
                  f"below the {args.min_ratio:.2f}x floor", file=sys.stderr)
        return 1
    if args.min_ratio:
        print(f"PASS: hoard >= {args.min_ratio:.2f}x mimalloc on all configs.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
