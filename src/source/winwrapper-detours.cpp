/* -*- C++ -*- */

/*
 * @file winwrapper-detours.cpp
 * @brief  Replaces malloc family on Windows using Microsoft Detours.
 * @author Emery Berger <http://www.emeryberger.org>
 * @note   Copyright (C) 2011-2024 by Emery Berger.
 *
 * This implementation uses Microsoft Detours for function interposition,
 * replacing the manual x86 code patching approach in winwrapper.cpp.
 *
 * Detours provides:
 * - Thread-safe transaction-based hooking
 * - Proper trampolines for calling original functions
 * - Better handling of edge cases (hot-patching, etc.)
 * - Support for both 32-bit and 64-bit
 */

#include <windows.h>
#include <errno.h>
#include <psapi.h>
#include <stdio.h>
#include <tchar.h>
#include <new>

// Microsoft Detours header
#include <detours.h>

#pragma comment(lib, "detours.lib")

extern "C" {
  void * xxmalloc(size_t);
  void   xxfree(void *);
  size_t xxmalloc_usable_size(void *);
  void   xxmalloc_lock(void);
  void   xxmalloc_unlock(void);
}

#ifdef _DEBUG
#error "This library must be compiled in release mode."
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#if (_WIN32_WINNT < 0x0500)
#define _WIN32_WINNT 0x0500
#endif

#pragma inline_depth(255)

#pragma warning(disable: 4273)
#pragma warning(disable: 4098)
#pragma warning(disable: 4355)
#pragma warning(disable: 4074)

//
// Original function pointers (trampolines)
//

// C++ operators - 64-bit mangled names
static void * (WINAPI * Real_new_64)(size_t) = nullptr;
static void * (WINAPI * Real_new_array_64)(size_t) = nullptr;
static void   (WINAPI * Real_delete_64)(void *) = nullptr;
static void   (WINAPI * Real_delete_array_64)(void *) = nullptr;

// C++ operators - 32-bit mangled names
static void * (WINAPI * Real_new_32)(size_t) = nullptr;
static void * (WINAPI * Real_new_array_32)(size_t) = nullptr;
static void   (WINAPI * Real_delete_32)(void *) = nullptr;
static void   (WINAPI * Real_delete_array_32)(void *) = nullptr;

// C++ nothrow variants
static void * (WINAPI * Real_new_nothrow)(size_t, const std::nothrow_t&) = nullptr;
static void * (WINAPI * Real_new_array_nothrow)(size_t, const std::nothrow_t&) = nullptr;
static void   (WINAPI * Real_delete_nothrow)(void *, const std::nothrow_t&) = nullptr;
static void   (WINAPI * Real_delete_array_nothrow)(void *, const std::nothrow_t&) = nullptr;

// Standard C allocation functions
static void * (__cdecl * Real_malloc)(size_t) = nullptr;
static void   (__cdecl * Real_free)(void *) = nullptr;
static void * (__cdecl * Real_calloc)(size_t, size_t) = nullptr;
static void * (__cdecl * Real_realloc)(void *, size_t) = nullptr;
static size_t (__cdecl * Real_msize)(void *) = nullptr;
static void * (__cdecl * Real_expand)(void *, size_t) = nullptr;
static void * (__cdecl * Real_recalloc)(void *, size_t, size_t) = nullptr;
static char * (__cdecl * Real_strdup)(const char *) = nullptr;

// CRT internal variants
static void * (__cdecl * Real_malloc_base)(size_t) = nullptr;
static void * (__cdecl * Real_malloc_crt)(size_t) = nullptr;
static void   (__cdecl * Real_free_base)(void *) = nullptr;
static void   (__cdecl * Real_free_crt)(void *) = nullptr;
static void * (__cdecl * Real_realloc_base)(void *, size_t) = nullptr;
static void * (__cdecl * Real_realloc_crt)(void *, size_t) = nullptr;
static void * (__cdecl * Real_calloc_base)(size_t, size_t) = nullptr;
static void * (__cdecl * Real_calloc_crt)(size_t, size_t) = nullptr;

