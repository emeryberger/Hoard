# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Hoard is a high-performance, scalable memory allocator for multithreaded applications. It's a drop-in replacement for malloc that eliminates contention, false sharing, and memory blowup problems common in system allocators.

## Build Commands

### Building the Library (Linux/macOS)

```bash
mkdir build && cd build
cmake ..
make
```

Output: `build/libhoard.dylib` (macOS) or `build/libhoard.so` (Linux)

### Building the Library (Windows)

Windows builds use Microsoft Detours for function interposition. Detours is automatically fetched and built by CMake:

```powershell
mkdir build && cd build
cmake ..
cmake --build . --config Release
```

Output: `build/Release/hoard.dll`, `build/Release/withdll.exe`, `build/Release/setdll.exe`

**Using a pre-installed Detours (optional):**

If you prefer to use a system-installed Detours (via vcpkg or manual build):

```powershell
# Install via vcpkg
vcpkg install detours:x64-windows      # or arm64-windows, x86-windows

# Build with system Detours
cmake .. -DUSE_SYSTEM_DETOURS=ON -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake

# Or if built from source
cmake .. -DUSE_SYSTEM_DETOURS=ON -DDETOURS_ROOT=C:/path/to/Detours
```

### Building Benchmarks

```bash
cd benchmarks
make
```

Individual benchmark:
```bash
cd benchmarks/threadtest
make
```

### Running with Hoard

**Linux:**
```bash
LD_PRELOAD=/path/to/libhoard.so ./myprogram
```

**macOS:**
```bash
DYLD_INSERT_LIBRARIES=/path/to/libhoard.dylib ./myprogram
```

**Windows (unmodified binaries):**

**Important:** Programs must be compiled with `/MD` (dynamic C runtime) for Hoard to intercept allocations. Programs compiled with `/MT` (static C runtime) have allocation functions embedded directly in the executable, which Hoard cannot intercept.

Windows uses DLL injection via `withdll.exe` (built automatically with Hoard):

```powershell
# From the build directory:
build\Release\withdll.exe /d:build\Release\hoard.dll myprogram.exe [args...]
```

The `/d:` flag specifies the DLL to inject. Multiple DLLs can be injected:
```powershell
withdll.exe /d:hoard.dll /d:other.dll myprogram.exe
```

**Alternative Windows methods:**

1. **setdll.exe (permanent modification):** Modifies the executable's import table to always load Hoard (also built automatically):
   ```powershell
   # Add Hoard to executable (creates backup as .exe~)
   build\Release\setdll.exe /d:build\Release\hoard.dll myprogram.exe

   # Remove Hoard from executable
   build\Release\setdll.exe /r:hoard.dll myprogram.exe
   ```

2. **AppInit_DLLs (system-wide, requires admin):** Registry-based injection for all processes:
   ```powershell
   # Not recommended for production - affects all processes
   reg add "HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Windows" /v AppInit_DLLs /t REG_SZ /d "C:\path\to\hoard.dll"
   reg add "HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Windows" /v LoadAppInit_DLLs /t REG_DWORD /d 1
   ```

### Running Benchmarks

```bash
./threadtest <threads> <iterations> <objects> <work> <size>
# Example: ./threadtest 4 1000 10000 0 8
```

**Windows benchmark example:**
```powershell
withdll.exe /d:hoard.dll threadtest.exe 4 1000 10000 0 8
```

## Architecture

### Heap Hierarchy (Bottom-up)

```
Thread-Local Allocation Buffers (TLABs)
    ↓ overflow
Per-Thread Heaps (PerThreadHoardHeap)
    ↓ emptiness threshold crossed
Global Heap (TheGlobalHeap)
    ↓ OS allocation
MmapSource (AlignedMmap)
```

### Key Architectural Concepts

**Superblocks**: Memory is managed in aligned chunks (256KB on Unix, 64KB on Windows). Each superblock contains a header and object allocations. Superblock address found via bitmask: `ptr & ~(SUPERBLOCK_SIZE-1)`.

**Emptiness Classes**: Superblocks are categorized by fullness (8 classes). This enables efficient memory reclamation - when a per-thread heap crosses the emptiness threshold, superblocks move to the global heap.

**TLABs**: Per-thread caches for small objects (up to 1024 bytes). Max 16MB per TLAB. Reduces contention between threads.

