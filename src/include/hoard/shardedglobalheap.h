// -*- C++ -*-

/*

  The Hoard Multiprocessor Memory Allocator
  www.hoard.org

  Author: Emery Berger, http://www.emeryberger.com
  Copyright (c) 1998-2020 Emery Berger

*/

#ifndef HOARD_SHARDEDGLOBALHEAP_H
#define HOARD_SHARDEDGLOBALHEAP_H

#include <atomic>
#include <cstdio>

#if defined(__linux__)
#include <sched.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

#include "hoardsuperblock.h"
#include "processheap.h"

namespace Hoard {

  template <size_t SuperblockSize,
	    template <class LockType_,
		      int SuperblockSize_,
		      typename HeapType_> class Header_,
	    int EmptinessClasses,
	    class MmapSource,
	    class LockType,
	    int NumShards = 8>  // Must be power of two; 8 works well up to ~64 cores
  class ShardedGlobalHeap {

    static_assert((NumShards & (NumShards - 1)) == 0, "NumShards must be a power of two");
    static_assert(NumShards >= 2, "NumShards must be at least 2");

    class bogusThresholdFunctionClass {
    public:
      static inline bool function (unsigned int, unsigned int, size_t) {
	return false;
      }
    };

  public:

    ShardedGlobalHeap()
    {
      for (int i = 0; i < NumShards; i++) {
        _shards[i] = getShard(i);
        _shardSizes[i].store(0, std::memory_order_relaxed);
      }
    }

    typedef ProcessHeap<SuperblockSize, Header_, EmptinessClasses, LockType, bogusThresholdFunctionClass, MmapSource> SuperHeap;
    typedef HoardSuperblock<LockType, SuperblockSize, ShardedGlobalHeap, Header_> SuperblockType;

    // Put a superblock back to the global heap.
    // Uses CPU-local shard for NUMA locality (last-touch policy).
    void put (void * s, size_t sz) {
      assert (s);
      auto * sb = (SuperblockType *) s;
      assert (sb->isValidSuperblock());

      // Put to CPU-local shard to preserve NUMA locality.
      // On NUMA systems, this keeps superblocks near their physical memory.
      auto tid = HL::CPUInfo::getThreadId();
      int shard = getLocalShard(tid);

      _shards[shard]->put((typename SuperHeap::SuperblockType *) s, sz);
      _shardSizes[shard].fetch_add(1, std::memory_order_relaxed);
    }

    // Get a superblock using power-of-two random choices.
    // This reduces contention while maintaining memory blowup bounds.
    SuperblockType * get (size_t sz, void * dest) {
      // Try local shard first (based on CPU for NUMA locality).
      auto tid = HL::CPUInfo::getThreadId();
      int localShard = getLocalShard(tid);

      auto * s = tryGetFromShard(localShard, sz, dest);
      if (s) {
        return s;
      }

      // Power-of-two choices: pick two random shards, try the fuller one.
      // This spreads load while avoiding the emptiest shards.
      unsigned int r = fastRand(tid);
      int shard1 = r & (NumShards - 1);
      int shard2 = (r >> 16) & (NumShards - 1);
      if (shard2 == shard1) {
        shard2 = (shard1 + 1) & (NumShards - 1);
      }

      // Try the fuller shard first (heuristic to balance load).
      auto size1 = _shardSizes[shard1].load(std::memory_order_relaxed);
      auto size2 = _shardSizes[shard2].load(std::memory_order_relaxed);

      int firstShard = (size1 >= size2) ? shard1 : shard2;
      int secondShard = (size1 >= size2) ? shard2 : shard1;

      s = tryGetFromShard(firstShard, sz, dest);
      if (s) {
        return s;
      }

      s = tryGetFromShard(secondShard, sz, dest);
      if (s) {
        return s;
      }

      // Fall back: try remaining shards starting from after our last random choice.
      int start = (secondShard + 1) & (NumShards - 1);
      for (int j = 0; j < NumShards; j++) {
        int i = (start + j) & (NumShards - 1);
        if (i == localShard || i == shard1 || i == shard2) continue;
        s = tryGetFromShard(i, sz, dest);
        if (s) {
          return s;
        }
      }

      return nullptr;
    }

  private:

    SuperblockType * tryGetFromShard(int shard, size_t sz, void * dest) {
      auto * s = reinterpret_cast<SuperblockType *>(
        _shards[shard]->get(sz, reinterpret_cast<SuperHeap *>(dest)));
      if (s) {
        assert(s->isValidSuperblock());
        _shardSizes[shard].fetch_sub(1, std::memory_order_relaxed);
      }
      return s;
    }

    // Fast PRNG for shard selection (xorshift).
    static inline unsigned int fastRand(size_t seed) {
      unsigned int x = static_cast<unsigned int>(seed ^ (seed >> 17));
      x ^= x << 13;
      x ^= x >> 7;
      x ^= x << 17;
      return x;
    }

    // Get a shard index with NUMA locality awareness.
    // Uses syscall to get both CPU and NUMA node, then maps to a shard
    // that preserves NUMA locality while spreading load within each node.
    static inline int getLocalShard(size_t tid) {
#if defined(__linux__) && !defined(HOARD_DISABLE_NUMA_SHARDING)
      unsigned int cpu = 0;
      unsigned int node = 0;
      // getcpu() via syscall returns both CPU and NUMA node
      if (syscall(SYS_getcpu, &cpu, &node, nullptr) == 0) {
        // Combine NUMA node and CPU to get shard:
        // - High bits from node ensure different nodes use different shard ranges
        // - Low bits from CPU spread load within each node's range
        // With 8 shards and 2 nodes: node 0 gets shards 0-3, node 1 gets shards 4-7
        unsigned int shardsPerNode = NumShards / getNumaNodeCount();
        if (shardsPerNode < 1) shardsPerNode = 1;
        unsigned int nodeBase = (node * shardsPerNode) & (NumShards - 1);
        unsigned int cpuOffset = cpu % shardsPerNode;
        return (nodeBase + cpuOffset) & (NumShards - 1);
      }
#endif
      // Fallback: hash thread ID
      return tid & (NumShards - 1);
    }

    // Cache the NUMA node count (queried once at startup)
    static inline unsigned int getNumaNodeCount() {
      static unsigned int count = 0;
      if (count == 0) {
        count = detectNumaNodeCount();
        if (count == 0) count = 1;
      }
      return count;
    }

    static unsigned int detectNumaNodeCount() {
#if defined(__linux__)
      // Count NUMA nodes by checking /sys/devices/system/node/nodeN
      unsigned int n = 0;
      for (n = 0; n < 256; n++) {
        char path[64];
        snprintf(path, sizeof(path), "/sys/devices/system/node/node%u", n);
        if (access(path, F_OK) != 0) break;
      }
      return n > 0 ? n : 1;
#else
      return 1;
#endif
    }

    SuperHeap * _shards[NumShards];
    std::atomic<int> _shardSizes[NumShards];

    inline static SuperHeap * getShard(int index) {
      // Each shard has its own static storage.
      static double shardBufs[NumShards][sizeof(SuperHeap) / sizeof(double) + 1];
      static SuperHeap * shards[NumShards] = { nullptr };
      static std::atomic<bool> initialized[NumShards] = {};

      if (!initialized[index].load(std::memory_order_acquire)) {
        shards[index] = new (&shardBufs[index][0]) SuperHeap;
        initialized[index].store(true, std::memory_order_release);
      }
      return shards[index];
    }

    ShardedGlobalHeap (const ShardedGlobalHeap&);
    ShardedGlobalHeap& operator=(const ShardedGlobalHeap&);
  };

}

#endif
