/* -*- C++ -*- */

/*

  Heap Layers: An Extensible Memory Allocation Infrastructure
  
  Copyright (C) 2000-2012 by Emery Berger
  http://www.cs.umass.edu/~emery
  emery@cs.umass.edu
  
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.
  
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.
  
  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

*/

/*
 * @file   wrapper.cpp
 * @brief  Replaces malloc with appropriate calls to TheCustomHeapType.
 * @author Emery Berger <http://www.cs.umass.edu/~emery>
 */


#pragma once

#include <string.h> // for memcpy and memset
#include <stdlib.h> // size_t

extern "C" {

  void * xxmalloc (size_t);
  void   xxfree (void *);

  // Takes a pointer and returns how much space it holds.
  size_t xxmalloc_usable_size (void *);

  // Locks the heap(s), used prior to any invocation of fork().
  void xxmalloc_lock (void);

  // Unlocks the heap(s), after fork().
  void xxmalloc_unlock (void);

}

#if defined(__APPLE__)
#include <malloc/malloc.h>
#elif defined(__FreeBSD__)
#include <stdlib.h>
#else
#include <malloc.h> // for memalign
#endif

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
// Disable warnings about long (> 255 chars) identifiers.
#pragma warning(disable:4786)
// Set inlining to the maximum possible depth.
#pragma inline_depth(255)
#pragma warning(disable: 4074)	// initializers put in compiler reserved area

//#pragma init_seg(compiler)

// #pragma comment(linker, "/merge:.CRT=.data")

#pragma comment(linker, "/disallowlib:libc.lib")
#pragma comment(linker, "/disallowlib:libcd.lib")
#pragma comment(linker, "/disallowlib:libcmt.lib")
#pragma comment(linker, "/disallowlib:libcmtd.lib")
#pragma comment(linker, "/disallowlib:msvcrtd.lib")

#else
#include <errno.h>
#endif

#ifndef CUSTOM_PREFIX
#define CUSTOM_PREFIX(x) x
#endif

#define CUSTOM_MALLOC(x)     CUSTOM_PREFIX(malloc)(x)
#define CUSTOM_FREE(x)       CUSTOM_PREFIX(free)(x)
#define CUSTOM_REALLOC(x,y)  CUSTOM_PREFIX(realloc)(x,y)
#define CUSTOM_CALLOC(x,y)   CUSTOM_PREFIX(calloc)(x,y)
#define CUSTOM_MEMALIGN(x,y) CUSTOM_PREFIX(memalign)(x,y)
#define CUSTOM_GETSIZE(x)    CUSTOM_PREFIX(malloc_usable_size)(x)
#define CUSTOM_MALLOPT(x,y)  CUSTOM_PREFIX(mallopt)(x,y)
#define CUSTOM_VALLOC(x)     CUSTOM_PREFIX(valloc)(x)
#define CUSTOM_PVALLOC(x)    CUSTOM_PREFIX(pvalloc)(x)
#define CUSTOM_RECALLOC(x,y,z)   CUSTOM_PREFIX(recalloc)(x,y,z)
#define CUSTOM_STRNDUP(s,sz) CUSTOM_PREFIX(strndup)(s,sz)
#define CUSTOM_STRDUP(s)     CUSTOM_PREFIX(strdup)(s)
#define CUSTOM_GETCWD(b,s)   CUSTOM_PREFIX(getcwd)(b,s)
#define CUSTOM_GETENV(s)     CUSTOM_PREFIX(getenv)(s)
#define CUSTOM_PUTENV(s)     CUSTOM_PREFIX(_putenv)(s)

#if defined(_WIN32)
#define MYCDECL __cdecl
#if !defined(NO_INLINE)
#define NO_INLINE __declspec(noinline)
#endif
#pragma inline_depth(255)

#if !defined(NDEBUG)
#define __forceinline inline
#endif

#else
#define MYCDECL
#endif

/***** generic malloc functions *****/

extern "C" void * MYCDECL CUSTOM_MALLOC(size_t sz)
{
  void * ptr = xxmalloc(sz);
  return ptr;
}

