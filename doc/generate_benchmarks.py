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

# Larson: server workload simulation
larson_threads = [16, 32, 64, 128, 192, 256]
larson_time = {
    'Hoard': [10.08, 10.16, 10.34, 10.80, 11.09, 10.63],
    'mimalloc': [10.08, 10.18, 10.39, 10.96, 11.42, 10.31],
    'jemalloc': [10.05, 10.10, 10.19, 10.39, 10.57, 10.57],
    'glibc': [10.08, 10.16, 10.34, 10.80, 11.09, 10.63],
}
larson_mem = {
    'Hoard': [69, 110, 222, 714, 929, 1344],
    'mimalloc': [128, 234, 517, 1167, 1918, 2576],
    'jemalloc': [93, 185, 424, 928, 1639, 2364],
    'glibc': [115, 230, 444, 882, 1291, 1687],
}

# threadtest: malloc/free throughput
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

# linux-scalability: malloc/free pairs
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

# Phong: realloc-heavy workload
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

def plot_single_graph(threads, data, title, ylabel_left, unit_label, filename, show_legend=True):
    """Create a single graph with dual y-axes."""
    fig, ax = plt.subplots(figsize=(7, 5))

    norm_data = normalize_to_hoard(data)
    hoard_vals = data['Hoard']

    for name in ALLOCATORS:
        ax.plot(threads, norm_data[name],
                marker=MARKERS[name],
                color=COLORS[name],
                linewidth=2,
                markersize=7,
                label=name,
                markeredgecolor='white',
                markeredgewidth=0.5)

    # Hoard reference line at 1.0
    ax.axhline(y=1.0, color=COLORS['Hoard'], linestyle='-', alpha=0.6, linewidth=2)

    ax.set_xlabel('Threads')
    ax.set_ylabel(ylabel_left)
    ax.set_title(title, fontweight='bold', pad=10)

    if show_legend:
        ax.legend(loc='best', framealpha=0.95, edgecolor='#cccccc')

    ax.set_xscale('log', base=2)
    ax.set_xticks(threads)
    ax.set_xticklabels([str(t) for t in threads])
    ax.set_ylim(bottom=0)

    # Create secondary y-axis with actual values
    ax2 = ax.twinx()
    y_min, y_max = ax.get_ylim()
    avg_hoard = np.mean(hoard_vals)
    ax2.set_ylim(y_min * avg_hoard, y_max * avg_hoard)
    ax2.set_ylabel(unit_label, color='#666666')
    ax2.tick_params(axis='y', colors='#666666')

    if 'seconds' in unit_label.lower():
        ax2.yaxis.set_major_formatter(plt.FuncFormatter(lambda x, p: f'{x:.2f}'))
    else:
        ax2.yaxis.set_major_formatter(plt.FuncFormatter(lambda x, p: f'{x:.0f}'))

    plt.tight_layout()
    save_fig(fig, filename)
    print(f'Generated {filename}.png')

# =============================================================================
# GENERATE INDIVIDUAL GRAPHS (8 total: time + memory for each benchmark)
# =============================================================================

# Larson
plot_single_graph(larson_threads, larson_time,
                  'Larson - Execution Time\nHoard is 1.0 (green line). Above = slower.',
                  'Time (relative to Hoard)', 'seconds', 'bench_larson_time')
plot_single_graph(larson_threads, larson_mem,
                  'Larson - Memory Usage\nHoard is 1.0 (green line). Above = more memory.',
                  'Memory (relative to Hoard)', 'MB', 'bench_larson_mem')

# threadtest
plot_single_graph(threadtest_threads, threadtest_time,
                  'threadtest - Execution Time\nHoard is 1.0 (green line). Above = slower.',
                  'Time (relative to Hoard)', 'seconds', 'bench_threadtest_time')
