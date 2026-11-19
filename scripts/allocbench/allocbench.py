#!/usr/bin/env python3
"""allocbench: interleaved allocator benchmarking for Hoard.

Replaces the ad-hoc shell one-liners used during the Nov 2026 investigations
(zsh word-splitting, missing awk asort, pipefail + grep -m1, self-matching
pkill all bit those). Everything is driven by a JSON spec (see specs/).

Subcommands
  verify SPEC              check every allocator really serves malloc
  run SPEC [--out F]       interleaved, resumable runs -> JSONL results
  report SPEC [--out F]    medians, ranges, paired ratios, thread speedups
  frame LIB [SYMBOL...]    stack frame + instruction count of symbols
  remote push|start|status|wait|fetch HOST SPEC   run on another machine,
                           detached; wait polls until done, then fetches

Spec (JSON):
{
  "name": "larson-noinline",
  "reps": 9,
  "timeout": 300,
  "vars": {"HOARD": "/Users/me/git/Hoard"},            # ${VAR} expansion;
                                                        # ${WORK} is built in:
                                                        # $ALLOCBENCH_WORK or
                                                        # ~/hoard-bench
  "allocators": {
     "master": {"lib": "/tmp/a/libhoard.dylib"},
     "system": {"lib": null},                            # no preload
     "tcmalloc-percpu": {"lib": null, "static": true,    # linked in; use
        "bench_suffix": "-tcmalloc",                     #   other binaries
        "env": {"GLIBC_TUNABLES": "glibc.pthread.rseq=0"}}
  },
  "benchmarks": [
     {"name": "larson-1t", "metric": "larson", "group": "larson", "threads": 1,
      "cmd": ["${HOARD}/benchmarks/larson/larson", "10","7","8","1000","10000","1","1"]}
  ],
  "pairs": [["ni", "master"]]                            # paired ratios a/b
}
metric: "larson" (Throughput = N, higher better), "elapsed" (N seconds,
lower better), "average" (Average execution time = N), "wall" (wall time).
Optional per-benchmark "verify_cmd": a short run used only by verify.
Optional per-allocator "probe": {"cmd": [...], "expect": "regex"} -- verify
runs it with the allocator's env and requires the regex (use it for
statically linked allocators, which LD_DEBUG/DYLD checks cannot see).
"bench_suffix" is appended to cmd[0] for that allocator (statically linked
builds). Benchmarks sharing a "group" with different "threads" get a
speedup table (t1 / tN for time metrics, tN / t1 for throughput).
"""

import argparse
import json
import os
import platform
import random
import re
import shlex
import statistics
import subprocess
import sys
import tempfile
import time
import zlib

IS_MAC = platform.system() == "Darwin"
PRELOAD_VAR = "DYLD_INSERT_LIBRARIES" if IS_MAC else "LD_PRELOAD"

METRICS = {
    # name: (regex, higher_is_better)
    "larson": (r"Throughput\s*=\s*([0-9.]+)", True),
    "elapsed": (r"([0-9]+\.[0-9]+)\s*seconds", False),
    "average": (r"Average execution time\s*=\s*([0-9.]+)", False),
    "wall": (None, False),
}


# ----------------------------------------------------------------- spec --

def expand(value, variables):
    if isinstance(value, str):
        return re.sub(r"\$\{(\w+)\}",
                      lambda m: variables.get(m.group(1), os.environ.get(m.group(1), m.group(0))),
                      value)
    if isinstance(value, list):
        return [expand(v, variables) for v in value]
    if isinstance(value, dict):
        return {k: expand(v, variables) for k, v in value.items()}
    return value