extern "C" void * MYCDECL CUSTOM_CALLOC(size_t nelem, size_t elsize)
{
  size_t n = nelem * elsize;
  void * ptr = CUSTOM_MALLOC(n);
  // Zero out the malloc'd block.
  if (ptr != NULL) {
    memset (ptr, 0, n);
  }
  return ptr;
}


#if !defined(_WIN32)
extern "C" void * MYCDECL CUSTOM_MEMALIGN (size_t alignment, size_t size);

extern "C" int posix_memalign (void **memptr, size_t alignment, size_t size)
{
  // Check for non power-of-two alignment.
  if ((alignment == 0) ||
      (alignment & (alignment - 1)))
    {
      return EINVAL;
    }
  void * ptr = CUSTOM_MEMALIGN (alignment, size);
  if (!ptr) {
    return ENOMEM;
  } else {
    *memptr = ptr;
    return 0;
  }
}
#endif


extern "C" void * MYCDECL CUSTOM_MEMALIGN (size_t alignment, size_t size)
{
  // NOTE: This function is deprecated.
  if (alignment == sizeof(double)) {
    return CUSTOM_MALLOC (size);
  } else {
    void * ptr = CUSTOM_MALLOC (size + 2 * alignment);
    void * alignedPtr = (void *) (((size_t) ptr + alignment - 1) & ~(alignment - 1));
    return alignedPtr;
  }
}

extern "C" size_t MYCDECL CUSTOM_GETSIZE (void * ptr)
{
  return xxmalloc_usable_size (ptr);
}

extern "C" void MYCDECL CUSTOM_FREE (void * ptr)
{
  xxfree (ptr);
}

extern "C" void * MYCDECL CUSTOM_REALLOC (void * ptr, size_t sz)
{
  if (ptr == NULL) {
    ptr = CUSTOM_MALLOC (sz);
    return ptr;
  }
  if (sz == 0) {
    CUSTOM_FREE (ptr);
    return NULL;
  }

  size_t objSize = CUSTOM_GETSIZE (ptr);

  void * buf = CUSTOM_MALLOC(sz);

  if (buf != NULL) {
    if (objSize == CUSTOM_GETSIZE(buf)) {
      // The objects are the same actual size.
      // Free the new object and return the original.
      CUSTOM_FREE (buf);
      return ptr;
    }
    // Copy the contents of the original object
    // up to the size of the new block.
    size_t minSize = (objSize < sz) ? objSize : sz;
    memcpy (buf, ptr, minSize);
  }

  // Free the old block.
  CUSTOM_FREE (ptr);

  // Return a pointer to the new one.
  return buf;
}

#if defined(linux)

extern "C" char * MYCDECL CUSTOM_STRNDUP(const char * s, size_t sz)
{
  char * newString = NULL;
  if (s != NULL) {
    size_t cappedLength = strnlen (s, sz);
    if ((newString = (char *) CUSTOM_MALLOC(cappedLength + 1))) {
      strncpy(newString, s, cappedLength);
      newString[cappedLength] = '\0';
    }
  }
  return newString;
}
#endif

extern "C" char * MYCDECL CUSTOM_STRDUP(const char * s)
{
  char * newString = NULL;
  if (s != NULL) {
    if ((newString = (char *) CUSTOM_MALLOC(strlen(s) + 1))) {
      strcpy(newString, s);
    }
  }
  return newString;
}

#if !defined(_WIN32)
#include <dlfcn.h>
#include <limits.h>

#if !defined(RTLD_NEXT)
#define RTLD_NEXT ((void *) -1)
#endif


typedef char * getcwdFunction (char *, size_t);


extern "C"  char * MYCDECL CUSTOM_GETCWD(char * buf, size_t size)
{
  static getcwdFunction * real_getcwd
    = reinterpret_cast<getcwdFunction *>
    (reinterpret_cast<intptr_t>(dlsym (RTLD_NEXT, "getcwd")));
  
  if (!buf) {
    if (size == 0) {
      size = PATH_MAX;
    }
    buf = (char *) CUSTOM_MALLOC(size);
  }
  return (real_getcwd)(buf, size);
}

#endif