**Size Separation**: Small objects go through `SmallHeap` (thread-local with superblock management). Large objects go through `BigHeap` (threshold-based segment heap with geometric size classes).

### Source Organization

```
src/
├── include/
│   ├── hoard/           # Core allocator components
│   │   ├── hoardheap.h         # Main heap composition (HoardHeap template)
│   │   ├── hoardmanager.h      # Superblock manager by emptiness classes
│   │   ├── globalheap.h        # Single global heap for redistribution
│   │   ├── hoardsuperblock.h   # Superblock structure
│   │   └── hoardconstants.h    # Configuration constants
│   ├── superblocks/     # Superblock/TLAB management
│   │   ├── tlab.h              # Thread-local allocation buffer
│   │   └── alignedsuperblockheap.h
│   └── util/            # Generic utilities
│       ├── alignedmmap.h       # Aligned OS allocation
│       └── thresholdsegheap.h  # Threshold-based segment heap
├── source/
│   ├── libhoard.cpp           # malloc/free/realloc entry points
│   ├── mactls.cpp             # macOS thread-local storage
│   ├── unixtls.cpp            # Unix TLS & pthread interception
│   ├── wintls.cpp             # Windows TLS & DllMain
│   └── winwrapper-detours.cpp # Windows Detours-based interposition
└── cmake/
    └── FindDetours.cmake      # CMake module to find Detours library
```

**Heap-Layers Dependency**: Fetched via CMake FetchContent from https://github.com/emeryberger/Heap-Layers. Provides the layered heap framework, locks, and utility wrappers.

### Key Constants (hoardconstants.h)

- `MAX_MEMORY_PER_TLAB`: 16MB
- `MaxThreads`: 2048
- `NumHeaps`: 128
- `LargestSmallObject`: 1024 bytes

### Platform-Specific Code

- **macOS**: Uses `MacLockType`, `macwrapper.cpp`, `mactls.cpp`
- **Linux**: Uses `SpinLockType`, `unixtls.cpp`
- **Windows**: Uses `WinLockType`, `winwrapper-detours.cpp`, `wintls.cpp`
  - Supports x86, x64, ARM, and ARM64 architectures
  - Uses Microsoft Detours for function interposition
  - Intercepts CRT, Windows Heap API, and RTL Heap API functions

### Heap Composition Pattern

The allocator is built through template composition. The main heap type `HoardHeap<N, NH>` composes:
- `ANSIWrapper` - Standard malloc interface
- `IgnoreInvalidFree` - Graceful handling of bad frees
- `HybridHeap` - Routes by size to SmallHeap or BigHeap
- `ThreadPoolHeap` - Per-thread heap pool
- `RedirectFree` - Routes frees to correct heap via superblock header

## Benchmarks

Located in `benchmarks/`:
- `threadtest` - Per-thread throughput (allocation/deallocation cycles)
- `cache-scratch`, `cache-thrash` - False sharing tests
- `larson` - Server workload simulation (mimalloc-bench parameters: `larson 10 7 8 1000 10000 1 <threads>`)
- `linux-scalability` - University of Michigan scalability test

## Performance Optimization Notes

### macOS TLS Optimization

On macOS, `__thread` variables go through `_tlv_get_addr()` which adds significant overhead (~50+ cycles per access). The `initial-exec` TLS model does NOT help on macOS - it still calls `_tlv_get_addr`. This is a fundamental difference from Linux where `initial-exec` gives direct TLS access.

**Solution**: Direct pthread TLS slot access via inline assembly (see `mactls.cpp`). Uses slot 89 (`__PTK_FRAMEWORK_OLDGC_KEY9`), same technique as mimalloc. This bypasses `_tlv_get_addr` entirely.

ARM64 (Apple Silicon) quirk: Must use `tpidrro_el0` register (read-only thread pointer), NOT `tpidr_el0`. The `__builtin_thread_pointer()` intrinsic reads the wrong register on macOS and will crash.

### Hot Path Optimizations

Key optimizations in the malloc/free fast path:

1. **Superblock caching** (tlab.h): Cache the last-freed superblock pointer plus a copy of its read-only header fields (`_start`, object size, power-of-two flag, magic mul/shift, class size). Consecutive frees to the same superblock normalize and account entirely from TLAB-local fields with zero superblock header loads. The "no cached superblock" sentinel is `(SuperblockType*)1`, never `nullptr` (a garbage near-zero pointer masks to a null superblock and must not match the cache, which performs no validity check).