def load_spec(path):
    with open(path) as f:
        spec = json.load(f)
    # ${WORK}: where builds and benchmark artifacts live on THIS machine.
    # Deliberately not /tmp (tmpfs on Linux: wiped on reboot).
    variables = {"WORK": os.environ.get("ALLOCBENCH_WORK", os.path.expanduser("~/hoard-bench"))}
    # Resolve spec vars in order, so a var may use ${WORK} or earlier vars.
    for k, v in spec.get("vars", {}).items():
        variables[k] = str(expand(v, variables))
    spec = expand(spec, variables)
    assert isinstance(spec, dict), "spec must be a JSON object"
    spec.setdefault("reps", 5)
    spec.setdefault("timeout", 300)
    spec.setdefault("pairs", [])
    for name, a in spec["allocators"].items():
        a.setdefault("lib", None)
        a.setdefault("env", {})
        a.setdefault("static", False)
        a.setdefault("bench_suffix", "")
    for b in spec["benchmarks"]:
        if b["metric"] not in METRICS:
            sys.exit("unknown metric %r in benchmark %s" % (b["metric"], b["name"]))
    spec["_path"] = os.path.abspath(path)
    return spec


def default_out(spec):
    base = os.path.splitext(spec["_path"])[0]
    return "%s.%s.results.jsonl" % (base, platform.node().split(".")[0])


def command_for(bench, alloc):
    cmd = list(bench["cmd"])
    cmd[0] = cmd[0] + alloc["bench_suffix"]
    return cmd


def env_for(alloc):
    env = dict(os.environ)
    env.pop("LD_PRELOAD", None)
    env.pop("DYLD_INSERT_LIBRARIES", None)
    if alloc["lib"]:
        env[PRELOAD_VAR] = alloc["lib"]
    env.update(alloc["env"])
    return env


# ---------------------------------------------------------------- verify --

ZONE_PROBE_C = r"""
#include <stdio.h>
#include <stdlib.h>
#include <malloc/malloc.h>
int main(void) { void *p = malloc(16); malloc_zone_t *z = malloc_zone_from_ptr(p);
  printf("%s\n", z && z->zone_name ? z->zone_name : "(no zone)"); return 0; }
"""


def mac_zone_probe(tmpdir):
    src = os.path.join(tmpdir, "zoneprobe.c")
    exe = os.path.join(tmpdir, "zoneprobe")
    with open(src, "w") as f:
        f.write(ZONE_PROBE_C)
    subprocess.run(["cc", "-O0", "-o", exe, src], check=True)
    return exe


def verify(spec):
    """Each allocator must actually serve malloc for the benchmark binary."""
    ok = True
    bench = spec["benchmarks"][0]
    with tempfile.TemporaryDirectory() as tmp:
        probe = mac_zone_probe(tmp) if IS_MAC else None
        for name, a in spec["allocators"].items():
            cmd = command_for(bench, a)
            if not os.path.exists(cmd[0]):
                print("FAIL %-16s benchmark binary missing: %s" % (name, cmd[0]))
                ok = False
                continue
            if a["lib"] and not os.path.exists(a["lib"]):
                print("FAIL %-16s library missing: %s" % (name, a["lib"]))
                ok = False
                continue
            if "probe" in a:
                # e.g. statically linked TCMalloc: run its probe under the
                # allocator's env and require the expected output.
                r = subprocess.run(a["probe"]["cmd"], env=env_for(a), capture_output=True,
                                   text=True, timeout=spec["timeout"])
                # re.S: probes print several lines; `.*` must span them.
                good = re.search(a["probe"]["expect"], r.stdout + r.stderr, re.S) is not None
                print("%s %-16s probe expects /%s/: %s" % (
                    "ok  " if good else "FAIL", name, a["probe"]["expect"],
                    " ".join((r.stdout + r.stderr).split())[:120]))
                ok &= good
                continue
            if a["static"] or not a["lib"]:
                print("ok   %-16s %s (no preload, no probe given)" % (
                    name, "static" if a["static"] else "system"))
                continue
            env = env_for(a)
            if IS_MAC:
                assert probe is not None
                env["DYLD_PRINT_LIBRARIES"] = "1"
                r = subprocess.run([probe], env=env, capture_output=True, text=True)
                loaded = os.path.realpath(a["lib"]) in r.stderr or a["lib"] in r.stderr
                zone = r.stdout.strip()
                status = "ok  " if loaded else "FAIL"
                print("%s %-16s loaded=%s zone=%s" % (status, name, loaded, zone))
                ok &= loaded
            else:
                env["LD_DEBUG"] = "bindings"
                # Only the binding matters: use the benchmark's short
                # "verify_cmd" when given, else the full command.
                vcmd = list(bench.get("verify_cmd", bench["cmd"]))
                vcmd[0] = vcmd[0] + a["bench_suffix"]
                r = subprocess.run(vcmd, env=env, capture_output=True, text=True,
                                   timeout=spec["timeout"])
                m = re.search(r"to (\S+) \[\d+\]: normal symbol `malloc'", r.stderr)
                who = m.group(1) if m else "?"
                good = os.path.realpath(who) == os.path.realpath(a["lib"])
                print("%s %-16s malloc bound to %s" % ("ok  " if good else "FAIL", name, who))
                ok &= good
    return ok