// Debug CRT functions
static void * (__cdecl * Real_malloc_dbg)(size_t, int, const char *, int) = nullptr;
static void   (__cdecl * Real_free_dbg)(void *, int) = nullptr;
static void * (__cdecl * Real_realloc_dbg)(void *, size_t, int, const char *, int) = nullptr;
static void * (__cdecl * Real_calloc_dbg)(size_t, size_t, int, const char *, int) = nullptr;
static size_t (__cdecl * Real_msize_dbg)(void *, int) = nullptr;
static void * (__cdecl * Real_expand_dbg)(void *, size_t, int, const char *, int) = nullptr;
static void * (__cdecl * Real_recalloc_dbg)(void *, size_t, size_t, int, const char *, int) = nullptr;

// Windows Heap API
static LPVOID (WINAPI * Real_HeapAlloc)(HANDLE, DWORD, SIZE_T) = nullptr;
static BOOL   (WINAPI * Real_HeapFree)(HANDLE, DWORD, LPVOID) = nullptr;
static LPVOID (WINAPI * Real_HeapReAlloc)(HANDLE, DWORD, LPVOID, SIZE_T) = nullptr;
static SIZE_T (WINAPI * Real_HeapSize)(HANDLE, DWORD, LPCVOID) = nullptr;
static SIZE_T (WINAPI * Real_HeapCompact)(HANDLE, DWORD) = nullptr;
static BOOL   (WINAPI * Real_HeapValidate)(HANDLE, DWORD, LPCVOID) = nullptr;
static BOOL   (WINAPI * Real_HeapWalk)(HANDLE, LPPROCESS_HEAP_ENTRY) = nullptr;

// RTL Heap API (ntdll.dll)
static PVOID  (NTAPI * Real_RtlAllocateHeap)(PVOID, ULONG, SIZE_T) = nullptr;
static BOOLEAN(NTAPI * Real_RtlFreeHeap)(PVOID, ULONG, PVOID) = nullptr;
static ULONG  (NTAPI * Real_RtlSizeHeap)(PVOID, ULONG, PVOID) = nullptr;

// Exit functions
static void   (__cdecl * Real_exit)(int) = nullptr;
static void   (__cdecl * Real__exit)(int) = nullptr;
static int    (__cdecl * Real_atexit)(void (*)(void)) = nullptr;
static _onexit_t (__cdecl * Real__onexit)(_onexit_t) = nullptr;
static void   (__cdecl * Real__cexit)(void) = nullptr;
static void   (__cdecl * Real__c_exit)(void) = nullptr;

//
// Exit function handling
//

static const int MAX_EXIT_FUNCTIONS = 2048;
static _onexit_t exitFunctions[MAX_EXIT_FUNCTIONS];
static int exitFunctionsRegistered = 0;

static void executeRegisteredFunctions() {
  for (int i = exitFunctionsRegistered; i > 0; i--) {
    (*exitFunctions[i - 1])();
  }
  exitFunctionsRegistered = 0;
}

//
// Replacement functions (detours)
//

// Debug: Uncomment HOARD_VERIFY_DELAY to add artificial delay for verification
// #define HOARD_VERIFY_DELAY 1

#ifdef HOARD_VERIFY_DELAY
static volatile long g_allocCount = 0;
#endif

static void * __cdecl Detour_malloc(size_t sz) {
#ifdef HOARD_VERIFY_DELAY
  // Add 1ms delay every 1000 allocations to verify interception
  if (InterlockedIncrement(&g_allocCount) % 1000 == 0) {
    Sleep(1);
  }
#endif
  return xxmalloc(sz);
}

static void __cdecl Detour_free(void * ptr) {
  xxfree(ptr);
}

static void * __cdecl Detour_calloc(size_t num, size_t size) {
  void * ptr = xxmalloc(num * size);
  if (ptr) {
    memset(ptr, 0, num * size);
  }
  return ptr;
}

static void * __cdecl Detour_realloc(void * ptr, size_t sz) {
  if (ptr == nullptr) {
    return xxmalloc(sz);
  }
  if (sz == 0) {
    xxfree(ptr);
    return xxmalloc(1);
  }

  size_t originalSize = xxmalloc_usable_size(ptr);
  size_t minSize = (originalSize < sz) ? originalSize : sz;

  // Don't change size if shrinking by less than half.
  if ((originalSize / 2 < sz) && (sz <= originalSize)) {
    return ptr;
  }

  void * buf = xxmalloc(sz);
  if (buf != nullptr) {
    memcpy(buf, ptr, minSize);
    xxfree(ptr);
  }
  return buf;
}

static size_t __cdecl Detour_msize(void * ptr) {
  return xxmalloc_usable_size(ptr);
}

