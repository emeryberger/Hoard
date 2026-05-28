///-*-C++-*-//////////////////////////////////////////////////////////////////
//
// NUMA Stress Test for Memory Allocators
//
// This test stresses NUMA locality by:
// 1. Pinning threads to specific CPUs across NUMA nodes (where supported)
// 2. Having threads allocate memory and pass pointers to threads on OTHER nodes
// 3. Measuring the performance impact of cross-node memory access
//
// The receiver performs reads AND writes on the cross-node memory to exercise
// the full NUMA penalty (both directions of cache coherence traffic).
//
// Portable: Linux (full NUMA support), macOS/Windows (runs without pinning)
//
//////////////////////////////////////////////////////////////////////////////

#ifndef _REENTRANT
#define _REENTRANT
#endif

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>

// Platform-specific includes for CPU pinning
#if defined(__linux__)
#define _GNU_SOURCE
#include <sched.h>
#include <unistd.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#include <mach/thread_policy.h>
#include <sys/sysctl.h>
#elif defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

// Configuration
static int nthreads = 8;
static int niterations = 100000;
static int objSize = 64;  // Cache line sized objects
static bool crossNode = true;

// Cache line alignment to prevent false sharing
constexpr size_t kCacheLineSize = 64;

struct alignas(kCacheLineSize) ThreadQueue {
    std::atomic<void*> slot{nullptr};
};

static ThreadQueue* queues = nullptr;
static std::atomic<bool> running{true};
static std::atomic<int> readyCount{0};

// Platform abstraction for CPU count
static int getNumCpus() {
#if defined(__linux__)
    return static_cast<int>(sysconf(_SC_NPROCESSORS_ONLN));
#elif defined(__APPLE__)
    int ncpu = 1;
    size_t len = sizeof(ncpu);
    sysctlbyname("hw.ncpu", &ncpu, &len, nullptr, 0);
    return ncpu;
#elif defined(_WIN32)
    SYSTEM_INFO sysinfo;
    GetSystemInfo(&sysinfo);
    return static_cast<int>(sysinfo.dwNumberOfProcessors);
#else
    return 1;
#endif
}

// Get NUMA node for a CPU (Linux only, others return 0)
static int getNumaNode(int cpu) {
#if defined(__linux__)
    char path[128];
    snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/node0", cpu);
    if (access(path, F_OK) == 0) return 0;
    snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/node1", cpu);
    if (access(path, F_OK) == 0) return 1;
    // Fallback: assume first half of CPUs are node 0
    int ncpus = getNumCpus();
    return cpu < ncpus / 2 ? 0 : 1;
#else
    (void)cpu;
    return 0;
#endif
}