# ------------------------------------------------------------------- run --

def measure(bench, alloc, timeout):
    cmd = command_for(bench, alloc)
    regex, _ = METRICS[bench["metric"]]
    start = time.monotonic()
    try:
        r = subprocess.run(cmd, env=env_for(alloc), capture_output=True, text=True,
                           timeout=timeout)
    except subprocess.TimeoutExpired:
        return None, "timeout"
    wall = time.monotonic() - start
    if r.returncode != 0:
        return None, "exit %d: %s" % (r.returncode, (r.stderr or r.stdout).strip()[-200:])
    if regex is None:
        return wall, None
    m = re.search(regex, r.stdout + r.stderr)
    if not m:
        return None, "no metric in output: %s" % (r.stdout.strip()[-200:])
    return float(m.group(1)), None


def load_results(path):
    rows = []
    if os.path.exists(path):
        with open(path) as f:
            rows = [json.loads(line) for line in f if line.strip()]
    return rows


def run(spec, out):
    done = {(r["rep"], r["bench"], r["alloc"]) for r in load_results(out) if r["value"] is not None}
    names = list(spec["allocators"])
    total = spec["reps"] * len(spec["benchmarks"]) * len(names)
    n = len(done)
    with open(out, "a") as f:
        for rep in range(1, spec["reps"] + 1):
            for bench in spec["benchmarks"]:
                # Shuffle per (rep, bench) to avoid position bias; deterministic
                # (crc32, not hash(), which is randomized per process) so a
                # resumed run continues the same schedule.
                order = names[:]
                random.Random(rep * 7919 + zlib.crc32(bench["name"].encode())).shuffle(order)
                for name in order:
                    if (rep, bench["name"], name) in done:
                        continue
                    value, err = measure(bench, spec["allocators"][name], spec["timeout"])
                    row = {"rep": rep, "bench": bench["name"], "alloc": name,
                           "value": value, "error": err, "time": time.time()}
                    f.write(json.dumps(row) + "\n")
                    f.flush()
                    n += 1
                    print("[%d/%d] rep %d %-20s %-16s %s" % (
                        n, total, rep, bench["name"], name,
                        value if err is None else "ERROR " + err), flush=True)
    print("DONE")


# ---------------------------------------------------------------- report --

