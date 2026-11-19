#!/usr/bin/env python3
"""Wait until a GitHub PR's checks have all finished, then print them.

    wait_pr_checks.py REPO PR [--min-checks N] [--interval S] [--timeout S]

Exit 0 if every check passed (or was skipped), 1 if any failed or was
cancelled, 2 on timeout. Waits for at least --min-checks checks to exist
first, since checks for a just-pushed commit appear with a delay (and a PR
with no checks yet would otherwise look "all green").
"""

import argparse
import json
import subprocess
import sys
import time


def checks(repo, pr):
    r = subprocess.run(["gh", "pr", "checks", str(pr), "-R", repo, "--json", "name,bucket,link"],
                       capture_output=True, text=True)
    if r.returncode not in (0, 1, 8):   # gh exits 1 when checks failed, 8 while pending
        return None
    try:
        return json.loads(r.stdout or "[]")
    except json.JSONDecodeError:
        return None


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("repo")
    p.add_argument("pr", type=int)
    p.add_argument("--min-checks", type=int, default=1)
    p.add_argument("--interval", type=int, default=30)
    p.add_argument("--timeout", type=int, default=3600)
    args = p.parse_args()

    deadline = time.time() + args.timeout
    while time.time() < deadline:
        cs = checks(args.repo, args.pr)
        if cs is not None and len(cs) >= args.min_checks and all(c["bucket"] != "pending" for c in cs):
            bad = [c for c in cs if c["bucket"] in ("fail", "cancel")]
            for c in cs:
                print("%-30s %s" % (c["name"], c["bucket"]))
            print("ALL PASSED" if not bad else "FAILED: %d check(s)" % len(bad))
            sys.exit(1 if bad else 0)
        time.sleep(args.interval)
    print("TIMEOUT waiting for checks on %s#%d" % (args.repo, args.pr))
    sys.exit(2)


if __name__ == "__main__":
    main()
