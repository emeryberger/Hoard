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

// Allocation-free printf (github.com/emeryberger/printf)
#include "printf.h"

// Platform-specific includes for CPU/NUMA detection
#if defined(__linux__)
#include <sched.h>
#include <sys/syscall.h>
#include <unistd.h>
#elif defined(__APPLE__)
#include <sys/sysctl.h>
#include <mach/mach.h>
#include <mach/thread_act.h>
#elif defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
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
	    int NumShards = 64>  // Must be power of two; 64 for large NUMA systems
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
    void put(void* s, size_t sz) {
      assert(s);
      auto* sb = static_cast<SuperblockType*>(s);
      assert(sb->isValidSuperblock());

      // Put to CPU-local shard to preserve NUMA locality.
      // On NUMA systems, this keeps superblocks near their physical memory.
      auto tid = HL::CPUInfo::getThreadId();
      int shard = getLocalShard(tid);

      _shards[shard]->put(static_cast<typename SuperHeap::SuperblockType*>(s), sz);
      _shardSizes[shard].fetch_add(1, std::memory_order_relaxed);
    }

    // Get a superblock using NUMA-aware power-of-two random choices.
    // Prefers shards on the same NUMA node to minimize cross-node traffic.
    SuperblockType * get (size_t sz, void * dest) {
      // Try local shard first (based on CPU for NUMA locality).
      auto tid = HL::CPUInfo::getThreadId();
      int localShard = getLocalShard(tid);

      auto * s = tryGetFromShard(localShard, sz, dest);
      if (s) {
        return s;
      }

      // Get NUMA topology info for this thread
      unsigned int shardsPerNode = NumShards / getNumaNodeCount();
      if (shardsPerNode < 1) shardsPerNode = 1;
      int nodeBase = (localShard / shardsPerNode) * shardsPerNode;
      int nodeEnd = nodeBase + shardsPerNode;

      // Phase 1: Power-of-two choices WITHIN same NUMA node
      unsigned int r = fastRand(tid);
      int shard1 = nodeBase + (r % shardsPerNode);
      int shard2 = nodeBase + ((r >> 16) % shardsPerNode);
      if (shard2 == shard1) {
        shard2 = nodeBase + ((shard1 - nodeBase + 1) % shardsPerNode);
      }

      // Try the fuller shard first (heuristic to balance load).
      auto size1 = _shardSizes[shard1].load(std::memory_order_relaxed);
      auto size2 = _shardSizes[shard2].load(std::memory_order_relaxed);

      int firstShard = (size1 >= size2) ? shard1 : shard2;
      int secondShard = (size1 >= size2) ? shard2 : shard1;

      if (firstShard != localShard) {
        s = tryGetFromShard(firstShard, sz, dest);
        if (s) return s;
      }

      if (secondShard != localShard) {
        s = tryGetFromShard(secondShard, sz, dest);
        if (s) return s;
      }

      // Phase 2: Try remaining shards on same NUMA node
      int start = (secondShard + 1 - nodeBase) % shardsPerNode + nodeBase;
      for (unsigned int j = 0; j < shardsPerNode; j++) {
        int i = (start - nodeBase + j) % shardsPerNode + nodeBase;
        if (i == localShard || i == shard1 || i == shard2) continue;
        s = tryGetFromShard(i, sz, dest);
        if (s) return s;
      }

      // Phase 3: Try shards on OTHER NUMA nodes (last resort)
      for (int i = 0; i < NumShards; i++) {
        if (i >= nodeBase && i < nodeEnd) continue;  // Skip same-node shards
        s = tryGetFromShard(i, sz, dest);
        if (s) return s;
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

    // Get current CPU number (platform-specific).
    static inline unsigned int getCurrentCpu() {
#if defined(__linux__)
      unsigned int cpu = 0;
      unsigned int node = 0;
      if (syscall(SYS_getcpu, &cpu, &node, nullptr) == 0) {
        return cpu;
      }
      return 0;
#elif defined(__APPLE__)
      // pthread_cpu_number_np is available since macOS 11.0 and is very fast
      // (~2ns, no syscall - reads from thread-local commpage).
      size_t cpu;
      if (pthread_cpu_number_np(&cpu) == 0) {
        return static_cast<unsigned int>(cpu);
      }
      return 0;
#elif defined(_WIN32)
      return GetCurrentProcessorNumber();
#else
      return 0;
#endif
    }

    // Get current NUMA node (platform-specific).
    static inline unsigned int getCurrentNumaNode() {
#if defined(__linux__)
      unsigned int cpu = 0;
      unsigned int node = 0;
      if (syscall(SYS_getcpu, &cpu, &node, nullptr) == 0) {
        return node;
      }
      return 0;
#elif defined(_WIN32)
      PROCESSOR_NUMBER procNum;
      GetCurrentProcessorNumberEx(&procNum);
      USHORT nodeNum = 0;
      GetNumaProcessorNodeEx(&procNum, &nodeNum);
      return static_cast<unsigned int>(nodeNum);
#else
      // macOS and others: assume single NUMA node
      return 0;
#endif
    }

    // Get a shard index with NUMA locality awareness.
    // Maps CPU and NUMA node to a shard that preserves locality
    // while spreading load within each node.
    static inline int getLocalShard(size_t tid) {
#if !defined(HOARD_DISABLE_NUMA_SHARDING)
      unsigned int cpu = getCurrentCpu();
      unsigned int node = getCurrentNumaNode();

      // Combine NUMA node and CPU to get shard:
      // - High bits from node ensure different nodes use different shard ranges
      // - Low bits from CPU spread load within each node's range
      // With 8 shards and 2 nodes: node 0 gets shards 0-3, node 1 gets shards 4-7
      unsigned int shardsPerNode = NumShards / getNumaNodeCount();
      if (shardsPerNode < 1) shardsPerNode = 1;
      unsigned int nodeBase = (node * shardsPerNode) & (NumShards - 1);
      unsigned int cpuOffset = cpu % shardsPerNode;
      return static_cast<int>((nodeBase + cpuOffset) & (NumShards - 1));
#else
      (void)tid;
#endif
      // Fallback: hash thread ID
      return static_cast<int>(tid & (NumShards - 1));
    }

    // Cache the NUMA node count (queried once at startup).
    static inline unsigned int getNumaNodeCount() {
      static unsigned int count = 0;
      if (count == 0) {
        count = detectNumaNodeCount();
        if (count == 0) count = 1;
      }
      return count;
    }

    // Detect number of NUMA nodes (platform-specific).
    static unsigned int detectNumaNodeCount() {
#if defined(__linux__)
      // Count NUMA nodes by checking /sys/devices/system/node/nodeN
      unsigned int n = 0;
      for (n = 0; n < 256; n++) {
        char path[64];
        // Use allocation-free snprintf_ from emeryberger/printf
        snprintf_(path, sizeof(path), "/sys/devices/system/node/node%u", n);
        if (access(path, F_OK) != 0) break;
      }
      return n > 0 ? n : 1;
#elif defined(_WIN32)
      ULONG highestNode = 0;
      if (GetNumaHighestNodeNumber(&highestNode)) {
        return static_cast<unsigned int>(highestNode + 1);
      }
      return 1;
#else
      // macOS and others: assume single NUMA node
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
