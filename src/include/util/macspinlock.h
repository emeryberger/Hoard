/* -*- C++ -*- */

/*
  Heap Layers: An Extensible Memory Allocation Infrastructure

  Copyright (C) 2000-2024 by Emery Berger
  http://www.emeryberger.com
  emery@cs.umass.edu

  Apache 2.0 License
*/

#ifndef HL_MACSPINLOCK_H
#define HL_MACSPINLOCK_H

#if defined(__APPLE__)

#include <os/lock.h>
#include <sched.h>

namespace HL {

  /**
   * @class MacSpinLockType
   * @brief A hybrid lock for macOS that spins briefly before falling back to os_unfair_lock.
   *
   * os_unfair_lock enters kernel wait quickly under contention, which causes
   * massive overhead on workloads with frequent cross-thread operations (like
   * Larson). This lock spins with trylock for a configurable number of attempts
   * before falling through to the blocking path, reducing syscall overhead for
   * short critical sections.
   */
  class MacSpinLockType {
  public:
    MacSpinLockType() : mutex(OS_UNFAIR_LOCK_INIT) {}

    ~MacSpinLockType() {
      mutex = OS_UNFAIR_LOCK_INIT;
    }

    inline void lock() {
      // Fast path: try to acquire immediately
      if (os_unfair_lock_trylock(&mutex)) {
        return;
      }
      // Slow path: spin briefly before blocking
      contendedLock();
    }

    inline void unlock() {
      os_unfair_lock_unlock(&mutex);
    }

  private:
    void contendedLock() {
      // Spin with exponential backoff before falling back to blocking lock.
      // On Apple Silicon, the M2 Pro has 12 cores, so moderate spinning is beneficial.
      constexpr int MAX_SPINS = 100;

      for (int i = 0; i < MAX_SPINS; ++i) {
        // Pause hint to reduce power consumption while spinning
        #if defined(__aarch64__)
        __asm__ volatile("yield" ::: "memory");
        #else
        __asm__ volatile("pause" ::: "memory");
        #endif

        if (os_unfair_lock_trylock(&mutex)) {
          return;
        }
      }

      // Fall back to blocking lock
      os_unfair_lock_lock(&mutex);
    }

    os_unfair_lock mutex;
  };

}

#endif // __APPLE__

#endif // HL_MACSPINLOCK_H
