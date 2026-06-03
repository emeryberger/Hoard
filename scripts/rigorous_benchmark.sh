#!/bin/bash

# Rigorous benchmark suite for Hoard allocator comparison
# Features:
#   - Interleaved trials (alternates allocators within each trial)
#   - 5 trials minimum per configuration
#   - Reports individual runs, mean, median, and standard error
#   - Supports variable thread counts

set -e

# Configuration
NUM_TRIALS=${NUM_TRIALS:-5}
BENCH_DIR="${BENCH_DIR:-$(dirname "$0")/../benchmarks}"
RESULTS_FILE="${RESULTS_FILE:-benchmark_results_$(date +%Y%m%d_%H%M%S).tsv}"

# Allocator paths (can be overridden via environment)
if [[ "$(uname)" == "Darwin" ]]; then
    HOARD_LIB="${HOARD_LIB:-$(dirname "$0")/../build/libhoard.dylib}"
    MIMALLOC_LIB="${MIMALLOC_LIB:-/opt/homebrew/lib/libmimalloc.dylib}"
    JEMALLOC_LIB="${JEMALLOC_LIB:-/opt/homebrew/lib/libjemalloc.dylib}"
    PRELOAD_VAR="DYLD_INSERT_LIBRARIES"
else
    HOARD_LIB="${HOARD_LIB:-$(dirname "$0")/../build/libhoard.so}"
    MIMALLOC_LIB="${MIMALLOC_LIB:-/usr/lib/x86_64-linux-gnu/libmimalloc.so}"
    JEMALLOC_LIB="${JEMALLOC_LIB:-/usr/lib/x86_64-linux-gnu/libjemalloc.so}"
    PRELOAD_VAR="LD_PRELOAD"
fi

# Thread counts to test
THREAD_COUNTS="${THREAD_COUNTS:-1 2 4 8 16 32 64 128 192 256}"

# Allocators to test (name:library pairs, empty library means system allocator)
declare -A ALLOCATORS
ALLOCATORS=(
    ["hoard"]="$HOARD_LIB"
    ["mimalloc"]="$MIMALLOC_LIB"
    ["jemalloc"]="$JEMALLOC_LIB"
    ["glibc"]=""
)

# Calculate statistics from space-separated values
calc_stats() {
    local values="$1"
    echo "$values" | tr ' ' '\n' | sort -n | awk '
    BEGIN { n=0; sum=0; sumsq=0 }
    {
        vals[NR] = $1
        n++
        sum += $1
        sumsq += $1 * $1
    }
    END {
        if (n == 0) { print "N/A N/A N/A N/A"; exit }
        mean = sum / n
        variance = (n > 1) ? (sumsq - sum*sum/n) / (n-1) : 0
        stddev = sqrt(variance)
        stderr = stddev / sqrt(n)

        # Median (middle value or average of two middle)
        if (n % 2 == 1) {
            median = vals[int(n/2) + 1]
        } else {
            median = (vals[n/2] + vals[n/2 + 1]) / 2
        }

        printf "%.2f %.2f %.2f %.2f", mean, median, stddev, stderr
    }'
}

# Run a single benchmark trial
run_single() {
    local lib="$1"
    local cmd="$2"

    if [ -n "$lib" ]; then
        env "$PRELOAD_VAR=$lib" $cmd 2>&1
    else
        $cmd 2>&1
    fi
}

# Extract throughput from Larson output
extract_larson_throughput() {
    grep -oE "Throughput = [0-9]+" | awk '{print $3}'
}

# Extract time from threadtest/linux-scalability output
extract_time() {
    grep -oE "Time elapsed[^0-9]*[0-9.]+" | grep -oE "[0-9.]+" | tail -1
}

# Print header
print_header() {
    echo "=========================================================="
    echo "Rigorous Allocator Benchmark Suite"
    echo "=========================================================="
    echo "Date: $(date)"
    echo "System: $(uname -a)"
    echo "Trials per configuration: $NUM_TRIALS"
    echo "Thread counts: $THREAD_COUNTS"
    echo ""
    echo "Allocator libraries:"
    for name in "${!ALLOCATORS[@]}"; do
        lib="${ALLOCATORS[$name]}"
        if [ -n "$lib" ]; then
            if [ -f "$lib" ]; then
                echo "  $name: $lib (found)"
            else
                echo "  $name: $lib (NOT FOUND - will skip)"
            fi
        else
            echo "  $name: system default"
        fi
    done
    echo "=========================================================="
    echo ""
}

