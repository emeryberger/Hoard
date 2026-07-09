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
      if (sb->getObjectsFree() == sb->getTotalObjects()) {
        sb->clear();
        sb->purgeData();
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
        return s;
      }

      // Slow path: steal from another shard using power-of-two choices
      return stealSuperblock(sz, dest, localShard);
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
      static double shardBufs[NumShards][(sizeof(SuperHeap) / sizeof(double)) + 1];
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