static void * __cdecl Detour_expand(void * ptr, size_t newSize) {
  // _expand cannot be supported - it requires in-place expansion.
  return nullptr;
}

static void * __cdecl Detour_recalloc(void * memblock, size_t num, size_t size) {
  const size_t requestedSize = num * size;
  void * ptr = Detour_realloc(memblock, requestedSize);
  if (ptr != nullptr) {
    const size_t actualSize = xxmalloc_usable_size(ptr);
    if (actualSize > requestedSize) {
      memset(static_cast<char *>(ptr) + requestedSize, 0, actualSize - requestedSize);
    }
  }
  return ptr;
}

static char * __cdecl Detour_strdup(const char * s) {
  char * newString = nullptr;
  if (s != nullptr) {
    size_t len = strlen(s) + 1;
    if ((newString = (char *)xxmalloc(len))) {
      memcpy(newString, s, len);
    }
  }
  return newString;
}

// Debug variants
static void * __cdecl Detour_malloc_dbg(size_t size, int blockType, const char * filename, int linenumber) {
  return xxmalloc(size);
}

static void __cdecl Detour_free_dbg(void * userData, int blockType) {
  xxfree(userData);
}

static void * __cdecl Detour_realloc_dbg(void * userData, size_t newSize, int blockType, const char * filename, int linenumber) {
  return Detour_realloc(userData, newSize);
}

static void * __cdecl Detour_calloc_dbg(size_t num, size_t size, int blockType, const char * filename, int linenumber) {
  return Detour_calloc(num, size);
}

static size_t __cdecl Detour_msize_dbg(void * userData, int blockType) {
  return xxmalloc_usable_size(userData);
}

static void * __cdecl Detour_expand_dbg(void * userData, size_t newSize, int blockType, const char * filename, int linenumber) {
  return nullptr;
}

static void * __cdecl Detour_recalloc_dbg(void * memblock, size_t num, size_t size, int blockType, const char * filename, int linenumber) {
  return Detour_recalloc(memblock, num, size);
}

// Windows Heap API replacements
static LPVOID WINAPI Detour_HeapAlloc(HANDLE hHeap, DWORD dwFlags, SIZE_T dwBytes) {
  if (hHeap == nullptr) {
    return nullptr;
  }
  if (dwFlags & HEAP_ZERO_MEMORY) {
    return Detour_calloc(1, dwBytes);
  } else {
    return xxmalloc(dwBytes);
  }
}

static BOOL WINAPI Detour_HeapFree(HANDLE hHeap, DWORD dwFlags, LPVOID lpMem) {
  xxfree(lpMem);
  return TRUE;
}

static LPVOID WINAPI Detour_HeapReAlloc(HANDLE hHeap, DWORD dwFlags, LPVOID lpMem, SIZE_T dwBytes) {
  if (dwFlags & HEAP_REALLOC_IN_PLACE_ONLY) {
    return nullptr;
  }
  if (dwFlags & HEAP_ZERO_MEMORY) {
    return Detour_recalloc(lpMem, 1, dwBytes);
  }
  return Detour_realloc(lpMem, dwBytes);
}

static SIZE_T WINAPI Detour_HeapSize(HANDLE hHeap, DWORD dwFlags, LPCVOID lpMem) {
  return xxmalloc_usable_size((void *)lpMem);
}

static SIZE_T WINAPI Detour_HeapCompact(HANDLE hHeap, DWORD dwFlags) {
  return (1UL << 31);  // 4GB should be enough
}

static BOOL WINAPI Detour_HeapValidate(HANDLE hHeap, DWORD dwFlags, LPCVOID lpMem) {
  return TRUE;  // Stub: heap is always valid
}

static BOOL WINAPI Detour_HeapWalk(HANDLE hHeap, LPPROCESS_HEAP_ENTRY lpEntry) {
  return FALSE;  // Stub: don't walk heap
}

// RTL Heap API replacements
static PVOID NTAPI Detour_RtlAllocateHeap(PVOID HeapHandle, ULONG Flags, SIZE_T Size) {
  return Detour_HeapAlloc(HeapHandle, (DWORD)Flags, Size);
}

static BOOLEAN NTAPI Detour_RtlFreeHeap(PVOID HeapHandle, ULONG Flags, PVOID HeapBase) {
  xxfree(HeapBase);
  return TRUE;
}

