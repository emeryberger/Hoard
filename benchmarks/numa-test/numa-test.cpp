///-*-C++-*-//////////////////////////////////////////////////////////////////
//
// NUMA Stress Test for Memory Allocators
//
// This test stresses NUMA locality by:
// 1. Pinning threads to specific CPUs across NUMA nodes
// 2. Having threads allocate memory and pass pointers to threads on OTHER nodes
// 3. Measuring the performance impact of cross-node memory access
//
// A NUMA-aware allocator should show better performance because memory
// allocated by a thread stays on that thread's local node.
//
//////////////////////////////////////////////////////////////////////////////

#ifndef _REENTRANT
#define _REENTRANT
#endif

#define _GNU_SOURCE
#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>
#include <vector>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <sched.h>
#include <unistd.h>

using namespace std;
using namespace std::chrono;

// Configuration
int nthreads = 8;
int niterations = 100000;
int objSize = 64;  // Cache line sized objects
bool crossNode = true;  // If true, pass objects to threads on other NUMA node

// Shared queues for cross-thread object passing
struct alignas(64) ThreadQueue {
    atomic<void*> slot{nullptr};
    char padding[64 - sizeof(atomic<void*>)];
};

ThreadQueue* queues = nullptr;
atomic<bool> running{true};
atomic<int> readyCount{0};

// Get NUMA node for a CPU
int getNumaNode(int cpu) {
    char path[128];
    snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/node0", cpu);
    if (access(path, F_OK) == 0) return 0;
    snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/node1", cpu);
    if (access(path, F_OK) == 0) return 1;
    // Fallback: assume first half of CPUs are node 0
    int ncpus = sysconf(_SC_NPROCESSORS_ONLN);
    return cpu < ncpus / 2 ? 0 : 1;
}

// Pin thread to specific CPU
void pinToCpu(int cpu) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(cpu, &cpuset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
}

// Worker function
void worker(int id, int targetCpu) {
    pinToCpu(targetCpu);

    int myNode = getNumaNode(targetCpu);

    // Find a partner thread on a different NUMA node (if crossNode mode)
    int partner = -1;
    if (crossNode) {
        for (int i = 0; i < nthreads; i++) {
            if (i != id) {
                // Simple heuristic: threads in first half vs second half
                int partnerNode = (i < nthreads / 2) ? 0 : 1;
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

    readyCount.fetch_add(1);
    while (readyCount.load() < nthreads) {
        // Spin until all threads ready
    }

    long allocCount = 0;
    long freeCount = 0;
    long crossFreeCount = 0;

    for (int iter = 0; iter < niterations && running.load(); iter++) {
        // Allocate an object
        void* obj = malloc(objSize);
        if (!obj) continue;
        memset(obj, 0x42, objSize);  // Touch the memory
        allocCount++;

        // Try to pass to partner (cross-node free)
        void* expected = nullptr;
        if (queues[partner].slot.compare_exchange_strong(expected, obj)) {
            // Successfully passed to partner
        } else {
            // Partner's slot full, free locally
            free(obj);
            freeCount++;
        }

        // Check if we received an object from another thread
        void* received = queues[id].slot.exchange(nullptr);
        if (received) {
            // Access the memory (simulate use)
            volatile char c = ((char*)received)[0];
            (void)c;
            free(received);
            crossFreeCount++;
        }
    }

    // Drain remaining objects
    void* remaining = queues[id].slot.exchange(nullptr);
    if (remaining) {
        free(remaining);
        crossFreeCount++;
    }
}

int main(int argc, char* argv[]) {
    if (argc >= 2) nthreads = atoi(argv[1]);
    if (argc >= 3) niterations = atoi(argv[2]);
    if (argc >= 4) objSize = atoi(argv[3]);
    if (argc >= 5) crossNode = (atoi(argv[4]) != 0);

    int ncpus = sysconf(_SC_NPROCESSORS_ONLN);

    printf("NUMA stress test: %d threads, %d iterations, %d byte objects, cross-node=%d\n",
           nthreads, niterations, objSize, crossNode);
    printf("System has %d CPUs\n", ncpus);

    // Allocate thread queues
    queues = new ThreadQueue[nthreads];

    // Assign CPUs to threads, spreading across NUMA nodes
    vector<int> cpuAssignments(nthreads);
    for (int i = 0; i < nthreads; i++) {
        if (crossNode) {
            // Interleave across nodes: 0, ncpus/2, 1, ncpus/2+1, ...
            int half = ncpus / 2;
            if (i % 2 == 0) {
                cpuAssignments[i] = (i / 2) % half;
            } else {
                cpuAssignments[i] = half + ((i / 2) % half);
            }
        } else {
            // Sequential assignment (same node for small thread counts)
            cpuAssignments[i] = i % ncpus;
        }
        printf("Thread %d -> CPU %d (node %d)\n", i, cpuAssignments[i], getNumaNode(cpuAssignments[i]));
    }

    vector<thread> threads;

    high_resolution_clock t;
    auto start = t.now();

    for (int i = 0; i < nthreads; i++) {
        threads.emplace_back(worker, i, cpuAssignments[i]);
    }

    for (auto& th : threads) {
        th.join();
    }

    auto stop = t.now();
    auto elapsed = duration_cast<duration<double>>(stop - start);

    double opsPerSec = (nthreads * niterations) / elapsed.count();
    printf("Time elapsed = %.3f seconds\n", elapsed.count());
    printf("Throughput = %.0f ops/sec\n", opsPerSec);

    delete[] queues;
    return 0;
}