#if defined(__SVR4)
// Apparently we no longer need to replace new and friends for Solaris.
#define NEW_INCLUDED
#endif


#ifndef NEW_INCLUDED
#define NEW_INCLUDED

void * operator new (size_t sz)
#if defined(__APPLE__)
  throw (std::bad_alloc)
#endif
{
  void * ptr = CUSTOM_MALLOC (sz);
  if (ptr == NULL) {
    throw std::bad_alloc();
  } else {
    return ptr;
  }
}

void operator delete (void * ptr)
#if defined(__APPLE__)
  throw ()
#endif
{
  CUSTOM_FREE (ptr);
}

#if !defined(__SUNPRO_CC) || __SUNPRO_CC > 0x420
void * operator new (size_t sz, const std::nothrow_t&) throw() {
  return CUSTOM_MALLOC(sz);
} 

void * operator new[] (size_t size) 
#if defined(__APPLE__)
  throw (std::bad_alloc)
#endif
{
  void * ptr = CUSTOM_MALLOC(size);
  if (ptr == NULL) {
    throw std::bad_alloc();
  } else {
    return ptr;
  }
}

void * operator new[] (size_t sz, const std::nothrow_t&)
  throw()
 {
  return CUSTOM_MALLOC(sz);
} 

void operator delete[] (void * ptr)
#if defined(__APPLE__)
  throw ()
#endif
{
  CUSTOM_FREE (ptr);
}

#endif
#endif

/***** replacement functions for GNU libc extensions to malloc *****/

// A stub function to ensure that we capture mallopt.
// It does nothing and always returns a failure value (0).
extern "C" int MYCDECL CUSTOM_MALLOPT (int /* number */, int /* value */)
{
  // Always fail.
  return 0;
}

// NOTE: for convenience, we assume page size = 8192.

extern "C" void * MYCDECL CUSTOM_VALLOC (size_t sz)
{
  return CUSTOM_MEMALIGN (8192, sz);
}


extern "C" void * MYCDECL CUSTOM_PVALLOC (size_t sz)
{
  // Rounds up to the next pagesize and then calls valloc. Hoard
  // doesn't support aligned memory requests.
  return CUSTOM_VALLOC ((sz + 8191) & ~8191);
}

// The wacky recalloc function, for Windows.
extern "C" void * MYCDECL CUSTOM_RECALLOC (void * p, size_t num, size_t sz)
{
  void * ptr = CUSTOM_REALLOC (p, num * sz);
  if ((p == NULL) && (ptr != NULL)) {
    // Clear out the memory.
    memset (ptr, 0, CUSTOM_GETSIZE(ptr));
  }
  return ptr;
}

#if defined(_WIN32)

/////// Other replacement functions that call malloc.

// from http://msdn2.microsoft.com/en-us/library/6ewkz86d(VS.80).aspx
// fgetc, _fgetchar, fgets, fprintf, fputc, _fputchar, fputs, fread, fscanf, fseek, fsetpos
// _fullpath, fwrite, getc, getchar, _getcwd, _getdcwd, gets, _getw, _popen, printf, putc
// putchar, _putenv, puts, _putw, scanf, _searchenv, setvbuf, _strdup, system, _tempnam,
// ungetc, vfprintf, vprintf


char * CUSTOM_GETENV(const char * str) {
  char buf[32767];
  int len = GetEnvironmentVariable (str, buf, 32767);
  if (len == 0) {
    return NULL;
  } else {
    char * str = new char[len + 1];
    strncpy (str, buf, len + 1);
    return str;
  }
}

int CUSTOM_PUTENV(char * str) {
  char * eqpos = strchr (str, '=');
  if (eqpos != NULL) {
    char first[32767], second[32767];
    int namelen = (size_t) eqpos - (size_t) str;
    strncpy (first, str, namelen);
    first[namelen] = '\0';
    int valuelen = strlen (eqpos + 1);
    strncpy (second, eqpos + 1, valuelen);
    second[valuelen] = '\0';
    char buf[255];
    sprintf (buf, "setting %s to %s\n", first, second);
    printf (buf);
    SetEnvironmentVariable (first, second);
    return 0;
  }
  return -1;
}

#endif