static ULONG NTAPI Detour_RtlSizeHeap(PVOID HeapHandle, ULONG Flags, PVOID MemoryPointer) {
  return (ULONG)xxmalloc_usable_size(MemoryPointer);
}

// Exit function replacements
static void __cdecl Detour_exit(int status) {
  executeRegisteredFunctions();
  // Call the original exit to properly flush buffers and run cleanup
  if (Real_exit) {
    Real_exit(status);
  }
  // Fallback if Real_exit wasn't set
  ExitProcess(status);
}

static void __cdecl Detour__exit(int status) {
  // Call the original _exit to properly flush buffers
  if (Real__exit) {
    Real__exit(status);
  }
  // Fallback if Real__exit wasn't set
  ExitProcess(status);
}

static int __cdecl Detour_atexit(void (*fn)(void)) {
  if (exitFunctionsRegistered >= MAX_EXIT_FUNCTIONS) {
    return ENOMEM;
  }
  exitFunctions[exitFunctionsRegistered] = (_onexit_t)fn;
  exitFunctionsRegistered++;
  return 0;
}

static _onexit_t __cdecl Detour__onexit(_onexit_t fn) {
  if (exitFunctionsRegistered >= MAX_EXIT_FUNCTIONS) {
    return nullptr;
  }
  exitFunctions[exitFunctionsRegistered] = fn;
  exitFunctionsRegistered++;
  return fn;
}

static void __cdecl Detour__cexit() {
  executeRegisteredFunctions();
}

static void __cdecl Detour__c_exit() {
  // Do nothing
}

//
// Helper structure for detour attachment
//

struct DetourEntry {
  const char * name;
  void ** ppOriginal;
  void * pDetour;
  bool attached;
};

//
// Attach a single detour for a function from a module
//
static bool AttachDetour(HMODULE hModule, DetourEntry * entry) {
  FARPROC proc = GetProcAddress(hModule, entry->name);
  if (proc == nullptr) {
    return false;
  }

  *entry->ppOriginal = (void *)proc;

  LONG error = DetourAttach(entry->ppOriginal, entry->pDetour);
  if (error == NO_ERROR) {
    entry->attached = true;
    return true;
  }
  return false;
}

//
// Detach a single detour
//
static void DetachDetour(DetourEntry * entry) {
  if (entry->attached && *entry->ppOriginal != nullptr) {
    DetourDetach(entry->ppOriginal, entry->pDetour);
    entry->attached = false;
  }
}

//
// Global detour entries
//

#define DETOUR_ENTRY(name, detour) { #name, (void **)&Real_##name, (void *)detour, false }
#define DETOUR_ENTRY_MANGLED(mangledName, realPtr, detour) { mangledName, (void **)&realPtr, (void *)detour, false }