plot_single_graph(threadtest_threads, threadtest_mem,
                  'threadtest - Memory Usage\nHoard is 1.0 (green line). Above = more memory.',
                  'Memory (relative to Hoard)', 'MB', 'bench_threadtest_mem')

# linux-scalability
plot_single_graph(linuxscal_threads, linuxscal_time,
                  'linux-scalability - Execution Time\nHoard is 1.0 (green line). Above = slower.',
                  'Time (relative to Hoard)', 'seconds', 'bench_linuxscal_time')
plot_single_graph(linuxscal_threads, linuxscal_mem,
                  'linux-scalability - Memory Usage\nHoard is 1.0 (green line). Above = more memory.',
                  'Memory (relative to Hoard)', 'MB', 'bench_linuxscal_mem')

# Phong
plot_single_graph(phong_threads, phong_time,
                  'Phong - Execution Time\nHoard is 1.0 (green line). Above = slower.',
                  'Time (relative to Hoard)', 'seconds', 'bench_phong_time')
plot_single_graph(phong_threads, phong_mem,
                  'Phong - Memory Usage\nHoard is 1.0 (green line). Above = more memory.',
                  'Memory (relative to Hoard)', 'MB', 'bench_phong_mem')

# =============================================================================
# SUMMARY GRAPH - Execution Time only (2x2 grid)
# =============================================================================

fig, axes = plt.subplots(2, 2, figsize=(12, 9))

def plot_summary_panel(ax, threads, data, title, unit_label, show_legend=False):
    """Plot a single panel for the summary graph."""
    norm_data = normalize_to_hoard(data)
    hoard_vals = data['Hoard']

    for name in ALLOCATORS:
        ax.plot(threads, norm_data[name],
                marker=MARKERS[name],
                color=COLORS[name],
                linewidth=2,
                markersize=6,
                label=name,
                markeredgecolor='white',
                markeredgewidth=0.5)

    ax.axhline(y=1.0, color=COLORS['Hoard'], linestyle='-', alpha=0.6, linewidth=2)
    ax.set_xlabel('Threads')
    ax.set_ylabel('Relative to Hoard')
    ax.set_title(title, fontweight='bold', pad=8)
    ax.set_xscale('log', base=2)
    ax.set_xticks(threads)
    ax.set_xticklabels([str(t) for t in threads], fontsize=9)
    ax.set_ylim(bottom=0)

    # Secondary axis
    ax2 = ax.twinx()
    y_min, y_max = ax.get_ylim()
    avg_hoard = np.mean(hoard_vals)
    ax2.set_ylim(y_min * avg_hoard, y_max * avg_hoard)
    ax2.set_ylabel(unit_label, color='#666666', fontsize=10)
    ax2.tick_params(axis='y', colors='#666666', labelsize=9)
    if 'seconds' in unit_label.lower():
        ax2.yaxis.set_major_formatter(plt.FuncFormatter(lambda x, p: f'{x:.2f}'))
    else:
        ax2.yaxis.set_major_formatter(plt.FuncFormatter(lambda x, p: f'{x:.0f}'))

plot_summary_panel(axes[0, 0], larson_threads, larson_time, 'Larson', 'seconds')
plot_summary_panel(axes[0, 1], threadtest_threads, threadtest_time, 'threadtest', 'seconds')
plot_summary_panel(axes[1, 0], linuxscal_threads, linuxscal_time, 'linux-scalability', 'seconds')
plot_summary_panel(axes[1, 1], phong_threads, phong_time, 'Phong', 'seconds')

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

plot_summary_panel(axes[0, 0], larson_threads, larson_mem, 'Larson', 'MB')
plot_summary_panel(axes[0, 1], threadtest_threads, threadtest_mem, 'threadtest', 'MB')
plot_summary_panel(axes[1, 0], linuxscal_threads, linuxscal_mem, 'linux-scalability', 'MB')
plot_summary_panel(axes[1, 1], phong_threads, phong_mem, 'Phong', 'MB')

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
