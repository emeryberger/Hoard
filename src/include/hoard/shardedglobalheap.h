// -*- C++ -*-

/*

  The Hoard Multiprocessor Memory Allocator
  www.hoard.org

  Author: Emery Berger, http://www.emeryberger.com
  Copyright (c) 1998-2020 Emery Berger

  See the LICENSE file at the top-level directory of this
  distribution and at http://github.com/emeryberger/Hoard.

*/

#ifndef HOARD_SHARDEDGLOBALHEAP_H
#define HOARD_SHARDEDGLOBALHEAP_H

#include <atomic>
#include <cstdlib>

#include "hoardsuperblock.h"
#include "processheap.h"
#include "hoardconstants.h"
#include "heaplayers.h"

namespace Hoard {

  template <size_t SuperblockSize,
	    template <class LockType_,
		      int SuperblockSize_,
		      typename HeapType_> class Header_,
	    int EmptinessClasses,
	    class MmapSource,
	    class LockType>
  class ShardedGlobalHeap {

    class bogusThresholdFunctionClass {
    public:
      static inline bool function (unsigned int, unsigned int, size_t) {
	return false;
      }
    };

  public:

    typedef ProcessHeap<SuperblockSize, Header_, EmptinessClasses, LockType, bogusThresholdFunctionClass, MmapSource> SuperHeap;
    typedef HoardSuperblock<LockType, SuperblockSize, ShardedGlobalHeap, Header_> SuperblockType;

    // Number of shards - enough to reduce contention but not too many
    enum { NumShards = 64 };

    /// Bytes of empty superblocks currently held UNPURGED (the retain cache).
    static inline std::atomic<size_t> _retainedBytes { 0 };

    /// Retain-cache budget, in bytes. Overridable with HOARD_RETAIN_MB
    /// (0 disables the cache, restoring purge-every-empty-superblock).
    static size_t retainBudgetBytes() {
      static const size_t budget = [] () -> size_t {
        // Default: 64MB. Big enough that ordinary request-scoped churn is
        // served from resident pages; small enough that a program which frees
        // a large working set still hands nearly all of it back.
        size_t mb = 64;
        if (const char * e = getenv ("HOARD_RETAIN_MB")) {
          char * end = nullptr;
          auto v = strtoul (e, &end, 10);
          if (end && *end == '\0') {
            mb = (size_t) v;
          }
        }
        return mb * 1048576UL;
      }();
      return budget;
    }

    /// A superblock is leaving the global heap: if it was held unpurged, it no
    /// longer counts against the retain budget.
    static void releaseFromRetainCache (SuperblockType * s) {
      if (s && s->isRetainedUnpurged()) {
        s->setRetainedUnpurged (false);
        _retainedBytes.fetch_sub (SuperblockSize, std::memory_order_relaxed);
      }
    }

    ShardedGlobalHeap() {
      for (int i = 0; i < NumShards; i++) {
        _shards[i] = getShardHeap(i);
      }
    }

    // put() always operates on the caller's local shard - no stealing, no contention
    void put (void * s, size_t sz) {
      assert (s);
      auto * sb = (SuperblockType *) s;
      assert (sb->isValidSuperblock());

      // If the superblock is completely empty, discard its data pages
      // (MADV_FREE and equivalents) so the OS can reclaim the physical
      // memory while it sits in the global heap. The virtual mapping,
      // header, and reuse path are unchanged, so this does not affect
      // Hoard's bounds; it only lowers resident memory. clear() first
      // resets the freelist into pure-reap (bump pointer) mode so no
      // allocator metadata lives in the purged region. No thread can
      // legitimately free into a fully-empty superblock concurrently,
      // and TLAB-cached objects count as live, so this is race-free.
      sb->setRetainedUnpurged (false);

      if (sb->getObjectsFree() == sb->getTotalObjects()) {
        sb->clear();
        // Retain cache: keep a bounded amount of empty superblocks WITHOUT
        // purging, so a workload that cycles memory reuses resident pages
        // instead of faulting them back in. Purging every empty superblock
        // costs ~27% on cyclic bulk reuse, because the pages we discard are
        // exactly the ones about to be reallocated.
        //
        // Beyond the budget we still purge, so a program that frees a large
        // working set still returns nearly all of it to the OS -- which is the
        // point of purge-on-empty, and where Hoard is far better than
        // allocators that simply hoard pages.
        if (retainBudgetBytes() > 0 &&
            _retainedBytes.fetch_add (SuperblockSize,
                                      std::memory_order_relaxed)
              + SuperblockSize <= retainBudgetBytes()) {
          sb->setRetainedUnpurged (true);
        } else {
          _retainedBytes.fetch_sub (SuperblockSize, std::memory_order_relaxed);
          sb->purgeData();
        }
      }

      int shard = getThreadShard();
      _shards[shard]->put((typename SuperHeap::SuperblockType *) s, sz);
    }