# Run Larson benchmark with interleaved trials
run_larson() {
    local threads="$1"
    echo "=== Larson Benchmark ($threads threads) ==="

    # Collect results for each allocator
    declare -A results
    for name in "${!ALLOCATORS[@]}"; do
        results[$name]=""
    done

    # Interleaved trials
    for trial in $(seq 1 $NUM_TRIALS); do
        echo "  Trial $trial/$NUM_TRIALS..."
        for name in "${!ALLOCATORS[@]}"; do
            lib="${ALLOCATORS[$name]}"
            # Skip if library doesn't exist
            if [ -n "$lib" ] && [ ! -f "$lib" ]; then
                continue
            fi

            output=$(run_single "$lib" "$BENCH_DIR/larson/larson 10 10 500 10000 1000 1 $threads $threads")
            throughput=$(echo "$output" | extract_larson_throughput)

            if [ -n "$throughput" ]; then
                results[$name]="${results[$name]} $throughput"
                # Convert to M ops/s for display
                mops=$((throughput / 1000000))
                echo "    $name: $mops M ops/s"
            else
                echo "    $name: FAILED"
            fi
        done
    done

    # Report statistics
    echo ""
    echo "  Statistics (M ops/s):"
    printf "  %-12s %10s %10s %10s %10s\n" "Allocator" "Mean" "Median" "StdDev" "StdErr"
    printf "  %-12s %10s %10s %10s %10s\n" "---------" "----" "------" "------" "------"

    for name in "${!ALLOCATORS[@]}"; do
        if [ -n "${results[$name]}" ]; then
            # Convert to M ops/s for statistics
            mops_values=$(echo "${results[$name]}" | tr ' ' '\n' | awk '{if($1!="") printf "%.2f ", $1/1000000}')
            stats=$(calc_stats "$mops_values")
            mean=$(echo "$stats" | awk '{print $1}')
            median=$(echo "$stats" | awk '{print $2}')
            stddev=$(echo "$stats" | awk '{print $3}')
            stderr=$(echo "$stats" | awk '{print $4}')
            printf "  %-12s %10.1f %10.1f %10.1f %10.2f\n" "$name" "$mean" "$median" "$stddev" "$stderr"

            # Output to TSV
            echo -e "larson\t$threads\t$name\t${results[$name]}\t$mean\t$median\t$stddev\t$stderr" >> "$RESULTS_FILE"
        fi
    done

    # Report individual runs
    echo ""
    echo "  Individual runs (M ops/s):"
    for name in "${!ALLOCATORS[@]}"; do
        if [ -n "${results[$name]}" ]; then
            mops_values=$(echo "${results[$name]}" | tr ' ' '\n' | awk '{if($1!="") printf "%.0f ", $1/1000000}')
            echo "    $name: $mops_values"
        fi
    done
    echo ""
}

