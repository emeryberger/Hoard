// -*- C++ -*-

/*

  The Hoard Multiprocessor Memory Allocator
  www.hoard.org

  Author: Emery Berger, http://www.emeryberger.com
  Copyright (c) 1998-2020 Emery Berger

  See the LICENSE file at the top-level directory of this
  distribution and at http://github.com/emeryberger/Hoard.

*/

#ifndef HOARD_ATOMICFREELIST_H
#define HOARD_ATOMICFREELIST_H

#include <atomic>
#include <cstddef>

namespace Hoard {

/**
 * @class AtomicFreeList
 * @brief Lock-free MPSC (Multiple Producer, Single Consumer) queue for
 *        delayed cross-thread frees.
 *
 * Inspired by mimalloc's thread_delayed_free mechanism. Multiple producer
 * threads can push concurrently using atomic CAS. The single consumer
 * (owner thread) drains the entire list atomically during malloc.
 *
 * Uses intrusive list design - stores next pointer in freed object's memory,
 * so no additional allocation is needed.
 *
 * Memory ordering:
 * - push(): release on CAS success ensures next pointer visible before head update
 * - popAll(): acquire ensures all next pointers visible to consumer
 * - isEmpty(): relaxed, approximate check for fast path (false negatives OK)
 */
class AtomicFreeList {
public:

    /**
     * @brief Entry structure stored in freed object's memory.
     * Uses atomic next pointer for lock-free traversal.
     */
    struct Entry {
        std::atomic<Entry*> next;
    };

    AtomicFreeList() : _head(nullptr) {}

    /**
     * @brief Push an item to the list (lock-free, multiple producers safe).
     * @param ptr Pointer to freed object (must have space for Entry).
     *
     * Uses compare-and-swap loop to atomically prepend to list.
     * Memory order: release on success ensures entry->next is visible
     * before _head update is visible to other threads.
     */
    inline void push(void* ptr) {
        Entry* entry = reinterpret_cast<Entry*>(ptr);
        Entry* old_head = _head.load(std::memory_order_relaxed);
        do {
            // Store current head as our next (will be validated by CAS)
            entry->next.store(old_head, std::memory_order_relaxed);
        } while (!_head.compare_exchange_weak(
                    old_head, entry,
                    std::memory_order_release,  // Success: release
                    std::memory_order_relaxed   // Failure: relaxed (just retry)
                ));
    }

    /**
     * @brief Atomically drain entire list (single consumer only).
     * @return Head of drained list, or nullptr if empty.
     *
     * Swaps head with nullptr, returning the old list.
     * Memory order: acquire ensures all next pointers written by
     * producers are visible to the consumer.
     */
    inline Entry* popAll() {
        return _head.exchange(nullptr, std::memory_order_acquire);
    }

    /**
     * @brief Check if queue is empty (approximate, for fast path).
     * @return true if likely empty, false if items pending.
     *
     * Uses relaxed ordering - may have false negatives (items pushed
     * but not yet visible), but that's acceptable for optimization.
     */
    inline bool isEmpty() const {
        return _head.load(std::memory_order_relaxed) == nullptr;
    }

private:
    /// Head of the intrusive linked list (atomic for lock-free access)
    std::atomic<Entry*> _head;
};

} // namespace Hoard

#endif // HOARD_ATOMICFREELIST_H
