#!/usr/bin/env python3
"""Generate benchmark graphs for Hoard PR.

All results are normalized to Hoard (Hoard = 1.0, the green horizontal line).
- Above the line = slower/more memory than Hoard
- Below the line = faster/less memory than Hoard

Left y-axis shows normalized values, right y-axis shows actual values.
"""

import matplotlib.pyplot as plt
import matplotlib as mpl
import numpy as np

# Use Times/serif font like academic papers
plt.rcParams.update({
    'font.family': 'serif',
    'font.serif': ['Times New Roman', 'Times', 'DejaVu Serif', 'Bitstream Vera Serif'],
    'font.size': 11,
    'axes.titlesize': 13,
    'axes.labelsize': 11,
    'xtick.labelsize': 10,
    'ytick.labelsize': 10,
    'legend.fontsize': 10,
    'figure.facecolor': 'white',
    'axes.facecolor': 'white',
    'axes.edgecolor': 'black',
    'axes.linewidth': 0.8,
    'axes.grid': True,
    'grid.color': '#cccccc',
    'grid.linewidth': 0.5,
    'grid.linestyle': '-',
})

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

# Larson: server workload simulation (throughput in M ops/sec, higher = better)
# Parameters: ./larson 5 8 1000 5000 100 4141 threads (same as mimalloc-bench)
# Data collected 2026-06-03 on 192-core AMD EPYC 9R14, 5 trials interleaved, median values
# Memory in KB
larson_threads = [1, 2, 4, 8, 16, 32, 64, 128, 192, 256]
larson_throughput = {
    'Hoard': [46, 44, 82, 154, 284, 553, 1049, 1932, 1823, 1950],
    'mimalloc': [44, 47, 92, 178, 359, 729, 1453, 2810, 3343, 3631],
    'jemalloc': [36, 40, 82, 157, 299, 610, 1172, 2151, 2464, 2439],
    'glibc': [29, 33, 65, 126, 248, 496, 939, 1578, 1488, 1157],
}
larson_mem = {
    'Hoard': [3144, 3152, 3124, 3152, 3104, 4680, 4648, 7728, 9292, 10832],
    'mimalloc': [3112, 3144, 3140, 3136, 3136, 4632, 6176, 9240, 12352, 15392],
    'jemalloc': [3144, 3140, 3116, 3120, 3136, 4664, 6204, 9280, 12348, 15424],
    'glibc': [3128, 3128, 3128, 3136, 3120, 4672, 6208, 9292, 12336, 15424],
}

# threadtest: malloc/free throughput (time in seconds, memory in MB)
threadtest_threads = [1, 2, 4, 8, 16, 32, 64, 128, 192, 256]
threadtest_time = {
    'Hoard': [8.75, 4.56, 2.24, 1.13, 2.89, 1.52, 2.41, 0.94, 0.48, 0.42],
    'mimalloc': [19.26, 10.27, 5.17, 2.51, 3.07, 1.55, 0.74, 0.43, 0.35, 0.43],
    'jemalloc': [19.24, 10.29, 5.42, 2.68, 7.90, 3.83, 1.94, 1.27, 0.94, 1.01],
    'glibc': [19.30, 9.61, 5.06, 2.52, 6.49, 3.42, 1.66, 0.98, 0.76, 0.90],
}
threadtest_mem = {
    'Hoard': [3, 3, 3, 3, 8, 8, 9, 10, 10, 11],
    'mimalloc': [3, 3, 3, 3, 9, 10, 11, 14, 16, 18],
    'jemalloc': [3, 3, 3, 3, 9, 14, 17, 22, 28, 28],
    'glibc': [3, 3, 3, 3, 8, 9, 9, 10, 11, 11],
}

# linux-scalability: malloc/free pairs (time in seconds, memory in MB)
linuxscal_threads = [1, 2, 4, 8, 16, 32, 64, 128, 192, 256]
linuxscal_time = {
    'Hoard': [1.55, 1.59, 2.20, 1.56, 0.066, 0.095, 0.142, 0.226, 0.449, 0.248],
    'mimalloc': [12.63, 10.83, 11.28, 12.32, 0.046, 0.067, 0.116, 0.211, 0.369, 0.177],
    'jemalloc': [10.66, 10.48, 11.95, 11.00, 0.044, 0.047, 0.064, 0.091, 0.073, 0.104],
    'glibc': [10.85, 12.12, 10.93, 11.46, 0.160, 0.216, 0.422, 0.590, 1.003, 1.562],
}
linuxscal_mem = {
    'Hoard': [11409, 11399, 10735, 11288, 183, 303, 543, 1035, 2018, 1796],
    'mimalloc': [19447, 19467, 19457, 19453, 182, 302, 542, 1034, 1526, 2028],
    'jemalloc': [19473, 19463, 19460, 19469, 62, 62, 74, 86, 74, 129],
    'glibc': [19455, 19452, 19464, 19472, 229, 351, 644, 1154, 1616, 2115],
}