2. **always_inline attribute** (tlab.h): Force inlining of TLAB malloc/free. LTO doesn't always inline these despite the `inline` keyword. Same for `Header::normalize` (hoardsuperblockheader.h) — the multiplicative-inverse branch is just big enough that compilers outline it — and for `TheCustomHeapType::malloc/free` (hoardtlab.h), which would otherwise stay an out-of-line `ANSIWrapper::malloc` call on every allocation.

3. **Branch prediction hints**: Use `__builtin_expect()` (via `TLAB_LIKELY`/`TLAB_UNLIKELY` macros) on hot path conditionals.

4. **Batch TLAB refill and flush** (tlab.h + `mallocMany`/`freeMany` in redirectfree.h, lockmallocheap.h, hoardmanager.h): when a TLAB bin is empty, fetch up to 64 objects under ONE per-thread-heap lock acquisition instead of one lock per object; symmetrically, when the TLAB overflows its threshold, flush up to 64 objects home under one lock (`flushBin`). Both are bounded by the TLAB's adaptive threshold, so blowup bounds are unchanged (a flush only ever REMOVES objects from the TLAB). The flush side matters because the single-object free path costs TWO lock acquisitions (superblock, then owning heap) once the delayed queue is full, so a thread whose live set outgrows the TLAB paid that on *every* free — the dominant cost of bulk workloads (10M-object live set: 16.8 → 12.7 ns/op, and linux-scalability 0.315s → 0.253s). The flush must stay OFF the hot path: it is reached only when the TLAB is full AND the object is home-owned, and both the threshold test and the remote test must stay in the SINGLE combined predicted branch the fast path already had. Splitting them, or routing foreign frees through the out-of-line overflow helper, cost ~5% on 8-thread larson — cross-thread frees are hot at high thread counts, and they must go straight to `_parentHeap->free` exactly as before. `freeMany` must NOT assume the batch is home-owned: a superblock can be moved to the global heap while its objects sit in a bin, so it re-reads the owner under the lock exactly as `free` does, and falls back to the single-object protocol for any object whose ownership changed.

5. **xxowns ownership hook** (libhoard.cpp + util/ownershipmap.h): on macOS, the alloc8 interposition layer needs an ownership predicate for every free/realloc/malloc_size. Without a hook it maintains an internal pointer->size hash table (a hash insert/erase on EVERY malloc/free) that hard-saturates at ~4M live objects, after which frees of Hoard's own pointers fall into a catastrophically slow `malloc_zone_from_ptr` walk and get dropped. Hoard exports strong `xxowns()`/`xxowns_active()` backed by `OwnershipMap`: one bit per 256KB chunk of address space, registered at the `AlignedMmap` layer (which rounds all region sizes to a superblock multiple so chunk ownership is exact). The 128MB bitmap is virtual; only pages covering address ranges Hoard actually uses are ever touched.

6. **Hidden visibility on xx* hooks** (libhoard.cpp, macOS **and ELF**): the xx* entry points are only called by alloc8 inside the same shared library, so they are hidden on both Mach-O and ELF. On ELF this matters for an extra reason: a default-visibility symbol in a shared object is *preemptible*, so the compiler must route through the PLT and cannot inline through it. Left exported, Linux `malloc` compiled to nothing but `b xxmalloc@plt` — every allocation paid an indirect jump and the whole TLAB fast path stayed out-of-line behind it. Hiding it took larson on Linux from 0.83x to 1.01x mimalloc (single-threaded 47.7 → 58.9 Mops/s, +23%), i.e. from clearly behind both baselines to parity. macOS was already hidden and is unaffected. Default visibility (exported) blocks ThinLTO from inlining them into `replace_malloc`/`replace_free`; hidden visibility lets the whole chain flatten. Note: alloc8's weak fallback definition of `xxowns` prevents its inlining regardless (ThinLTO will not import a prevailing definition over a module-local weak one), so `xxowns` remains one small call. The ownership bitmap itself (`ownershipmap.h`) is a plain zerofill global defined in libhoard.cpp with hidden visibility: one load + bit test, no GOT hop, no null check. It must stay a NON-WEAK definition — a template static member (weak) cannot be coalesced into a zerofill section on Mach-O and would bloat the dylib by the full 128MB.

