// -*- C++ -*-

/**
 * @file   gnuwrapper.cpp
 * @brief  Replaces malloc family on GNU/Linux with custom versions.
 * @author Emery Berger <http://www.cs.umass.edu/~emery>
 * @note   Copyright (C) 2010 by Emery Berger, University of Massachusetts Amherst.
 */


#ifndef __GNUC__
#error "This file requires the GNU compiler."
#endif

#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <malloc.h>
#include <new>
#include <pthread.h>

#include "heaplayers.h"

/*
  To use this library,
  you only need to define the following allocation functions:
  
  - xxmalloc
  - xxfree
  - xxmalloc_usable_size
  - xxmalloc_lock
  - xxmalloc_unlock

  See the extern "C" block below for function prototypes and more
  details. YOU SHOULD NOT NEED TO MODIFY ANY OF THE CODE HERE TO
  SUPPORT ANY ALLOCATOR.


  LIMITATIONS:

  - This wrapper assumes that the underlying allocator will do "the
    right thing" when xxfree() is called with a pointer internal to an
    allocated object. Header-based allocators, for example, need not
    apply.

*/

#ifndef __THROW
#define __THROW
#endif

static bool initialized = false;

//#include "wrapper.cpp"

extern "C" {

  void * xxmalloc (size_t);
  void   xxfree (void *);
  size_t xxmalloc_usable_size (void *);
  void   xxmalloc_lock (void);
  void   xxmalloc_unlock (void);

  static void my_init_hook (void);

  // New hooks for allocation functions.
  static void * my_malloc_hook (size_t, const void *);
  static void   my_free_hook (void *, const void *);
  static void * my_realloc_hook (void *, size_t, const void *);
  static void * my_memalign_hook (size_t, size_t, const void *);

  // Store the old hooks just in case.
  static void * (*old_malloc_hook) (size_t, const void *);
  static void   (*old_free_hook) (void *, const void *);
  static void * (*old_realloc_hook)(void *ptr, size_t size, const void *caller);
  static void * (*old_memalign_hook)(size_t alignment, size_t size, const void *caller);

// From GNU libc 2.14 this macro is defined, to declare
// hook variables as volatile. Define it as empty for
// older glibc versions
#ifndef __MALLOC_HOOK_VOLATILE
 #define __MALLOC_HOOK_VOLATILE
#endif

  void (*__MALLOC_HOOK_VOLATILE __malloc_initialize_hook) (void) = my_init_hook;

#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
  
  static void my_init_hook (void) {
    if (!initialized) {
      // Store the old hooks.
      old_malloc_hook = __malloc_hook;
      old_free_hook = __free_hook;
      old_realloc_hook = __realloc_hook;
      old_memalign_hook = __memalign_hook;
      
      // Point the hooks to the replacement functions.
      __malloc_hook = my_malloc_hook;
      __free_hook = my_free_hook;
      __realloc_hook = my_realloc_hook;
      __memalign_hook = my_memalign_hook;

      // Set up everything so that fork behaves properly.
      pthread_atfork(xxmalloc_lock, xxmalloc_unlock, xxmalloc_unlock);

      initialized = true;

    }

  }

  static void * my_malloc_hook (size_t size, const void *) {
    return xxmalloc(size);
  }

  static void my_free_hook (void * ptr, const void *) {
    xxfree(ptr);
  }

  static void * my_realloc_hook (void * ptr, size_t sz, const void *) {
    return CUSTOM_PREFIX(realloc)(ptr, sz);
  }

  static void * my_memalign_hook (size_t size, size_t alignment, const void *) {
    return CUSTOM_PREFIX(memalign)(size, alignment);
  }

}