// Pin thread to specific CPU (best effort, no-op on unsupported platforms)
static void pinToCpu(int cpu) {
#if defined(__linux__)
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(cpu, &cpuset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
#elif defined(__APPLE__)
    // macOS doesn't support hard affinity, but we can provide a hint
    thread_affinity_policy_data_t policy = { cpu };
    thread_policy_set(mach_thread_self(), THREAD_AFFINITY_POLICY,
                      reinterpret_cast<thread_policy_t>(&policy), 1);
#elif defined(_WIN32)
    SetThreadAffinityMask(GetCurrentThread(), 1ULL << cpu);
#else
    (void)cpu;
#endif
}

// Touch memory with reads and writes to exercise NUMA traffic
// Uses a simple checksum pattern that the compiler can't optimize away
static void touchMemory(void* ptr, size_t size) {
    auto* data = static_cast<volatile uint8_t*>(ptr);
    uint8_t checksum = 0;

    // Write pattern
    for (size_t i = 0; i < size; i++) {
        data[i] = static_cast<uint8_t>(i & 0xFF);
    }

    // Read and accumulate (prevents compiler from eliminating the writes)
    for (size_t i = 0; i < size; i++) {
        checksum ^= data[i];
    }

    // Write the checksum back (ensures reads aren't eliminated)
    if (size > 0) {
        data[0] = checksum;
    }
}

// Perform read-modify-write on received memory (cross-node traffic)
// This exercises the full NUMA penalty: read from remote, write back
static void processReceivedMemory(void* ptr, size_t size) {
    auto* data = static_cast<volatile uint8_t*>(ptr);

    // Read-modify-write every cache line
    for (size_t i = 0; i < size; i += kCacheLineSize) {
        // Read
        uint8_t val = data[i];
        // Modify
        val = static_cast<uint8_t>(val ^ 0xAA);
        // Write back
        data[i] = val;
    }

    // Full read pass to ensure cache line ownership transfer
    uint8_t checksum = 0;
    for (size_t i = 0; i < size; i++) {
        checksum ^= data[i];
    }

    // Final write (prevents optimization)
    if (size > 0) {
        data[size - 1] = checksum;
    }
}

static void worker(int id, int targetCpu) {
    pinToCpu(targetCpu);

    int myNode = getNumaNode(targetCpu);

    // Find a partner thread on a different NUMA node (if crossNode mode)
    int partner = -1;
    if (crossNode) {
        int ncpus = getNumCpus();
        for (int i = 0; i < nthreads; i++) {
            if (i != id) {
                // Determine partner's node from CPU assignment
                int partnerCpu;
                int half = ncpus / 2;
                if (i % 2 == 0) {
                    partnerCpu = (i / 2) % half;
                } else {
                    partnerCpu = half + ((i / 2) % half);
                }
                int partnerNode = getNumaNode(partnerCpu);
                if (partnerNode != myNode) {
                    partner = i;
                    break;
                }
            }
        }
    }
    if (partner < 0) {
        // No cross-node partner, use adjacent thread
        partner = (id + 1) % nthreads;
    }

    readyCount.fetch_add(1, std::memory_order_release);
    while (readyCount.load(std::memory_order_acquire) < nthreads) {
        std::this_thread::yield();
    }

    for (int iter = 0; iter < niterations && running.load(std::memory_order_relaxed); iter++) {
        // Allocate an object
        void* obj = malloc(static_cast<size_t>(objSize));
        if (!obj) continue;

        // Touch the memory (establishes local NUMA ownership)
        touchMemory(obj, static_cast<size_t>(objSize));

        // Try to pass to partner (cross-node free)
        void* expected = nullptr;
        if (queues[partner].slot.compare_exchange_strong(expected, obj,
                std::memory_order_release, std::memory_order_relaxed)) {
            // Successfully passed to partner
        } else {
            // Partner's slot full, free locally
            free(obj);
        }

        // Check if we received an object from another thread
        void* received = queues[id].slot.exchange(nullptr, std::memory_order_acquire);
        if (received) {
            // Process the memory (read-modify-write to exercise NUMA traffic)
            processReceivedMemory(received, static_cast<size_t>(objSize));
            free(received);
        }
    }

    // Drain remaining objects
    void* remaining = queues[id].slot.exchange(nullptr, std::memory_order_acquire);
    if (remaining) {
        free(remaining);
    }
}

int main(int argc, char* argv[]) {
    if (argc >= 2) nthreads = std::atoi(argv[1]);
    if (argc >= 3) niterations = std::atoi(argv[2]);
    if (argc >= 4) objSize = std::atoi(argv[3]);
    if (argc >= 5) crossNode = (std::atoi(argv[4]) != 0);

    int ncpus = getNumCpus();

    std::printf("NUMA stress test: %d threads, %d iterations, %d byte objects, cross-node=%d\n",
                nthreads, niterations, objSize, crossNode);
    std::printf("System has %d CPUs\n", ncpus);

    // Allocate thread queues
    queues = new ThreadQueue[static_cast<size_t>(nthreads)];

    // Assign CPUs to threads, spreading across NUMA nodes
    std::vector<int> cpuAssignments(static_cast<size_t>(nthreads));
    for (int i = 0; i < nthreads; i++) {
        if (crossNode) {
            // Interleave across nodes: 0, ncpus/2, 1, ncpus/2+1, ...
            int half = ncpus / 2;
            if (half < 1) half = 1;
            if (i % 2 == 0) {
                cpuAssignments[static_cast<size_t>(i)] = (i / 2) % half;
            } else {
                cpuAssignments[static_cast<size_t>(i)] = half + ((i / 2) % half);
            }
        } else {
            // Sequential assignment (same node for small thread counts)
            cpuAssignments[static_cast<size_t>(i)] = i % ncpus;
        }
        std::printf("Thread %d -> CPU %d (node %d)\n", i,
                    cpuAssignments[static_cast<size_t>(i)],
                    getNumaNode(cpuAssignments[static_cast<size_t>(i)]));
    }

    std::vector<std::thread> threads;
    threads.reserve(static_cast<size_t>(nthreads));

    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < nthreads; i++) {
        threads.emplace_back(worker, i, cpuAssignments[static_cast<size_t>(i)]);
    }

    for (auto& th : threads) {
        th.join();
    }

    auto stop = std::chrono::high_resolution_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::duration<double>>(stop - start);

    double opsPerSec = (nthreads * niterations) / elapsed.count();
    std::printf("Time elapsed = %.3f seconds\n", elapsed.count());
    std::printf("Throughput = %.0f ops/sec\n", opsPerSec);

    delete[] queues;
    return 0;
}
