# Benchmarking Google's TCMalloc against Hoard's false-sharing tests

How to build and run Hoard's false-sharing benchmarks (`cache-scratch`,
`cache-thrash`, `ci/cache_scratch.c`) against **Google's TCMalloc**
(<https://github.com/google/tcmalloc>), and the pitfalls hit doing it. This is
not gperftools' tcmalloc (`libtcmalloc.so` from distro packages), which is an
older, separate code base.

Validated 2026-11-18 on Amazon Linux 2023 (kernel 6.12, glibc 2.34, x86_64,
AMD EPYC 9R14, 192 cores).

## Quick start

```sh
benchmarks/google-tcmalloc/build.sh     # into $WORK (default /tmp/google-tcmalloc). First run downloads
                                        # LLVM (1.2 GB, 12 GB unpacked) and Bazel deps (~0.6 GB); reruns are cached
benchmarks/google-tcmalloc/run.sh       # TCMalloc (both modes) + glibc on the same binaries

# Compare other allocators on the SAME binaries (required, see below).
# Use absolute paths: the binding check compares them with LD_DEBUG's output.
PRELOADS="hoard=$PWD/build/libhoard.so jemalloc=/usr/lib64/libjemalloc.so.2" \
  benchmarks/google-tcmalloc/run.sh
```

## Method

1. **Link TCMalloc the supported way.** TCMalloc does not support
   `LD_PRELOAD` or dynamic loading (`docs/compatibility.md`: "Do not rely on
   dynamic loading/unloading"); its documented use is Bazel's `cc_binary`
   `malloc = "@tcmalloc//tcmalloc"` attribute (`docs/quickstart.md`).
   `build.sh` therefore checks out TCMalloc at a pinned commit, copies the
   benchmark sources into a package inside TCMalloc's workspace
   (`hoard_bench/`), and builds each benchmark twice: statically linked with
   TCMalloc, and with Bazel's default allocator (glibc) as a control.
2. **Pin everything.** Bazelisk (checksum-verified) launches a pinned Bazel 9;
   TCMalloc and the compiler are pinned too (see `build.sh` for versions and
   SHA-256s). TCMalloc's own `.bazelrc` (its CI flags) is used as is.
3. **Confirm the allocator and its mode.** `probe.cc`, linked the same way,
   checks that `malloc` is really TCMalloc's (`MallocExtension::GetOwnership`)
   and whether per-CPU caches are active (`PerCpuCachesActive()`).
4. **Measure every allocator on identical code.** Other allocators are
   `LD_PRELOAD`ed onto the `-default` binaries, the same compiled code that
   TCMalloc is statically linked into, each one checked with
   `LD_DEBUG=bindings` to really serve `malloc` before it is timed.

## Enabling TCMalloc's per-CPU mode

Per-CPU caches are TCMalloc's default and design point, but they need the
thread's rseq area, and a thread can register only one. If glibc has already
registered its own (upstream glibc 2.35+, and backports such as Amazon Linux
2023's 2.34), TCMalloc's registration fails and it **silently** falls back to
per-thread caches. To enable per-CPU mode, stop glibc from registering:

```sh
GLIBC_TUNABLES=glibc.pthread.rseq=0 ./your-tcmalloc-binary
```

No TCMalloc setting or rebuild is involved. Verify the mode rather than
assuming it:

```sh
$ $WORK/out/probe                                   # MallocExtension::PerCpuCachesActive()
probe: per_cpu_caches_active=NO ...
$ GLIBC_TUNABLES=glibc.pthread.rseq=0 $WORK/out/probe
probe: per_cpu_caches_active=yes ...

$ strace -f -e trace=rseq $WORK/out/probe           # without the tunable:
rseq(..., 0x20, 0, 0x53053053) = 0                  #   glibc registers (main + each thread)
rseq(..., 0x20, 0, 0x53053053) = -1 EINVAL          #   TCMalloc's registration refused
$ GLIBC_TUNABLES=glibc.pthread.rseq=0 strace -f -e trace=rseq $WORK/out/probe
rseq(..., 0x20, 0, 0x53053053) = 0                  #   only TCMalloc registers, and succeeds
```

In your own program, `tcmalloc::MallocExtension::PerCpuCachesActive()` from
`tcmalloc/malloc_extension.h` reports the mode. Caveat: the tunable disables
glibc's rseq for the whole process, so anything else relying on glibc's
registration (`__rseq_offset`, glibc's rseq-based `sched_getcpu`) loses it.

## Pitfalls (each one was hit)

- **Compiler.** TCMalloc's version check (`tcmalloc/internal/config.h`:
  GCC >= 12.1, Clang >= 11) is not a real minimum. GCC 11.5 is rejected, and
  Clang 15 passes the check but fails to compile `tcmalloc/span.h`
  (restrict-qualified member calls). TCMalloc's CI uses Abseil's
  `oss-linux-latest` container (recent compilers). `build.sh` therefore pins
  an official LLVM release (Clang 23.1.6), SHA-256-verified and unpacked under
  `$WORK`; nothing is installed system-wide. Set `CC=` to use your own.
- **Wrong libstdc++ for a standalone Clang.** Clang picks the GCC
  installation with the highest version number, which on AL2023 is the
  C-only gcc14 package (no libstdc++ headers): `'cassert' file not found`.
  `build.sh` wraps clang with `--gcc-install-dir` set to the directory the
  system `g++` uses.
- **LLVM's `.tar.zst` needs `zstd --long=31`.** It is compressed with a 1 GiB
  window, which plain `tar --zstd` refuses.
- **Per-CPU mode is silently off when glibc registers rseq.** TCMalloc's
  per-CPU caches (its design point) need the thread's rseq area, and a thread
  can register only one. glibc registers its own by default (upstream glibc
  2.35 and later, and backports: AL2023's 2.34 does). TCMalloc's registration
  then fails with `EINVAL` (visible in `strace -e trace=rseq`) and it falls
  back to per-thread caches **without any message**. The fix is
  `GLIBC_TUNABLES=glibc.pthread.rseq=0`, which TCMalloc's docs do not mention.
  `run.sh` reports both modes: `tcmalloc-percpu` (tunable set) and
  `tcmalloc-asis` (what an unmodified program gets). The probe confirms the
  mode each time.
- **Do not compare across differently built binaries.** How much a
  false-sharing allocator loses depends on how fast the benchmark's write loop
  is. The Clang 23/Bazel build's loop is faster than the GCC 11/`make` one
  (2.67 s vs 4.34 s single-threaded), and glibc's cache-scratch 8-thread
  speedup moves from 3.7x to 1.8x between the two builds. Allocators that
  avoid false sharing scale ideally either way. Hence step 4: every number in
  a table must come from the same binaries.
- **The benchmarks themselves were broken** until 2026-11-18: compilers
  elided their heap accesses (and with C++14 new/delete elision, the
  allocations), so every allocator scored the same. They now write through a
  `volatile` pointer. Check the disassembly for allocator calls in `worker`
  before trusting any false-sharing result.

## Results (2026-11-18)

Speedup from 1 to 8 / 32 threads (ideal 8 / 32). Median of 5 runs, all on the
identical Clang 23/Bazel binaries. The CI column is wall time in seconds
(lower is better).

| Allocator | cache-scratch (passive) | cache-thrash (active) | CI cache_scratch 32t |
|---|---|---|---|
| **Google TCMalloc**, per-CPU mode | **0.62 / 1.79** | 7.98 / 31.2 | 0.246 |
| **Google TCMalloc**, as-is (per-thread fallback) | **0.47 / 1.78** | **0.46 / 1.78** | 0.243 |
| gperftools tcmalloc 2.9.1 | **0.55 / 1.95** | **0.53 / 1.97** | 0.440 |
| glibc 2.34 (default) | **1.80 / 6.84** | 7.98 / 31.1 | 0.127 |
| Hoard (master) | 7.98 / 30.9 | 7.98 / 31.0 | 0.036 |
| jemalloc 5.2.1 | 7.98 / 31.1 | 7.98 / 31.1 | 0.034 |
| mimalloc 2.5.3 | 7.97 / 31.2 | 7.98 / 31.2 | 0.034 |
| mimalloc 3.7.0 | 7.98 / 31.3 | 7.99 / 31.3 | 0.034 |
| snmalloc (main, 8338112) | 7.98 / 31.3 | 7.99 / 31.3 | 0.033 |
| *control: adopts foreign frees* | *0.94 / 3.68* | *7.98 / 30.9* | *0.244* |
| *control: shared bump region* | *2.58 / 9.56* | *2.41 / 9.47* | *0.410* |

- **Google TCMalloc avoids active false sharing in per-CPU mode** (each CPU
  gets its own objects) **but not passive false sharing in either mode.** A
  thread that frees an object allocated by another thread caches it per CPU
  (or per thread) and hands it straight back, so writes from different
  threads land on one cache line. On cache-scratch it is slower at 8 threads
  than at 1 (0.62x), worse than the deliberately broken adopt control.
- **In the mode an unmodified program gets** on a glibc that registers rseq
  (here), it fails **both** tests, like gperftools' tcmalloc.
- Hoard, jemalloc, mimalloc 2/3 and snmalloc scale ideally on both tests and
  are indistinguishable here.
- Earlier tables built with GCC 11/`make` show the same pass/fail pattern;
  the absolute speedups of the failing allocators differ for the reason given
  under "Do not compare across differently built binaries".

