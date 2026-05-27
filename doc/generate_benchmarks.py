#!/usr/bin/env python3
"""Generate benchmark graphs for Hoard PR."""

import matplotlib.pyplot as plt
import numpy as np

plt.style.use('seaborn-v0_8-whitegrid')

# Color scheme
COLORS = {
    'Hoard': '#2ecc71',      # Green
    'mimalloc': '#3498db',   # Blue
    'jemalloc': '#e74c3c',   # Red
}

def save_fig(fig, name):
    fig.savefig(f'/home/emerydb/git/Hoard/doc/{name}.png', dpi=150, bbox_inches='tight',
                facecolor='white', edgecolor='none')
    plt.close(fig)

# Larson data (time in seconds, memory in MB)
larson_threads = [16, 32, 64, 128, 192, 256]
larson_time = {
    'Hoard': [10.08, 10.16, 10.34, 10.80, 11.09, 10.63],
    'mimalloc': [10.08, 10.18, 10.39, 10.96, 11.42, 10.31],
    'jemalloc': [10.05, 10.10, 10.19, 10.39, 10.57, 10.57],
}
larson_mem = {
    'Hoard': [69, 110, 222, 714, 929, 1344],
    'mimalloc': [128, 234, 517, 1167, 1918, 2576],
    'jemalloc': [93, 185, 424, 928, 1639, 2364],
}

# threadtest data
threadtest_threads = [16, 32, 64, 128, 192, 256]
threadtest_time = {
    'Hoard': [2.89, 1.52, 2.41, 0.94, 0.48, 0.42],
    'mimalloc': [3.07, 1.55, 0.74, 0.43, 0.35, 0.43],
    'jemalloc': [7.90, 3.83, 1.94, 1.27, 0.94, 1.01],
}
threadtest_mem = {
    'Hoard': [8, 8, 9, 10, 10, 11],
    'mimalloc': [9, 10, 11, 14, 16, 18],
    'jemalloc': [9, 14, 17, 22, 28, 28],
}

# linux-scalability data
linuxscal_threads = [16, 32, 64, 128, 192, 256]
linuxscal_time = {
    'Hoard': [0.066, 0.095, 0.142, 0.226, 0.449, 0.248],
    'mimalloc': [0.046, 0.067, 0.116, 0.211, 0.369, 0.177],
    'jemalloc': [0.044, 0.047, 0.064, 0.091, 0.073, 0.104],
}
linuxscal_mem = {
    'Hoard': [183, 303, 543, 1035, 2018, 1796],
    'mimalloc': [182, 302, 542, 1034, 1526, 2028],
    'jemalloc': [62, 62, 74, 86, 74, 129],
}

# Phong data
phong_threads = [4, 8, 16, 32, 64, 128, 192, 256]
phong_time = {
    'Hoard': [2.08, 0.43, 4.45, 1.28, 0.43, 0.37, 0.40, 0.45],
    'mimalloc': [4.42, 0.97, 9.04, 1.83, 0.49, 0.23, 0.19, 0.19],
    'jemalloc': [6.82, 1.37, 13.02, 2.77, 0.64, 0.25, 0.22, 0.17],
}
phong_mem = {
    'Hoard': [203, 204, 995, 1000, 1012, 1014, 1054, 1043],
    'mimalloc': [101, 107, 488, 510, 560, 640, 622, 614],
    'jemalloc': [100, 101, 481, 487, 508, 520, 535, 510],
}

def plot_benchmark(threads, time_data, mem_data, title, filename):
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(12, 4.5))

    x = np.arange(len(threads))
    width = 0.25

    # Time plot
    for i, (name, times) in enumerate(time_data.items()):
        bars = ax1.bar(x + i*width - width, times, width, label=name, color=COLORS[name], edgecolor='black', linewidth=0.5)

    ax1.set_xlabel('Threads', fontsize=11)
    ax1.set_ylabel('Time (seconds)', fontsize=11)
    ax1.set_title(f'{title} - Execution Time (lower is better)', fontsize=12, fontweight='bold')
    ax1.set_xticks(x)
    ax1.set_xticklabels(threads)
    ax1.legend(loc='best')
    ax1.set_ylim(bottom=0)

    # Memory plot
    for i, (name, mems) in enumerate(mem_data.items()):
        bars = ax2.bar(x + i*width - width, mems, width, label=name, color=COLORS[name], edgecolor='black', linewidth=0.5)

    ax2.set_xlabel('Threads', fontsize=11)
    ax2.set_ylabel('Peak Memory (MB)', fontsize=11)
    ax2.set_title(f'{title} - Memory Usage (lower is better)', fontsize=12, fontweight='bold')
    ax2.set_xticks(x)
    ax2.set_xticklabels(threads)
    ax2.legend(loc='best')
    ax2.set_ylim(bottom=0)

    plt.tight_layout()
    save_fig(fig, filename)
    print(f'Generated {filename}.png')

# Generate all plots
plot_benchmark(larson_threads, larson_time, larson_mem, 'Larson', 'bench_larson')
plot_benchmark(threadtest_threads, threadtest_time, threadtest_mem, 'threadtest', 'bench_threadtest')
plot_benchmark(linuxscal_threads, linuxscal_time, linuxscal_mem, 'linux-scalability', 'bench_linuxscal')
plot_benchmark(phong_threads, phong_time, phong_mem, 'Phong (realloc-heavy)', 'bench_phong')

# Create a summary comparison plot
fig, axes = plt.subplots(2, 2, figsize=(14, 10))

def plot_lines(ax, threads, time_data, title):
    for name, times in time_data.items():
        ax.plot(threads, times, 'o-', label=name, color=COLORS[name], linewidth=2, markersize=6)
    ax.set_xlabel('Threads', fontsize=10)
    ax.set_ylabel('Time (seconds)', fontsize=10)
    ax.set_title(title, fontsize=11, fontweight='bold')
    ax.legend(loc='best')
    ax.set_ylim(bottom=0)
    ax.grid(True, alpha=0.3)

plot_lines(axes[0, 0], larson_threads, larson_time, 'Larson (server workload)')
plot_lines(axes[0, 1], threadtest_threads, threadtest_time, 'threadtest (malloc/free)')
plot_lines(axes[1, 0], linuxscal_threads, linuxscal_time, 'linux-scalability')
plot_lines(axes[1, 1], phong_threads, phong_time, 'Phong (realloc-heavy)')

plt.suptitle('Hoard Performance Comparison (192-core NUMA system)', fontsize=14, fontweight='bold', y=1.02)
plt.tight_layout()
save_fig(fig, 'bench_summary')
print('Generated bench_summary.png')

print('\nAll graphs generated in doc/')