# Phong: realloc-heavy workload (time in seconds, memory in MB)
phong_threads = [1, 2, 4, 8, 16, 32, 64, 128, 192, 256]
phong_time = {
    'Hoard': [1.08, 0.93, 2.08, 0.43, 4.45, 1.28, 0.43, 0.37, 0.40, 0.45],
    'mimalloc': [1.13, 1.12, 4.42, 0.97, 9.04, 1.83, 0.49, 0.23, 0.19, 0.19],
    'jemalloc': [1.15, 1.11, 6.82, 1.37, 13.02, 2.77, 0.64, 0.25, 0.22, 0.17],
    'glibc': [1.12, 1.11, 10.19, 2.14, 18.41, 4.14, 1.11, 0.61, 0.56, 0.54],
}
phong_mem = {
    'Hoard': [1934, 1928, 203, 204, 995, 1000, 1012, 1014, 1054, 1043],
    'mimalloc': [960, 945, 101, 107, 488, 510, 560, 640, 622, 614],
    'jemalloc': [951, 940, 100, 101, 481, 487, 508, 520, 535, 510],
    'glibc': [955, 957, 109, 110, 529, 541, 565, 550, 529, 534],
}

# =============================================================================
# PLOTTING FUNCTIONS
# =============================================================================

def plot_single_graph(threads, data, title, ylabel_left, filename, show_legend=True, higher_is_better=False):
    """Create a single graph normalized to Hoard.

    higher_is_better: If True, values > 1.0 mean better than Hoard (e.g., throughput).
                      If False, values > 1.0 mean worse than Hoard (e.g., time, memory).
    """
    fig, ax = plt.subplots(figsize=(7, 5))

    norm_data = normalize_to_hoard(data)

    # Find max y value for shading
    all_vals = [v for values in norm_data.values() for v in values]
    y_max = max(all_vals) * 1.1

    # Shade regions based on metric type
    # Green = Hoard is better, Pink = Hoard is worse
    if higher_is_better:
        # For throughput: values < 1.0 mean others have lower throughput = Hoard wins
        ax.axhspan(1.0, y_max, alpha=0.15, color='#e74c3c', zorder=0)  # Pink above (others better)
        ax.axhspan(0, 1.0, alpha=0.15, color='#2ecc71', zorder=0)      # Green below (Hoard better)
    else:
        # For time/memory: values > 1.0 mean others use more = Hoard wins
        ax.axhspan(1.0, y_max, alpha=0.15, color='#2ecc71', zorder=0)  # Green above (Hoard better)
        ax.axhspan(0, 1.0, alpha=0.15, color='#e74c3c', zorder=0)      # Pink below (others better)

    for name in ALLOCATORS:
        ax.plot(threads, norm_data[name],
                marker=MARKERS[name],
                color=COLORS[name],
                linewidth=2,
                markersize=7,
                label=name,
                markeredgecolor='white',
                markeredgewidth=0.5,
                zorder=2)

    # Hoard reference line at 1.0
    ax.axhline(y=1.0, color=COLORS['Hoard'], linestyle='-', alpha=0.8, linewidth=2, zorder=1)

    ax.set_xlabel('Threads')
    ax.set_ylabel(ylabel_left)
    ax.set_title(title, fontweight='bold', pad=10)

    if show_legend:
        ax.legend(loc='best', framealpha=0.95, edgecolor='#cccccc')

    ax.set_xscale('log', base=2)
    ax.set_xticks(threads)
    ax.set_xticklabels([str(t) for t in threads])
    ax.set_ylim(bottom=0, top=y_max)

    plt.tight_layout()
    save_fig(fig, filename)
    print(f'Generated {filename}.png')

# =============================================================================
# GENERATE INDIVIDUAL GRAPHS (8 total: time + memory for each benchmark)
# =============================================================================

# Larson (throughput - higher is better)
plot_single_graph(larson_threads, larson_throughput,
                  'Larson - Throughput\nHoard is 1.0 (green line). Below = lower throughput.',
                  'Throughput (relative to Hoard)', 'bench_larson_throughput', higher_is_better=True)
plot_single_graph(larson_threads, larson_mem,
                  'Larson - Memory Usage\nHoard is 1.0 (green line). Above = more memory.',
                  'Memory (relative to Hoard)', 'bench_larson_mem')

# threadtest
plot_single_graph(threadtest_threads, threadtest_time,
                  'threadtest - Execution Time\nHoard is 1.0 (green line). Above = slower.',
                  'Time (relative to Hoard)', 'bench_threadtest_time')
plot_single_graph(threadtest_threads, threadtest_mem,
                  'threadtest - Memory Usage\nHoard is 1.0 (green line). Above = more memory.',
                  'Memory (relative to Hoard)', 'bench_threadtest_mem')

# linux-scalability
plot_single_graph(linuxscal_threads, linuxscal_time,
                  'linux-scalability - Execution Time\nHoard is 1.0 (green line). Above = slower.',
                  'Time (relative to Hoard)', 'bench_linuxscal_time')