def report(spec, out):
    rows = [r for r in load_results(out) if r["value"] is not None]
    errors = [r for r in load_results(out) if r["value"] is None]
    if not rows:
        sys.exit("no results in %s" % out)
    vals = {}
    for r in rows:
        vals.setdefault((r["bench"], r["alloc"]), {})[r["rep"]] = r["value"]
    names = list(spec["allocators"])
    print("# %s  (%s)" % (spec["name"], out))
    for bench in spec["benchmarks"]:
        _, higher = METRICS[bench["metric"]]
        print("\n## %s  [%s, %s is better]" % (
            bench["name"], bench["metric"], "higher" if higher else "lower"))
        print("  %-18s %4s %12s %12s %12s %7s" % ("allocator", "n", "median", "min", "max", "spread"))
        for name in names:
            v = list(vals.get((bench["name"], name), {}).values())
            if not v:
                continue
            med = statistics.median(v)
            print("  %-18s %4d %12.4g %12.4g %12.4g %6.0f%%" % (
                name, len(v), med, min(v), max(v), 100 * (max(v) - min(v)) / med))
        for a, b in spec["pairs"]:
            va = vals.get((bench["name"], a), {})
            vb = vals.get((bench["name"], b), {})
            common = sorted(set(va) & set(vb))
            if not common:
                continue
            # ratio > 1 always means "a is better than b"
            ratios = [(va[k] / vb[k]) if higher else (vb[k] / va[k]) for k in common]
            wins = sum(1 for x in ratios if x > 1)
            print("  paired %s vs %s: median %.3fx better  (a better in %d/%d reps)" % (
                a, b, statistics.median(ratios), wins, len(ratios)))
    # thread-scaling speedups per group
    groups = {}
    for b in spec["benchmarks"]:
        if "group" in b and "threads" in b:
            groups.setdefault(b["group"], []).append(b)
    for g, benches in groups.items():
        benches.sort(key=lambda b: b["threads"])
        base = benches[0]
        if len(benches) < 2:
            continue
        _, higher = METRICS[base["metric"]]
        print("\n## speedup: %s (relative to %d thread(s); ideal = thread ratio)" % (g, base["threads"]))
        for name in names:
            b0 = list(vals.get((base["name"], name), {}).values())
            if not b0:
                continue
            m0 = statistics.median(b0)
            cells = []
            for b in benches[1:]:
                v = list(vals.get((b["name"], name), {}).values())
                if v:
                    m = statistics.median(v)
                    cells.append("%dt %.2fx" % (b["threads"], (m / m0) if higher else (m0 / m)))
            print("  %-18s %s" % (name, "  ".join(cells)))
    if errors:
        print("\n%d failed runs (excluded):" % len(errors))
        for r in errors[:20]:
            print("  rep %d %s %s: %s" % (r["rep"], r["bench"], r["alloc"], r["error"]))


# ----------------------------------------------------------------- frame --

def frame(lib, symbols, show=False):
    """Report stack-frame setup and size of exported entry points. A function
    ends at the next symbol label (including C++ mangled `__Z...` labels)."""
    for sym in symbols:
        if IS_MAC:
            text = subprocess.run(["otool", "-tV", lib], capture_output=True, text=True).stdout
            label = "_" + sym + ":"
            lines, grab = [], False
            for line in text.splitlines():
                if line.strip() == label:
                    grab = True
                    continue
                if grab and re.match(r"^_\w.*:$", line.strip()):
                    break
                if grab:
                    lines.append(line)
            setup = [l.strip() for l in lines if re.search(r"sub\s+sp, sp|\[sp, #-", l)][:3]
        else:
            text = subprocess.run(["objdump", "-d", "--no-show-raw-insn", lib],
                                  capture_output=True, text=True).stdout
            lines, grab = [], False
            for line in text.splitlines():
                if line.endswith("<%s>:" % sym):
                    grab = True
                    continue
                if grab and not line.strip():
                    break
                if grab:
                    lines.append(line)
            setup = [l.split(":", 1)[1].strip() for l in lines
                     if re.search(r"sub\s+\$0x[0-9a-f]+,%rsp", l)][:1]
        print("%-16s %5d insns  frame: %s" % (sym, len(lines), "; ".join(setup) or "(none)"))
        if show:
            for line in lines:
                print("    " + line.strip())


# ---------------------------------------------------------------- remote --

# Relative to the remote home directory (ssh and rsync start there), so it
# works whatever the remote $HOME is; persistent, unlike /tmp.
REMOTE_DIR = "hoard-bench/allocbench"


def ssh(host, *args, check=True, shell_tail=""):
    """Run a command on host. ssh joins its arguments into ONE remote shell
    command line, so each argument is quoted to arrive intact; shell_tail is
    appended unquoted for deliberate shell syntax (redirections, &)."""
    remote_cmd = " ".join(shlex.quote(a) for a in args)
    if shell_tail:
        remote_cmd += " " + shell_tail
    return subprocess.run(["ssh", "-o", "BatchMode=yes", host, remote_cmd],
                          capture_output=True, text=True, check=check)


