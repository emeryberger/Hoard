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
- `larson` - Server workload simulation
- `linux-scalability` - University of Michigan scalability test