7. **Foreign-free routing / home heaps** (tlab.h, threadpoolheap.h, hoardmanager.h): each TLAB refills exclusively from a round-robin-assigned "home heap", and the free path compares a superblock's owner against that home identity (cached per superblock alongside the other header fields, so the hit path pays one predictable branch). Frees of foreign-owned objects are NOT adopted into the local bins — they go home via `RedirectFree` (lock-free delayed queue, locked fallback). Rationale: a LIFO bin hands the same object right back, so adopting foreign objects permanently pins a thread to memory interleaved with other threads' live data at cache-line granularity; larson's shuffled warmup makes every op false-share and costs ~2.4x at 8 threads (this single effect was the entire hoard-vs-mimalloc larson gap). Routing foreign frees home lets mixed working sets migrate apart within one pass, exactly like mimalloc's owner-page frees. The home-heap identity must be THREAD-stable, not CPU-stable: keying it to `pthread_cpu_number_np` reclassifies a migrating thread's whole working set as foreign and churns it through remote frees (measured: RSS growth plus throughput decay). The delayed queue is capped small (256) because pushes bypass emptiness accounting — an idle or dead owner may never drain, so sustained cross-thread frees must overflow to the locked path whose bookkeeping lets empty superblocks recirculate. Owners reconcile pending delayed frees (with stats adjustment) at `getObject`, and superblock transfers drain in `HoardManager::get`/`unlocked_put` before stats are computed.

### Size Classes

`sizeclasses.h` is the **single source of truth**: 16-byte steps from 16 to 128, then four classes per power of two up to 32768 (40 bins, ≤25% worst-case internal fragmentation vs ~100% for the generic power-of-two classes). Both consumers are thin bindings of it and are generated at compile time — `bins256k.h` (`HL::bins<Header, 262144>`, used by the parent heap) and `sizeclasslut.h` (`getSizeClassLUT`, used by the TLAB fast path). **Edit only `sizeclasses.h`.**

These were previously two hand-maintained tables that "MUST stay in sync" — the TLAB indexes its bins by the LUT while the parent heap uses `HL::bins`, so a drift would silently hand out undersized memory. That is now a compile error: `sizeclasses.h` `static_assert`s that its lookup table and closed-form large-size path agree with the reference classifier at every class boundary and every LUT bucket, and that the classes are increasing multiples of the 16-byte quantum. Reintroducing the old 8-byte-spaced classes, or drifting the large-size path off the table, both fail to compile.

Non-power-of-two object sizes rely on the precomputed multiplicative-inverse modulo in the superblock header. Windows (64KB superblocks) still uses the generic power-of-two bins; the LUT mismatch there is benign (bins keyed by LUT classes hold objects at least as large as any request mapped to them) but wastes some TLAB reuse.

**Every class must be a multiple of 16, and the smallest must be 16** (`Alignment = sizeof(void*) * 2` in hoardsuperblockheader.h). Objects are packed contiguously from a 16-byte-aligned superblock base, so a class size that is not a multiple of 16 misaligns every subsequent object in the superblock and breaks malloc's fundamental-alignment guarantee (`alignof(max_align_t) == 16`). The original classes used 8-byte steps (8, 24, 40, ..., 120) and returned 8-byte-aligned pointers for *half* of all allocations in those classes — an ABI violation that breaks SIMD, 16-byte atomics, and `long double`. 16 is also macOS malloc's minimum allocation size. The superblock header asserts both invariants (`_objectSize >= Alignment`, `_objectSize % Alignment == 0`); those asserts had never run because `-DNDEBUG` was forced on in every build type (see Build/Assertions).

### Memory / RSS

- **Purge-on-empty** (shardedglobalheap.h): when a completely-empty superblock reaches the global heap, its data pages are discarded via `MADV_FREE_REUSABLE` (macOS; `MADV_FREE`/`MADV_DONTNEED` on Linux, `DiscardVirtualMemory` on Windows — see util/purge.h). `clear()` first resets the freelist to bump-pointer mode so no allocator metadata lives in purged pages. Plain `MADV_FREE` does NOT lower RSS/footprint numbers on macOS until memory pressure; `MADV_FREE_REUSABLE` does.
- **Big-object retention** (hoardheap.h): ThresholdSegHeap waste threshold is 10%.
- `AddHeaderHeap::free` must free `size + headerSize` — malloc maps `sz + headerSize`, and the mmap layer's map/unmap sizes have to match or trailing pages/ownership bits leak.

### Assertions