# Run threadtest benchmark with interleaved trials
run_threadtest() {
    local threads="$1"
    echo "=== Threadtest Benchmark ($threads threads) ==="

    declare -A results
    for name in "${!ALLOCATORS[@]}"; do
        results[$name]=""
    done

    for trial in $(seq 1 $NUM_TRIALS); do
        echo "  Trial $trial/$NUM_TRIALS..."
        for name in "${!ALLOCATORS[@]}"; do
            lib="${ALLOCATORS[$name]}"
            if [ -n "$lib" ] && [ ! -f "$lib" ]; then
                continue
            fi

            # Use consistent work: 100000 iterations, 10000 objects
            output=$(run_single "$lib" "$BENCH_DIR/threadtest/threadtest $threads 100000 10000 0 8")
            time_val=$(echo "$output" | extract_time)

            if [ -n "$time_val" ]; then
                results[$name]="${results[$name]} $time_val"
                echo "    $name: ${time_val}s"
            else
                echo "    $name: FAILED"
            fi
        done
    done

    echo ""
    echo "  Statistics (seconds, lower is better):"
    printf "  %-12s %10s %10s %10s %10s\n" "Allocator" "Mean" "Median" "StdDev" "StdErr"
    printf "  %-12s %10s %10s %10s %10s\n" "---------" "----" "------" "------" "------"

    for name in "${!ALLOCATORS[@]}"; do
        if [ -n "${results[$name]}" ]; then
            stats=$(calc_stats "${results[$name]}")
            mean=$(echo "$stats" | awk '{print $1}')
            median=$(echo "$stats" | awk '{print $2}')
            stddev=$(echo "$stats" | awk '{print $3}')
            stderr=$(echo "$stats" | awk '{print $4}')
            printf "  %-12s %10.3f %10.3f %10.3f %10.4f\n" "$name" "$mean" "$median" "$stddev" "$stderr"

            echo -e "threadtest\t$threads\t$name\t${results[$name]}\t$mean\t$median\t$stddev\t$stderr" >> "$RESULTS_FILE"
        fi
    done

    echo ""
    echo "  Individual runs (seconds):"
    for name in "${!ALLOCATORS[@]}"; do
        if [ -n "${results[$name]}" ]; then
            echo "    $name: ${results[$name]}"
        fi
    done
    echo ""
}

# Run linux-scalability benchmark
run_linux_scalability() {
    local threads="$1"
    echo "=== Linux-Scalability Benchmark ($threads threads) ==="

    declare -A results
    for name in "${!ALLOCATORS[@]}"; do
        results[$name]=""
    done

    for trial in $(seq 1 $NUM_TRIALS); do
        echo "  Trial $trial/$NUM_TRIALS..."
        for name in "${!ALLOCATORS[@]}"; do
            lib="${ALLOCATORS[$name]}"
            if [ -n "$lib" ] && [ ! -f "$lib" ]; then
                continue
            fi

            output=$(run_single "$lib" "$BENCH_DIR/linux-scalability/linux-scalability $threads 10000000 64")
            time_val=$(echo "$output" | extract_time)

            if [ -n "$time_val" ]; then
                results[$name]="${results[$name]} $time_val"
                echo "    $name: ${time_val}s"
            else
                echo "    $name: FAILED"
            fi
        done
    done

    echo ""
    echo "  Statistics (seconds, lower is better):"
    printf "  %-12s %10s %10s %10s %10s\n" "Allocator" "Mean" "Median" "StdDev" "StdErr"
    printf "  %-12s %10s %10s %10s %10s\n" "---------" "----" "------" "------" "------"

    for name in "${!ALLOCATORS[@]}"; do
        if [ -n "${results[$name]}" ]; then
            stats=$(calc_stats "${results[$name]}")
            mean=$(echo "$stats" | awk '{print $1}')
            median=$(echo "$stats" | awk '{print $2}')
            stddev=$(echo "$stats" | awk '{print $3}')
            stderr=$(echo "$stats" | awk '{print $4}')
            printf "  %-12s %10.3f %10.3f %10.3f %10.4f\n" "$name" "$mean" "$median" "$stddev" "$stderr"

            echo -e "linux-scalability\t$threads\t$name\t${results[$name]}\t$mean\t$median\t$stddev\t$stderr" >> "$RESULTS_FILE"
        fi
    done

    echo ""
    echo "  Individual runs (seconds):"
    for name in "${!ALLOCATORS[@]}"; do
        if [ -n "${results[$name]}" ]; then
            echo "    $name: ${results[$name]}"
        fi
    done
    echo ""
}

# Main
print_header

# Initialize TSV file with header
echo -e "benchmark\tthreads\tallocator\traw_values\tmean\tmedian\tstddev\tstderr" > "$RESULTS_FILE"

# Run benchmarks for each thread count
for threads in $THREAD_COUNTS; do
    run_larson "$threads"
    run_threadtest "$threads"
    run_linux_scalability "$threads"
done

echo "=========================================================="
echo "Results saved to: $RESULTS_FILE"
echo "=========================================================="
