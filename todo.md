# Hoard Windows Detours Port - Status

## Completed

- [x] Created `src/source/winwrapper-detours.cpp` - Detours-based allocation interposition
- [x] Created `cmake/FindDetours.cmake` - CMake module to find Detours
- [x] Updated `CMakeLists.txt` for Windows build with Detours
- [x] Auto-fetch Detours from GitHub (main branch, not v4.0.1 which has ARM64 bugs)
- [x] Build `withdll.exe` and `setdll.exe` tools automatically
- [x] Export `DetourFinishHelperProcess` at ordinal #1 (required for withdll.exe)
- [x] Fixed MSVC compilation errors:
  - [x] `WIN32_LEAN_AND_MEAN` redefinition warnings
  - [x] `lstrlen` → `lstrlenW` for wide strings
  - [x] `__attribute__` conditional for GCC vs MSVC
  - [x] `__builtin_expect` → no-op for MSVC (`likely`/`unlikely` macros)
- [x] Added `DetourRestoreAfterWith()` call (critical for withdll.exe injection)
- [x] Added `msvcp*.dll` to module detection for C++ runtime
- [x] Removed exit function interception (was causing intermittent hangs)
- [x] Updated README.md with Windows build/usage instructions
- [x] Updated CLAUDE.md with Windows documentation

## Current Issue

**Intermittent process termination** - Sometimes the process exits before printing final output.

**Possible cause**: Interposition may not be fully working. Added diagnostic output:
- `Hoard: Memory allocator active` = success
- `Hoard: FAILED TO INITIALIZE` = failure

## Functions Intercepted

### CRT Functions (from ucrt/msvcrt/vcruntime DLLs)
- `malloc`, `free`, `calloc`, `realloc`, `_msize`, `_expand`, `_recalloc`, `strdup`
- `_malloc_base`, `_malloc_crt`, `_free_base`, `_free_crt`, etc.
- Debug variants: `_malloc_dbg`, `_free_dbg`, etc.

### C++ Operators (64-bit mangled names)
- `??2@YAPEAX_K@Z` - operator new(size_t)
- `??_U@YAPEAX_K@Z` - operator new[](size_t)
- `??3@YAXPEAX@Z` - operator delete(void*)
- `??_V@YAXPEAX@Z` - operator delete[](void*)

### C++ Operators (32-bit mangled names)
- `??2@YAPAXI@Z` - operator new(unsigned int)
- `??_U@YAPAXI@Z` - operator new[](unsigned int)
- `??3@YAXPAX@Z` - operator delete(void*)
- `??_V@YAXPAX@Z` - operator delete[](void*)

### Windows Heap API (kernel32.dll)
- `HeapAlloc`, `HeapFree`, `HeapReAlloc`, `HeapSize`, `HeapCompact`, `HeapValidate`, `HeapWalk`

### RTL Heap API (ntdll.dll)
- `RtlAllocateHeap`, `RtlFreeHeap`, `RtlSizeHeap`

## Modules Scanned for Patching

DLLs containing these substrings are scanned:
- `CRT`, `crt`
- `ucrt`, `UCRT`
- `msvcr`, `MSVCR`
- `msvcp`, `MSVCP`
- `vcruntime`, `VCRUNTIME`

## Next Steps to Debug

1. **Check diagnostic output** - Does "Hoard: Memory allocator active" appear?

2. **If not active**, possible issues:
   - Module names don't match expected patterns
   - Functions not exported from expected DLLs
   - Statically linked CRT (no external DLLs to patch)

3. **If active but still slow**, possible issues:
   - C++ new/delete not being intercepted (wrong mangled names for ARM64?)
   - Need to also intercept from the executable itself, not just DLLs

4. **To enable debug logging**, build with:
   ```
   cmake .. -DCMAKE_CXX_FLAGS="/DHOARD_DEBUG"
   ```

## Architecture Support

- [x] x86 (32-bit)
- [x] x64 (64-bit)
- [x] ARM (32-bit)
- [x] ARM64 (64-bit) - Currently being tested

## Build Commands

```powershell
# Clean build
rmdir /S /Q build
mkdir build && cd build
cmake ..
cmake --build . --config Release

# Run with Hoard
Release\withdll.exe /d:Release\hoard.dll program.exe [args...]
```

## Files Modified/Created

| File | Status |
|------|--------|
| `src/source/winwrapper-detours.cpp` | Created - Detours wrapper |
| `src/source/wintls.cpp` | Modified - Fixed lstrlenW |
| `src/source/libhoard.cpp` | Modified - Fixed __attribute__ |
| `src/include/superblocks/manageonesuperblock.h` | Modified - Fixed likely/unlikely |
| `cmake/FindDetours.cmake` | Created - CMake find module |
| `CMakeLists.txt` | Modified - Windows build support |
| `README.md` | Modified - Windows instructions |
| `CLAUDE.md` | Modified - Windows documentation |