plot_single_graph(linuxscal_threads, linuxscal_mem,
                  'linux-scalability - Memory Usage\nHoard is 1.0 (green line). Above = more memory.',
                  'Memory (relative to Hoard)', 'bench_linuxscal_mem')

# Phong
plot_single_graph(phong_threads, phong_time,
                  'Phong - Execution Time\nHoard is 1.0 (green line). Above = slower.',
                  'Time (relative to Hoard)', 'bench_phong_time')
plot_single_graph(phong_threads, phong_mem,
                  'Phong - Memory Usage\nHoard is 1.0 (green line). Above = more memory.',
                  'Memory (relative to Hoard)', 'bench_phong_mem')

# =============================================================================
# SUMMARY GRAPH - Execution Time only (2x2 grid)
# =============================================================================

fig, axes = plt.subplots(2, 2, figsize=(12, 9))

def plot_summary_panel(ax, threads, data, title, higher_is_better=False):
    """Plot a single panel for the summary graph."""
    norm_data = normalize_to_hoard(data)

    # Find max y value for shading
    all_vals = [v for values in norm_data.values() for v in values]
    y_max = max(all_vals) * 1.1

    # Shade regions based on metric type
    if higher_is_better:
        ax.axhspan(1.0, y_max, alpha=0.15, color='#e74c3c', zorder=0)  # Pink above
        ax.axhspan(0, 1.0, alpha=0.15, color='#2ecc71', zorder=0)      # Green below
    else:
        ax.axhspan(1.0, y_max, alpha=0.15, color='#2ecc71', zorder=0)  # Green above
        ax.axhspan(0, 1.0, alpha=0.15, color='#e74c3c', zorder=0)      # Pink below

    for name in ALLOCATORS:
        ax.plot(threads, norm_data[name],
                marker=MARKERS[name],
                color=COLORS[name],
                linewidth=2,
                markersize=6,
                label=name,
                markeredgecolor='white',
                markeredgewidth=0.5,
                zorder=2)

    ax.axhline(y=1.0, color=COLORS['Hoard'], linestyle='-', alpha=0.8, linewidth=2, zorder=1)
    ax.set_xlabel('Threads')
    ax.set_ylabel('Relative to Hoard')
    ax.set_title(title, fontweight='bold', pad=8)
    ax.set_xscale('log', base=2)
    ax.set_xticks(threads)
    ax.set_xticklabels([str(t) for t in threads], fontsize=9)
    ax.set_ylim(bottom=0, top=y_max)

plot_summary_panel(axes[0, 0], larson_threads, larson_throughput, 'Larson (throughput)', higher_is_better=True)
plot_summary_panel(axes[0, 1], threadtest_threads, threadtest_time, 'threadtest')
plot_summary_panel(axes[1, 0], linuxscal_threads, linuxscal_time, 'linux-scalability')
plot_summary_panel(axes[1, 1], phong_threads, phong_time, 'Phong')

# Shared legend at bottom
handles = [plt.Line2D([0], [0], marker=MARKERS[name], color=COLORS[name],
                      linewidth=2, markersize=7, markeredgecolor='white', markeredgewidth=0.5)
           for name in ALLOCATORS]
fig.legend(handles, ALLOCATORS, loc='upper center', ncol=4, bbox_to_anchor=(0.5, 0.02),
           framealpha=0.95, edgecolor='#cccccc')

fig.suptitle('Execution Time Summary (192-core NUMA system)\nHoard is 1.0 (green line). Above = slower.',
             fontweight='bold', y=0.98)

plt.tight_layout()
plt.subplots_adjust(bottom=0.10, top=0.90)
save_fig(fig, 'bench_summary_time')
print('Generated bench_summary_time.png')

# =============================================================================
# SUMMARY GRAPH - Memory only (2x2 grid)
# =============================================================================

fig, axes = plt.subplots(2, 2, figsize=(12, 9))

plot_summary_panel(axes[0, 0], larson_threads, larson_mem, 'Larson')
plot_summary_panel(axes[0, 1], threadtest_threads, threadtest_mem, 'threadtest')
plot_summary_panel(axes[1, 0], linuxscal_threads, linuxscal_mem, 'linux-scalability')
plot_summary_panel(axes[1, 1], phong_threads, phong_mem, 'Phong')

# Shared legend at bottom
fig.legend(handles, ALLOCATORS, loc='upper center', ncol=4, bbox_to_anchor=(0.5, 0.02),
           framealpha=0.95, edgecolor='#cccccc')

fig.suptitle('Memory Usage Summary (192-core NUMA system)\nHoard is 1.0 (green line). Above = more memory.',
             fontweight='bold', y=0.98)

plt.tight_layout()
plt.subplots_adjust(bottom=0.10, top=0.90)
save_fig(fig, 'bench_summary_mem')
print('Generated bench_summary_mem.png')

print('\nAll graphs generated in doc/')
