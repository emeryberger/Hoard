#!/usr/bin/env python3
"""Generate benchmark graphs for Hoard PR.

All results are normalized to Hoard (Hoard = 1.0).
For time/memory: lower is better, so values > 1 mean worse than Hoard.
For throughput: higher is better, so we show as "relative performance" where > 1 is better.
"""

import matplotlib.pyplot as plt
import seaborn as sns
import numpy as np

# Set up seaborn style
sns.set_theme(style="whitegrid", context="paper", font_scale=1.1)
plt.rcParams['figure.facecolor'] = 'white'
plt.rcParams['axes.facecolor'] = 'white'

# Color palette
COLORS = {
    'Hoard': '#2ecc71',      # Green
    'mimalloc': '#3498db',   # Blue
    'jemalloc': '#e74c3c',   # Red
    'glibc': '#9b59b6',      # Purple
}

MARKERS = {
    'Hoard': 'o',
    'mimalloc': 's',
    'jemalloc': '^',
    'glibc': 'D',
}

ALLOCATORS = ['Hoard', 'mimalloc', 'jemalloc', 'glibc']

def save_fig(fig, name):
    fig.savefig(f'/home/emerydb/git/Hoard/doc/{name}.png', dpi=150, bbox_inches='tight',
                facecolor='white', edgecolor='none')
    plt.close(fig)

def normalize_to_hoard(data):
    """Normalize all values to Hoard (Hoard = 1.0)."""
    hoard_vals = data['Hoard']
    return {k: [v[i] / hoard_vals[i] if hoard_vals[i] != 0 else 1.0 for i in range(len(v))]
            for k, v in data.items()}

# =============================================================================
# BENCHMARK DATA
# =============================================================================

# Larson: fixed 5-second run, reports throughput (ops/sec) - higher is better
# Memory measured separately
larson_threads = [16, 32, 64, 128, 192, 256]
larson_mem = {
    'Hoard': [69, 110, 222, 714, 929, 1344],
    'mimalloc': [128, 234, 517, 1167, 1918, 2576],
    'jemalloc': [93, 185, 424, 928, 1639, 2364],
    'glibc': [115, 230, 444, 882, 1291, 1687],
}

# threadtest: elapsed time (seconds) - lower is better
threadtest_threads = [16, 32, 64, 128, 192, 256]
threadtest_time = {
    'Hoard': [2.89, 1.52, 2.41, 0.94, 0.48, 0.42],
    'mimalloc': [3.07, 1.55, 0.74, 0.43, 0.35, 0.43],
    'jemalloc': [7.90, 3.83, 1.94, 1.27, 0.94, 1.01],
    'glibc': [6.49, 3.42, 1.66, 0.98, 0.76, 0.90],
}
threadtest_mem = {
    'Hoard': [8, 8, 9, 10, 10, 11],
    'mimalloc': [9, 10, 11, 14, 16, 18],
    'jemalloc': [9, 14, 17, 22, 28, 28],
    'glibc': [8, 9, 9, 10, 11, 11],
}

# linux-scalability: elapsed time (seconds) - lower is better
linuxscal_threads = [16, 32, 64, 128, 192, 256]
linuxscal_time = {
    'Hoard': [0.066, 0.095, 0.142, 0.226, 0.449, 0.248],
    'mimalloc': [0.046, 0.067, 0.116, 0.211, 0.369, 0.177],
    'jemalloc': [0.044, 0.047, 0.064, 0.091, 0.073, 0.104],
    'glibc': [0.160, 0.216, 0.422, 0.590, 1.003, 1.562],
}
linuxscal_mem = {
    'Hoard': [183, 303, 543, 1035, 2018, 1796],
    'mimalloc': [182, 302, 542, 1034, 1526, 2028],
    'jemalloc': [62, 62, 74, 86, 74, 129],
    'glibc': [229, 351, 644, 1154, 1616, 2115],
}

# Phong: elapsed time (seconds) - lower is better
phong_threads = [4, 8, 16, 32, 64, 128, 192, 256]
phong_time = {
    'Hoard': [2.08, 0.43, 4.45, 1.28, 0.43, 0.37, 0.40, 0.45],
    'mimalloc': [4.42, 0.97, 9.04, 1.83, 0.49, 0.23, 0.19, 0.19],
    'jemalloc': [6.82, 1.37, 13.02, 2.77, 0.64, 0.25, 0.22, 0.17],
    'glibc': [10.19, 2.14, 18.41, 4.14, 1.11, 0.61, 0.56, 0.54],
}
phong_mem = {
    'Hoard': [203, 204, 995, 1000, 1012, 1014, 1054, 1043],
    'mimalloc': [101, 107, 488, 510, 560, 640, 622, 614],
    'jemalloc': [100, 101, 481, 487, 508, 520, 535, 510],
    'glibc': [109, 110, 529, 541, 565, 550, 529, 534],
}

# =============================================================================
# PLOTTING FUNCTIONS
# =============================================================================