static DetourEntry g_CRTDetours[] = {
  // Standard C allocation
  DETOUR_ENTRY(malloc, Detour_malloc),
  DETOUR_ENTRY(free, Detour_free),
  DETOUR_ENTRY(calloc, Detour_calloc),
  DETOUR_ENTRY(realloc, Detour_realloc),
  DETOUR_ENTRY_MANGLED("_msize", Real_msize, Detour_msize),
  DETOUR_ENTRY_MANGLED("_expand", Real_expand, Detour_expand),
  DETOUR_ENTRY_MANGLED("_recalloc", Real_recalloc, Detour_recalloc),
  DETOUR_ENTRY(strdup, Detour_strdup),

  // CRT internal variants
  DETOUR_ENTRY_MANGLED("_malloc_base", Real_malloc_base, Detour_malloc),
  DETOUR_ENTRY_MANGLED("_malloc_crt", Real_malloc_crt, Detour_malloc),
  DETOUR_ENTRY_MANGLED("_free_base", Real_free_base, Detour_free),
  DETOUR_ENTRY_MANGLED("_free_crt", Real_free_crt, Detour_free),
  DETOUR_ENTRY_MANGLED("_realloc_base", Real_realloc_base, Detour_realloc),
  DETOUR_ENTRY_MANGLED("_realloc_crt", Real_realloc_crt, Detour_realloc),
  DETOUR_ENTRY_MANGLED("_calloc_base", Real_calloc_base, Detour_calloc),
  DETOUR_ENTRY_MANGLED("_calloc_crt", Real_calloc_crt, Detour_calloc),

  // Debug CRT
  DETOUR_ENTRY_MANGLED("_malloc_dbg", Real_malloc_dbg, Detour_malloc_dbg),
  DETOUR_ENTRY_MANGLED("_free_dbg", Real_free_dbg, Detour_free_dbg),
  DETOUR_ENTRY_MANGLED("_realloc_dbg", Real_realloc_dbg, Detour_realloc_dbg),
  DETOUR_ENTRY_MANGLED("_calloc_dbg", Real_calloc_dbg, Detour_calloc_dbg),
  DETOUR_ENTRY_MANGLED("_msize_dbg", Real_msize_dbg, Detour_msize_dbg),
  DETOUR_ENTRY_MANGLED("_expand_dbg", Real_expand_dbg, Detour_expand_dbg),
  DETOUR_ENTRY_MANGLED("_recalloc_dbg", Real_recalloc_dbg, Detour_recalloc_dbg),

  // NOTE: Exit functions are NOT intercepted - they cause intermittent hangs
  // and are not needed for memory allocation replacement.

  // C++ operators - 64-bit
  DETOUR_ENTRY_MANGLED("??2@YAPEAX_K@Z", Real_new_64, Detour_malloc),          // operator new(size_t)
  DETOUR_ENTRY_MANGLED("??_U@YAPEAX_K@Z", Real_new_array_64, Detour_malloc),   // operator new[](size_t)
  DETOUR_ENTRY_MANGLED("??3@YAXPEAX@Z", Real_delete_64, Detour_free),          // operator delete(void*)
  DETOUR_ENTRY_MANGLED("??_V@YAXPEAX@Z", Real_delete_array_64, Detour_free),   // operator delete[](void*)

  // C++ operators - 32-bit
  DETOUR_ENTRY_MANGLED("??2@YAPAXI@Z", Real_new_32, Detour_malloc),            // operator new(unsigned int)
  DETOUR_ENTRY_MANGLED("??_U@YAPAXI@Z", Real_new_array_32, Detour_malloc),     // operator new[](unsigned int)
  DETOUR_ENTRY_MANGLED("??3@YAXPAX@Z", Real_delete_32, Detour_free),           // operator delete(void*)
  DETOUR_ENTRY_MANGLED("??_V@YAXPAX@Z", Real_delete_array_32, Detour_free),    // operator delete[](void*)
};

static DetourEntry g_KernelDetours[] = {
  // Windows Heap API
  DETOUR_ENTRY(HeapAlloc, Detour_HeapAlloc),
  DETOUR_ENTRY(HeapFree, Detour_HeapFree),
  DETOUR_ENTRY(HeapReAlloc, Detour_HeapReAlloc),
  DETOUR_ENTRY(HeapSize, Detour_HeapSize),
  DETOUR_ENTRY(HeapCompact, Detour_HeapCompact),
  DETOUR_ENTRY(HeapValidate, Detour_HeapValidate),
  DETOUR_ENTRY(HeapWalk, Detour_HeapWalk),
};

static DetourEntry g_NtdllDetours[] = {
  // RTL Heap API
  DETOUR_ENTRY(RtlAllocateHeap, Detour_RtlAllocateHeap),
  DETOUR_ENTRY(RtlFreeHeap, Detour_RtlFreeHeap),
  DETOUR_ENTRY(RtlSizeHeap, Detour_RtlSizeHeap),
};