def remote(action, host, spec_path):
    here = os.path.dirname(os.path.abspath(__file__))
    spec_name = os.path.basename(spec_path)
    rspec = "%s/specs/%s" % (REMOTE_DIR, spec_name)
    rout = "%s/specs/%s.results.jsonl" % (REMOTE_DIR, os.path.splitext(spec_name)[0])
    rlog = rout + ".log"
    if action == "push":
        ssh(host, "mkdir", "-p", REMOTE_DIR + "/specs")
        subprocess.run(["rsync", "-a", os.path.join(here, "allocbench.py"),
                        "%s:%s/" % (host, REMOTE_DIR)], check=True)
        subprocess.run(["rsync", "-a", spec_path, "%s:%s/specs/" % (host, REMOTE_DIR)], check=True)
        print("pushed to %s:%s" % (host, REMOTE_DIR))
    elif action == "start":
        remote("push", host, spec_path)
        v = ssh(host, "python3", REMOTE_DIR + "/allocbench.py", "verify", rspec, check=False)
        print(v.stdout.strip())
        if v.returncode != 0:
            sys.exit("verify failed on %s; not starting" % host)
        # nohup + setsid: survives the ssh session ending (WSSH drops happen).
        # -u: unbuffered, so the log shows progress immediately.
        ssh(host, "setsid", "nohup", "python3", "-u", REMOTE_DIR + "/allocbench.py", "run", rspec,
            "--out", rout,
            shell_tail="> %s 2>&1 < /dev/null &" % shlex.quote(rlog))
        print("started on %s; log %s" % (host, rlog))
    elif action == "status":
        rows = ssh(host, "wc", "-l", rout, check=False).stdout.split()
        running = ssh(host, "pgrep", "-f", "allocbench.py run " + rspec, check=False).returncode == 0
        print("%s: %s result rows, %s" % (host, rows[0] if rows else 0,
                                         "running" if running else "not running"))
        r = ssh(host, "tail", "-n", "3", rlog, check=False)
        if r.stdout.strip():
            print(r.stdout.strip())
    elif action == "wait":
        # Poll until the remote run has exited, then fetch its results.
        # A failed poll (e.g. a dropped connection) is retried, not fatal.
        while True:
            r = ssh(host, "pgrep", "-f", "allocbench.py run " + rspec, check=False)
            if r.returncode == 1:          # pgrep: no process matched
                break
            time.sleep(60)
        return remote("fetch", host, spec_path)
    elif action == "fetch":
        local = default_out(load_spec(spec_path)).replace(
            ".%s." % platform.node().split(".")[0], ".%s." % host)
        subprocess.run(["rsync", "-a", "%s:%s" % (host, rout), local], check=True)
        print(local)
        return local


# ------------------------------------------------------------------ main --

def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="cmd", required=True)
    for c in ("verify", "run", "report"):
        s = sub.add_parser(c)
        s.add_argument("spec")
        s.add_argument("--out")
    s = sub.add_parser("frame")
    s.add_argument("lib")
    s.add_argument("symbols", nargs="*", default=["malloc", "free"])
    s.add_argument("--show", action="store_true", help="print the instructions")
    s = sub.add_parser("remote")
    s.add_argument("action", choices=["push", "start", "status", "wait", "fetch"])
    s.add_argument("host")
    s.add_argument("spec")
    args = p.parse_args()

    if args.cmd == "frame":
        frame(args.lib, args.symbols, args.show)
        return
    if args.cmd == "remote":
        remote(args.action, args.host, args.spec)
        return
    spec = load_spec(args.spec)
    out = args.out or default_out(spec)
    if args.cmd == "verify":
        sys.exit(0 if verify(spec) else 1)
    if args.cmd == "run":
        if not verify(spec):
            sys.exit("verify failed; not running")
        run(spec, out)
    elif args.cmd == "report":
        report(spec, out)


if __name__ == "__main__":
    main()