def plot_normalized_lines(ax, threads, data, title, ylabel, show_legend=True):
    """Plot normalized line graph. Values > 1 mean worse than Hoard."""
    norm_data = normalize_to_hoard(data)

    for name in ALLOCATORS:
        ax.plot(threads, norm_data[name],
                marker=MARKERS[name],
                color=COLORS[name],
                linewidth=2.5,
                markersize=8,
                label=name,
                markeredgecolor='white',
                markeredgewidth=0.5)

    ax.axhline(y=1.0, color='#2ecc71', linestyle='-', alpha=0.7, linewidth=2, label='_nolegend_')
    ax.set_xlabel('Threads', fontsize=11)
    ax.set_ylabel(ylabel, fontsize=11)
    ax.set_title(title, fontsize=12, fontweight='bold', pad=10)

    if show_legend:
        ax.legend(loc='best', framealpha=0.95, edgecolor='gray')

    ax.set_xscale('log', base=2)
    ax.set_xticks(threads)
    ax.set_xticklabels([str(t) for t in threads])
    ax.grid(True, alpha=0.3, linestyle='-')
    ax.set_ylim(bottom=0)

    # Add subtle shading below 1.0 (Hoard wins region)
    ax.axhspan(0, 1.0, alpha=0.05, color='green')

def plot_benchmark_dual(threads, time_data, mem_data, title, filename, time_label='Time'):
    """Create a two-panel figure with normalized time and memory."""
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(14, 5))

    plot_normalized_lines(ax1, threads, time_data,
                         f'{title}\n{time_label} (lower is better)',
                         f'{time_label} (relative to Hoard)')

    plot_normalized_lines(ax2, threads, mem_data,
                         f'{title}\nMemory (lower is better)',
                         'Memory (relative to Hoard)',
                         show_legend=False)

    # Add shared legend at bottom
    handles, labels = ax1.get_legend_handles_labels()
    ax1.get_legend().remove()
    fig.legend(handles, labels, loc='upper center', ncol=4, bbox_to_anchor=(0.5, 0.02),
               framealpha=0.95, edgecolor='gray')

    plt.tight_layout()
    plt.subplots_adjust(bottom=0.15)
    save_fig(fig, filename)
    print(f'Generated {filename}.png')

def plot_benchmark_single(threads, data, title, filename, ylabel):
    """Create a single-panel figure."""
    fig, ax = plt.subplots(figsize=(8, 5))
    plot_normalized_lines(ax, threads, data, title, ylabel)
    plt.tight_layout()
    save_fig(fig, filename)
    print(f'Generated {filename}.png')

# =============================================================================
# GENERATE PLOTS
# =============================================================================

# Individual benchmark plots
plot_benchmark_single(larson_threads, larson_mem,
                     'Larson (server workload)\nMemory Usage (lower is better)',
                     'bench_larson', 'Memory (relative to Hoard)')

plot_benchmark_dual(threadtest_threads, threadtest_time, threadtest_mem,
                   'threadtest (malloc/free throughput)', 'bench_threadtest')

plot_benchmark_dual(linuxscal_threads, linuxscal_time, linuxscal_mem,
                   'linux-scalability', 'bench_linuxscal')

plot_benchmark_dual(phong_threads, phong_time, phong_mem,
                   'Phong (realloc-heavy)', 'bench_phong')

# Summary plot - 2x2 grid
fig, axes = plt.subplots(2, 2, figsize=(14, 10))

plot_normalized_lines(axes[0, 0], larson_threads, larson_mem,
                     'Larson - Memory', 'Memory (relative to Hoard)', show_legend=False)

plot_normalized_lines(axes[0, 1], threadtest_threads, threadtest_time,
                     'threadtest - Time', 'Time (relative to Hoard)', show_legend=False)

plot_normalized_lines(axes[1, 0], linuxscal_threads, linuxscal_time,
                     'linux-scalability - Time', 'Time (relative to Hoard)', show_legend=False)

plot_normalized_lines(axes[1, 1], phong_threads, phong_time,
                     'Phong - Time', 'Time (relative to Hoard)', show_legend=False)

# Shared legend
handles, labels = [], []
for name in ALLOCATORS:
    handles.append(plt.Line2D([0], [0], marker=MARKERS[name], color=COLORS[name],
                              linewidth=2.5, markersize=8, markeredgecolor='white', markeredgewidth=0.5))
    labels.append(name)

fig.legend(handles, labels, loc='upper center', ncol=4, bbox_to_anchor=(0.5, 0.02),
           framealpha=0.95, edgecolor='gray', fontsize=11)

plt.suptitle('Hoard Performance Comparison (192-core NUMA system)\nNormalized to Hoard (1.0 line). Below line = Hoard wins.',
             fontsize=13, fontweight='bold', y=0.98)
plt.tight_layout()
plt.subplots_adjust(bottom=0.08, top=0.90)
save_fig(fig, 'bench_summary')
print('Generated bench_summary.png')

print('\nAll graphs generated in doc/')