    // get() tries local shard first, then steals using power-of-two choices
    SuperblockType * get (size_t sz, void * dest) {
      int localShard = getThreadShard();

      // Fast path: try local shard first (likely no contention)
      auto * s = reinterpret_cast<SuperblockType *>(
        _shards[localShard]->get(sz, reinterpret_cast<SuperHeap *>(dest)));
      if (s) {
        assert(s->isValidSuperblock());
        releaseFromRetainCache (s);
        return s;
      }

      // Slow path: steal from another shard using power-of-two choices
      auto * stolen = stealSuperblock(sz, dest, localShard);
      releaseFromRetainCache (stolen);
      return stolen;
    }

  private:

    // Get the shard index for the current thread
    static int getThreadShard() {
      return (int)(HL::CPUInfo::getThreadId() % NumShards);
    }

    // Simple deterministic victim selection based on thread ID and attempt
    // Avoids thread_local which can cause issues during early init
    static unsigned int selectVictim(int attempt) {
      unsigned int tid = (unsigned int)HL::CPUInfo::getThreadId();
      // Mix thread ID with attempt number using xorshift-like mixing
      unsigned int x = tid * 2654435761u + (unsigned int)attempt * 1103515245u;
      x ^= x >> 16;
      return x;
    }

    // Steal a superblock using power-of-two random choices
    SuperblockType * stealSuperblock(size_t sz, void * dest, int localShard) {
      unsigned int r = selectVictim(0);
      int victim1 = (int)(r % NumShards);
      int victim2 = (int)(selectVictim(1) % NumShards);

      // Avoid local shard (we already tried it)
      if (victim1 == localShard) victim1 = (victim1 + 1) % NumShards;
      if (victim2 == localShard) victim2 = (victim2 + 1) % NumShards;
      if (victim2 == victim1) victim2 = (victim2 + 1) % NumShards;
      if (victim2 == localShard) victim2 = (victim2 + 1) % NumShards;

      // Try victim1 first
      auto * s = reinterpret_cast<SuperblockType *>(
        _shards[victim1]->get(sz, reinterpret_cast<SuperHeap *>(dest)));
      if (s) {
        assert(s->isValidSuperblock());
        return s;
      }

      // Try victim2
      s = reinterpret_cast<SuperblockType *>(
        _shards[victim2]->get(sz, reinterpret_cast<SuperHeap *>(dest)));
      if (s) {
        assert(s->isValidSuperblock());
        return s;
      }

      // Last resort: scan remaining shards (rare case)
      for (int i = 0; i < NumShards; i++) {
        if (i == localShard || i == victim1 || i == victim2) continue;
        s = reinterpret_cast<SuperblockType *>(
          _shards[i]->get(sz, reinterpret_cast<SuperHeap *>(dest)));
        if (s) {
          assert(s->isValidSuperblock());
          return s;
        }
      }

      return nullptr;
    }

    SuperHeap * _shards[NumShards];

    static SuperHeap * getShardHeap(int index) {
      // alignas: placement-new into an under-aligned buffer is UB (see
      // getMainHoardHeap).
      alignas(SuperHeap)
      static char shardBufs[NumShards][sizeof(SuperHeap)];
      static SuperHeap * shardHeaps[NumShards] = { nullptr };
      static LockType initLock;

      if (!shardHeaps[index]) {
        std::lock_guard<LockType> g(initLock);
        if (!shardHeaps[index]) {
          shardHeaps[index] = new (&shardBufs[index][0]) SuperHeap;
        }
      }
      return shardHeaps[index];
    }

    ShardedGlobalHeap (const ShardedGlobalHeap&);
    ShardedGlobalHeap& operator=(const ShardedGlobalHeap&);

  };

}

#endif