`cmake -DCMAKE_BUILD_TYPE=Debug` gives an assertion-checked build; Release/RelWithDebInfo define `NDEBUG` via CMake's own per-config flags. Do NOT add `-DNDEBUG` to `add_definitions`/`add_compile_options`: it was previously forced on for all three platforms, which changed nothing for Release but made an assertion-checked build impossible — every assert in the heap layers (`isValid()`, alignment, size-class invariants) was compiled out in every configuration, and the size-class alignment bug above sat undetected behind them. Note that a `-O0` build is far slower (it is the allocator), so use short benchmark parameters when running larson/threadtest against it.

### Statically-initialized globals

Globals that Hoard's own entry points touch **must be constant-initialized** — Hoard interposes `malloc`, so dyld and other images' initializers allocate through it *before* libhoard's own `__mod_init_func` runs. A global with a dynamic initializer is therefore written *after* the allocator is already live, clobbering whatever it accumulated during startup.

This bit the ownership bitmap (`ownershipmap.h` / `libhoard.cpp`): an array of `std::atomic<uint64_t>` **cannot** be constant-initialized with libc++ (its default constructor is not usable in a constant expression), so unoptimized builds emitted a dynamic initializer that walked all 2^24 elements at load and zeroed the map, wiping the ownership bits of every superblock mapped during startup. alloc8 then saw those pointers as foreign and misrouted their `free`/`malloc_size` (objc aborted with "corrupt data pointer"; `malloc_size` returned 0). Optimized builds constant-folded it to zerofill and were unaffected — an optimization-level-dependent miscompile of the invariant. The storage is now a plain `uint64_t` array (no constructor, always zerofill) accessed via `std::atomic_ref`, marked `constinit` so the compiler enforces it. It would also have touched all 128MB, turning reserved address space into resident memory.

### Interposition-layer overhead (alloc8)

Hoard's fast path is only as short as alloc8's entry points, which sit in front of every `malloc`/`free`. Measured on a tight recycle loop (M1 Max), alloc8's per-op entry work was **~36% of the whole allocation fast path**. It is now one acquire load (`g_fast` in alloc8) gating init, passthrough and stats together, and Hoard compiles with `-DALLOC8_NO_CALLER_RA` (CMakeLists.txt) because it never reads alloc8's caller-return-address hint — a `thread_local` store that cost ~15% on its own.

Do NOT relax alloc8's `ensure_init` acquire load to relaxed (worth another ~12%): dyld constructors can allocate from several threads before alloc8's priority-101 init runs — that is why `alloc8_init_once` has a spin-wait — and a relaxed load could observe `INIT_DONE` without the values it publishes.

If a change to the fast path shows no benefit, check whether alloc8 is the bottleneck before optimizing Hoard: the *bulk* gap is Hoard's (locks and per-object bookkeeping), but the *recycle* gap was alloc8's.

### Baseline Comparisons

When optimizing, compare against mimalloc and jemalloc, not system malloc. Use consistent benchmark parameters across runs. Larson is sensitive to cross-thread free patterns; threadtest measures pure per-thread throughput.

Measurement notes (macOS): SIP strips `DYLD_INSERT_LIBRARIES` for system binaries — a wrapper like `/usr/bin/time` will silently drop it, and hardened-runtime binaries (system python3, sqlite3, etc.) ignore it entirely, so interposition tests must use locally-built unsigned binaries. Verify interposition rather than assuming it (e.g. `MIMALLOC_VERBOSE=1` for mimalloc, `DYLD_PRINT_LIBRARIES=1` for Hoard).

Larson status (July 2026): at the canonical parameters (`larson 10 7 8 1000 10000 1 8`) Hoard is at parity with mimalloc (~330-360M ops/s both, M1 Max) after the foreign-free routing work; single-threaded they tie. Remaining gaps: (1) larson with sizes spanning many classes (e.g. 100-200) is ~65% of mimalloc — the single-entry TLAB superblock cache thrashes when consecutive frees hit different superblocks; (2) linux-scalability and pure tight-loop recycle are ~70% of mimalloc — raw fast-path length (alloc8 `replace_*` entry checks with an acquire load per op, PAC/frame overhead in four stacked frames per op, and the per-op `_localHeapBytes` accounting). Also note: larson leaks its own zombie threads (joinable, never joined) — RSS climbing during long larson runs is mostly dead thread stacks and afflicts every allocator equally; measure allocator RSS with respawn disabled (huge `num_rounds`).