//
// Install all detours
//
static bool InstallDetours() {
  // Get module handles
  HMODULE hKernel32 = GetModuleHandle(_T("kernel32.dll"));
  HMODULE hNtdll = GetModuleHandle(_T("ntdll.dll"));

  // Begin a Detours transaction
  DetourTransactionBegin();
  DetourUpdateThread(GetCurrentThread());

  bool anyAttached = false;

  // Enumerate all loaded modules and attach CRT detours
  HANDLE hProcess = GetCurrentProcess();
  DWORD cbNeeded;
  const DWORD MaxModules = 8192;
  HMODULE hMods[MaxModules];

  int modulesPatched = 0;
  int functionsAttached = 0;

  if (EnumProcessModules(hProcess, hMods, sizeof(hMods), &cbNeeded)) {
    for (DWORD i = 0; i < (cbNeeded / sizeof(HMODULE)); i++) {
      TCHAR szModName[MAX_PATH] = { 0 };
      if (GetModuleFileName(hMods[i], szModName, MAX_PATH)) {
        // Patch CRT and C++ runtime libraries
        if (!(_tcsstr(szModName, _T("CRT")) || _tcsstr(szModName, _T("crt")) ||
              _tcsstr(szModName, _T("ucrt")) || _tcsstr(szModName, _T("UCRT")) ||
              _tcsstr(szModName, _T("msvcr")) || _tcsstr(szModName, _T("MSVCR")) ||
              _tcsstr(szModName, _T("msvcp")) || _tcsstr(szModName, _T("MSVCP")) ||
              _tcsstr(szModName, _T("vcruntime")) || _tcsstr(szModName, _T("VCRUNTIME")))) {
          continue;
        }

        HMODULE hCRT = hMods[i];
        int moduleAttached = 0;
        for (size_t j = 0; j < sizeof(g_CRTDetours) / sizeof(g_CRTDetours[0]); j++) {
          if (AttachDetour(hCRT, &g_CRTDetours[j])) {
            anyAttached = true;
            moduleAttached++;
            functionsAttached++;
          }
        }
        if (moduleAttached > 0) {
          modulesPatched++;
#ifdef HOARD_DEBUG
          fprintf(stderr, "Hoard: Patched %d functions in %ls\n", moduleAttached, szModName);
#endif
        }
      }
    }
  }

#ifdef HOARD_DEBUG
  fprintf(stderr, "Hoard: Total %d modules patched, %d functions attached\n", modulesPatched, functionsAttached);
#endif

  // NOTE: We intentionally do NOT hook kernel32 (HeapAlloc/HeapFree) or
  // ntdll (RtlAllocateHeap/RtlFreeHeap) functions. These low-level Windows
  // heap functions are used internally by the CRT and Windows during DLL
  // unload. Hooking them causes crashes at process exit because:
  // 1. When hoard.dll unloads, the trampolines become invalid
  // 2. Other DLLs may still have cached pointers to the trampolines
  // 3. The cleanup code in ntdll/kernel32 tries to free memory using
  //    the now-invalid trampolines, causing a segfault
  //
  // Hooking at the CRT level (malloc/free/new/delete) is sufficient for
  // user code - the CRT's internal use of HeapAlloc will use the original
  // Windows heap, which is fine.

  // Commit the transaction
  LONG error = DetourTransactionCommit();
  if (error != NO_ERROR) {
    // Transaction failed, abort
    DetourTransactionAbort();
    return false;
  }

  return anyAttached;
}

//
// Remove all detours
//
static void RemoveDetours() {
  DetourTransactionBegin();
  DetourUpdateThread(GetCurrentThread());

  // Detach all CRT detours
  for (size_t i = 0; i < sizeof(g_CRTDetours) / sizeof(g_CRTDetours[0]); i++) {
    DetachDetour(&g_CRTDetours[i]);
  }

  // NOTE: We don't attach kernel32/ntdll detours anymore (see InstallDetours),
  // so no need to detach them here.

  DetourTransactionCommit();
}

//
// Public API called from wintls.cpp
//

extern "C" void InitializeWinWrapper() {
  // CRITICAL: Must call DetourRestoreAfterWith for withdll.exe injection to work
  DetourRestoreAfterWith();

  // Allocate (and leak) something from the old Windows heap first.
  // This ensures the Windows heap is initialized before we take over.
  HeapAlloc(GetProcessHeap(), 0, 1);

  // Install all detours
  bool success = InstallDetours();

  // Always print diagnostic - user needs to know if interposition worked
  // Use multiple output methods to ensure visibility:
  // 1. stdout (most programs have this)
  // 2. OutputDebugString (visible in debugger/DebugView)
  const char * msg = success ? "Hoard: Memory allocator active\n" : "Hoard: FAILED TO INITIALIZE\n";
  printf("%s", msg);
  fflush(stdout);

  char debugMsg[256];
  snprintf(debugMsg, sizeof(debugMsg), "%s", msg);
  OutputDebugStringA(debugMsg);
}

extern "C" void FinalizeWinWrapper() {
  // Don't try to remove detours during process exit - it can cause issues.
  // Just let the process exit normally; the OS will clean up.
  // Note: We explicitly do NOT call TerminateProcess here because it
  // prevents stdout from being flushed and cleanup code from running.
}

//
// DLL export for reference (prevents linker from stripping)
//
extern "C" __declspec(dllexport) int ReferenceWinWrapperStub;
