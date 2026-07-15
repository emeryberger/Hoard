#!/usr/bin/env python3
"""
Windows QoS gate: Hoard vs the Windows system heap, across the CI benchmarks.

WHY THIS IS SHAPED DIFFERENTLY FROM qos_larson.py
-------------------------------------------------
On Linux/macOS the gate compares Hoard to mimalloc, run interleaved in one
process each. Neither is possible on Windows here:

  * The comparison allocators (mimalloc/jemalloc) are not built on Windows --
    allocators/CMakeLists.txt is Unix-only. The only other allocator present is
    the Windows system heap, which the benchmarks already measure as their
    "baseline" (no injection) vs "hoard" (injected via withdll.exe).

  * The benchmarks run under DLL injection and are launched one at a time by a
    watchdog, so there is no clean interleaving to do. Instead each writes its
    result line to a file (BENCH_OUT; see benchmarks/ci/bench_common.h), and we
    compare the two files after the fact.

That makes hoard-vs-system the natural Windows comparison -- and a meaningful
one: Hoard is a drop-in replacement for exactly that heap, so "am I at least as
fast as what I replace?" is the question that matters most on this platform.

WHY GEOMEAN, NOT PER-BENCHMARK
------------------------------
Each benchmark runs ONCE per CI job, so a single ratio is noisy (larson in
particular swings, being false-sharing sensitive). The geometric mean over the
whole suite is far steadier than any single benchmark, so we gate on that and
merely REPORT the per-benchmark ratios. A real regression drags the whole suite
down; one noisy benchmark does not move the geomean much.
"""

import argparse
import math
import re
import sys

# "name: ...stuff... (12345 ops/sec)"  -- benchmarks that report a rate.
RATE_RE = re.compile(r'^(\w[\w-]*):\s.*\(([0-9]+)\s*ops/sec\)')


def _read_text(path):
    """Read a results file regardless of its encoding.

    The two files come from different producers: the Hoard results file is
    written by the benchmark itself (plain ASCII via fprintf), while the
    baseline file is written by PowerShell's Tee-Object, which defaults to
    UTF-16 on Windows. Decode whichever it is; a byte that is not valid in the
    chosen encoding is replaced rather than fatal.
    """
    data = open(path, "rb").read()
    if data[:2] in (b"\xff\xfe", b"\xfe\xff"):      # UTF-16 BOM
        return data.decode("utf-16", errors="replace")
    if b"\x00" in data[:200]:                          # UTF-16 without BOM
        return data.decode("utf-16-le", errors="replace")
    return data.decode("utf-8-sig", errors="replace")   # UTF-8 (BOM tolerated)


def parse(path):
    """Return {benchmark_name: ops_per_sec} from a results file."""
    out = {}
    for line in _read_text(path).splitlines():
        m = RATE_RE.match(line.strip())
        if m and m.group(1) not in out:   # first occurrence wins
            out[m.group(1)] = int(m.group(2))
    return out


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--baseline", required=True, help="system-heap results file")
    p.add_argument("--hoard", required=True, help="Hoard results file")
    p.add_argument("--min-ratio", type=float, default=0.0,
                   help="fail if geomean(hoard/system) < this")
    p.add_argument("--report-only", action="store_true")
    args = p.parse_args()

    base = parse(args.baseline)
    hoard = parse(args.hoard)

    common = sorted(set(base) & set(hoard))
    if not common:
        print("ERROR: no benchmarks in common between the two result files.",
              file=sys.stderr)
        print(f"  baseline: {sorted(base)}", file=sys.stderr)
        print(f"  hoard:    {sorted(hoard)}", file=sys.stderr)
        return 2

    print("Windows QoS: Hoard vs the system heap "
          "(ops/sec, HIGHER IS BETTER)\n")
    hdr = f"{'benchmark':<15}{'system':>15}{'hoard':>15}   {'hoard/system':>14}"
    print(hdr)
    print("-" * len(hdr))

    ratios = []
    for n in common:
        r = hoard[n] / base[n]
        ratios.append(r)
        tag = ("" if 0.97 <= r <= 1.03
               else ("  faster" if r > 1 else "  SLOWER"))
        print(f"{n:<15}{base[n]:>15,}{hoard[n]:>15,}   {r:>13.2f}x{tag}")

    geomean = math.exp(sum(math.log(r) for r in ratios) / len(ratios))
    print("-" * len(hdr))
    print(f"{'geomean':<15}{'':>15}{'':>15}   {geomean:>13.2f}x  "
          f"over {len(ratios)} benchmarks\n")

    if args.report_only or not args.min_ratio:
        if args.min_ratio and geomean < args.min_ratio:
            # Surface it loudly even when not gating, so a real regression on a
            # report-only platform still reaches a human.
            print(f"::warning::Windows geomean {geomean:.2f}x is below the "
                  f"{args.min_ratio:.2f}x floor -- Hoard has lost ground against "
                  f"the system heap. Report-only, so not failing the build.")
        print("report-only: not gating." if args.report_only
              else "no floor set: not gating.")
        return 0

    if geomean < args.min_ratio:
        print(f"FAIL: Hoard is {geomean:.2f}x the system heap (geomean), "
              f"below the {args.min_ratio:.2f}x floor.", file=sys.stderr)
        return 1
    print(f"PASS: Hoard is {geomean:.2f}x the system heap (geomean), "
          f">= {args.min_ratio:.2f}x floor.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
