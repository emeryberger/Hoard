# allocbench

Interleaved, verified allocator benchmarking for Hoard (macOS and Linux).
Full reference: `python3 allocbench.py --help`.

```sh
python3 allocbench.py verify specs/mac-noinline.json   # each allocator really serves malloc?
python3 allocbench.py run    specs/mac-noinline.json   # interleaved, resumable -> *.results.jsonl
python3 allocbench.py report specs/mac-noinline.json   # medians, paired ratios, speedups

# On another machine (detached; survives a dropped ssh session):
python3 allocbench.py remote start cloudnew specs/linux-larson-vs-mimalloc.json
python3 allocbench.py remote status cloudnew specs/linux-larson-vs-mimalloc.json
python3 allocbench.py remote wait   cloudnew specs/linux-larson-vs-mimalloc.json   # then fetches
python3 allocbench.py report specs/linux-larson-vs-mimalloc.json \
    --out specs/linux-larson-vs-mimalloc.cloudnew.results.jsonl

python3 allocbench.py frame build/libhoard.so malloc free   # stack frame / size of entry points

# Build Hoard variants side by side (git refs, incl. PR heads) into ${WORK}/builds/NAME,
# reporting the Heap-Layers/alloc8 commits fetched and the malloc/free frames:
python3 build_hoard.py master=master mybranch=pr:123
ssh host python3 hoard-bench/allocbench/build_hoard.py master=master   # `remote push` ships it
```

Specs can use `${WORK}` and `${LIBNAME}` (`libhoard.dylib` / `libhoard.so`), so
one spec runs unchanged on macOS and Linux.

Small helpers:

```sh
# Block until a PR's checks finish; exit 0 only if all passed. --min-checks
# stops a PR whose checks haven't appeared yet from looking "all green".
python3 wait_pr_checks.py emeryberger/Hoard 123 --min-checks 5

# Before deleting local copies of files that were merged elsewhere: which are
# byte-identical to a ref, which hold edits the ref lacks (exit 1), which are new.
python3 compare_with_ref.py origin/master benchmarks/some-dir scripts/foo.py
```

Why each piece exists (all were real failures):

- **verify before timing.** macOS SIP and hardened runtimes silently drop
  `DYLD_INSERT_LIBRARIES`; a preload can load without serving `malloc`. Linux
  uses `LD_DEBUG=bindings`; macOS checks the library loaded and reports the
  malloc zone. Statically linked allocators declare a `probe` (e.g. Google
  TCMalloc's per-CPU mode, which glibc's rseq registration silently disables).
- **interleaved and shuffled.** Machine state drifts (thermal, scheduler,
  efficiency cores). Every round runs every allocator, in a shuffled but
  deterministic order, and pairs are compared round by round.
- **resumable.** Results append to JSONL; `run` skips finished rows.
- **paths under `${WORK}`** (`$ALLOCBENCH_WORK`, default `~/hoard-bench`), not
  `/tmp`, which is tmpfs on many Linux hosts. `relocate.py` moves existing
  artifacts (including git worktrees) out of `/tmp`.
- **`frame`.** Found Heap-Layers redefining `NO_INLINE` to nothing: Hoard's
  `malloc` had a 712-byte stack frame on Linux (24 bytes once fixed).

Specs in `specs/` reproduce the November 2026 measurements; their paths
assume the `${WORK}` layout used then and need adjusting for other machines.
