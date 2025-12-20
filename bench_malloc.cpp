#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

// High-resolution timer
double get_time_ms() {
    LARGE_INTEGER freq, counter;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&counter);
    return (double)counter.QuadPart * 1000.0 / (double)freq.QuadPart;
}

// Benchmark 1: Many small allocations
void bench_small_allocs(int iterations, int size) {
    double start = get_time_ms();

    for (int i = 0; i < iterations; i++) {
        void* ptr = malloc(size);
        if (ptr) {
            ((char*)ptr)[0] = 'x';  // Touch memory
            free(ptr);
        }
    }

    double elapsed = get_time_ms() - start;
    printf("  Small allocs (%d x %d bytes): %.2f ms (%.0f ops/sec)\n",
           iterations, size, elapsed, iterations / (elapsed / 1000.0));
}

// Benchmark 2: Allocation burst (allocate many, then free all)
void bench_burst(int count, int size) {
    void** ptrs = (void**)malloc(count * sizeof(void*));
    if (!ptrs) return;

    double start = get_time_ms();

    // Allocate all
    for (int i = 0; i < count; i++) {
        ptrs[i] = malloc(size);
        if (ptrs[i]) ((char*)ptrs[i])[0] = 'x';
    }

    // Free all
    for (int i = 0; i < count; i++) {
        free(ptrs[i]);
    }

    double elapsed = get_time_ms() - start;
    printf("  Burst alloc (%d x %d bytes): %.2f ms (%.0f ops/sec)\n",
           count, size, elapsed, (count * 2) / (elapsed / 1000.0));

    free(ptrs);
}

// Benchmark 3: Realloc chain
void bench_realloc(int iterations) {
    double start = get_time_ms();

    for (int i = 0; i < iterations; i++) {
        void* ptr = malloc(16);
        ptr = realloc(ptr, 64);
        ptr = realloc(ptr, 256);
        ptr = realloc(ptr, 1024);
        ptr = realloc(ptr, 64);
        ptr = realloc(ptr, 16);
        free(ptr);
    }

    double elapsed = get_time_ms() - start;
    printf("  Realloc chains (%d iterations): %.2f ms (%.0f ops/sec)\n",
           iterations, elapsed, (iterations * 7) / (elapsed / 1000.0));
}

// Benchmark 4: Mixed sizes
void bench_mixed_sizes(int iterations) {
    int sizes[] = {8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096};
    int num_sizes = sizeof(sizes) / sizeof(sizes[0]);

    double start = get_time_ms();

    for (int i = 0; i < iterations; i++) {
        int size = sizes[i % num_sizes];
        void* ptr = malloc(size);
        if (ptr) {
            ((char*)ptr)[0] = 'x';
            free(ptr);
        }
    }

    double elapsed = get_time_ms() - start;
    printf("  Mixed sizes (%d iterations): %.2f ms (%.0f ops/sec)\n",
           iterations, elapsed, iterations / (elapsed / 1000.0));
}

// Benchmark 5: calloc
void bench_calloc(int iterations, int size) {
    double start = get_time_ms();

    for (int i = 0; i < iterations; i++) {
        void* ptr = calloc(1, size);
        if (ptr) free(ptr);
    }

    double elapsed = get_time_ms() - start;
    printf("  Calloc (%d x %d bytes): %.2f ms (%.0f ops/sec)\n",
           iterations, size, elapsed, iterations / (elapsed / 1000.0));
}

// Benchmark 6: C++ new/delete
void bench_cpp_new(int iterations) {
    double start = get_time_ms();

    for (int i = 0; i < iterations; i++) {
        int* ptr = new int[100];
        ptr[0] = i;
        delete[] ptr;
    }

    double elapsed = get_time_ms() - start;
    printf("  C++ new/delete (%d iterations): %.2f ms (%.0f ops/sec)\n",
           iterations, elapsed, iterations / (elapsed / 1000.0));
}

int main(int argc, char* argv[]) {
    printf("=== Memory Allocator Benchmark ===\n\n");
    fflush(stdout);

    // Warm up
    for (int i = 0; i < 10000; i++) {
        void* p = malloc(64);
        free(p);
    }

    int iterations = 100000;
    if (argc > 1) {
        iterations = atoi(argv[1]);
    }

    printf("Running with %d iterations per test...\n\n", iterations);
    fflush(stdout);

    // Run benchmarks
    printf("Small allocations:\n");
    bench_small_allocs(iterations, 16);
    bench_small_allocs(iterations, 64);
    bench_small_allocs(iterations, 256);
    fflush(stdout);

    printf("\nBurst allocations:\n");
    bench_burst(iterations / 10, 32);
    bench_burst(iterations / 10, 128);
    fflush(stdout);

    printf("\nRealloc:\n");
    bench_realloc(iterations / 10);
    fflush(stdout);

    printf("\nMixed sizes:\n");
    bench_mixed_sizes(iterations);
    fflush(stdout);

    printf("\nCalloc:\n");
    bench_calloc(iterations, 64);
    fflush(stdout);

    printf("\nC++ new/delete:\n");
    bench_cpp_new(iterations);
    fflush(stdout);

    printf("\n=== Benchmark complete ===\n");
    return 0;
}
