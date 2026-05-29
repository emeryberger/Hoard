// -*- C++ -*-

/*
  The Hoard Multiprocessor Memory Allocator
  www.hoard.org

  Author: Emery Berger, http://www.emeryberger.com
  Copyright (c) 1998-2020 Emery Berger
*/

#ifndef HOARD_REMOTEFREEQUEUE_H
#define HOARD_REMOTEFREEQUEUE_H

#include <atomic>
#include <cstddef>

namespace Hoard {

  /**
   * @class RemoteFreeQueue
   * @brief Lock-free MPSC queue for cross-thread frees (mailbox pattern).
   *
   * Remote threads push freed pointers to this queue. The owning thread
   * drains the queue during malloc, processing all pending frees at once.
   * This is more efficient than per-superblock delayed queues because:
   * 1. Single collection point per heap (no iteration over superblocks)
   * 2. Better cache locality when draining
   * 3. Bounded drain cost: O(k) for k pending frees, not O(n) superblocks
   *
   * The entry is intrusive - we use the first word of the freed object
   * to store the next pointer. This requires minimum allocation size >= 8.
   */
  class RemoteFreeQueue {
  public:
    struct Entry {
      Entry* next;  // Non-atomic for simplicity; only read after popAll
    };

    RemoteFreeQueue() : _head(nullptr) {}

    /**
     * @brief Push a pointer to the queue (lock-free, multiple producers).
     * @param ptr Pointer to free.
     *
     * Uses the object's memory to store the queue entry (intrusive).
     * The first sizeof(Entry*) bytes of the freed object are repurposed.
     */
    void push(void* ptr) {
      auto* entry = reinterpret_cast<Entry*>(ptr);
      Entry* oldHead = _head.load(std::memory_order_relaxed);
      do {
        entry->next = oldHead;
      } while (!_head.compare_exchange_weak(oldHead, entry,
                                            std::memory_order_release,
                                            std::memory_order_relaxed));
    }

    /**
     * @brief Pop all entries atomically (single consumer).
     * @return Head of the list, or nullptr if empty.
     *
     * Returns the entire list for batch processing. Caller iterates
     * via entry->next until nullptr.
     */
    Entry* popAll() {
      return _head.exchange(nullptr, std::memory_order_acquire);
    }

    /**
     * @brief Check if queue is likely empty (relaxed, may have false negatives).
     */
    bool isEmpty() const {
      return _head.load(std::memory_order_relaxed) == nullptr;
    }

  private:
    std::atomic<Entry*> _head;
  };

}

#endif
