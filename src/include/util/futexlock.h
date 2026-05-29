// -*- C++ -*-

/*
  The Hoard Multiprocessor Memory Allocator
  www.hoard.org

  Author: Emery Berger, http://www.emeryberger.com
  Copyright (c) 1998-2020 Emery Berger
*/

#ifndef HOARD_FUTEXLOCK_H
#define HOARD_FUTEXLOCK_H

#if defined(__linux__)

#include <atomic>
#include <linux/futex.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <climits>

namespace HL {

/**
 * @class FutexLockType
 * @brief Fast userspace lock using Linux futex.
 *
 * Uses a three-state lock:
 *   0 = unlocked
 *   1 = locked, no waiters
 *   2 = locked, with waiters
 *
 * This provides:
 *   - Fast uncontended path (single atomic exchange)
 *   - Efficient waiting under contention (kernel-assisted sleep)
 *   - Reduced syscall overhead vs pure pthread_mutex
 */
class FutexLockType {
public:
  FutexLockType() : _state(0) {}

  inline void lock() {
    // Fast path: try to acquire if unlocked
    int expected = 0;
    if (_state.compare_exchange_strong(expected, 1,
                                       std::memory_order_acquire,
                                       std::memory_order_relaxed)) {
      return; // Got the lock
    }

    // Slow path: lock is held
    lockContended();
  }

  inline bool didLock() {
    int expected = 0;
    return _state.compare_exchange_strong(expected, 1,
                                          std::memory_order_acquire,
                                          std::memory_order_relaxed);
  }

  inline void unlock() {
    // Fast path: if state is 1, just set to 0
    int prev = _state.exchange(0, std::memory_order_release);
    if (prev == 2) {
      // There were waiters - wake one
      syscall(SYS_futex, &_state, FUTEX_WAKE_PRIVATE, 1, nullptr, nullptr, 0);
    }
  }

private:
  void lockContended() {
    // Spin briefly before sleeping
    const int MAX_SPIN = 100;
    for (int i = 0; i < MAX_SPIN; i++) {
      int expected = 0;
      if (_state.compare_exchange_strong(expected, 1,
                                         std::memory_order_acquire,
                                         std::memory_order_relaxed)) {
        return;
      }
      // Pause to reduce cache line bouncing
      #if defined(__x86_64__) || defined(__i386__)
      __asm__ volatile("pause" ::: "memory");
      #elif defined(__aarch64__)
      __asm__ volatile("yield" ::: "memory");
      #endif
    }

    // Spin failed - prepare to sleep
    int c;
    // If state is 1, try to set to 2 (indicating waiters)
    // If state is 2, we'll wait on it
    while ((c = _state.exchange(2, std::memory_order_acquire)) != 0) {
      // Sleep until state changes from 2
      syscall(SYS_futex, &_state, FUTEX_WAIT_PRIVATE, 2, nullptr, nullptr, 0);
    }
  }

  std::atomic<int> _state;
};

} // namespace HL

#endif // __linux__

#endif // HOARD_FUTEXLOCK_H
