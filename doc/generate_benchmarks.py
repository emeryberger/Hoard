#!/usr/bin/env python3
"""Generate benchmark graphs for Hoard PR.

All results are normalized to Hoard (Hoard = 1.0, the green horizontal line).
- Above the line = slower/more memory than Hoard
- Below the line = faster/less memory than Hoard

Left y-axis shows normalized values, right y-axis shows actual values.
"""

import matplotlib.pyplot as plt
import matplotlib as mpl
import seaborn as sns
import numpy as np

# Set up style with clean sans-serif font
plt.rcParams['font.family'] = 'sans-serif'
plt.rcParams['font.sans-serif'] = ['DejaVu Sans', 'Arial', 'Helvetica']
plt.rcParams['figure.facecolor'] = 'white'
plt.rcParams['axes.facecolor'] = 'white'
plt.rcParams['axes.edgecolor'] = '#333333'
plt.rcParams['axes.linewidth'] = 0.8
plt.rcParams['axes.titleweight'] = 'bold'
plt.rcParams['grid.color'] = '#cccccc'
plt.rcParams['grid.linewidth'] = 0.5
sns.set_theme(style="whitegrid", context="paper", font_scale=1.2,
              rc={'font.family': 'sans-serif', 'font.sans-serif': ['DejaVu Sans', 'Arial', 'Helvetica']})

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

def plot_panel(ax, threads, data, ylabel_left, unit_label, show_legend=True):
    """Plot normalized line graph with dual y-axes.

    Left axis: normalized to Hoard (1.0)
    Right axis: actual values in specified unit
    """
    norm_data = normalize_to_hoard(data)
    hoard_vals = data['Hoard']

    for name in ALLOCATORS:
        ax.plot(threads, norm_data[name],
                marker=MARKERS[name],
                color=COLORS[name],
                linewidth=2.5,
                markersize=8,
                label=name,
                markeredgecolor='white',
                markeredgewidth=0.8)

    # Hoard reference line at 1.0
    ax.axhline(y=1.0, color=COLORS['Hoard'], linestyle='-', alpha=0.6, linewidth=2.5)

    ax.set_xlabel('Threads', fontsize=12, fontfamily='sans-serif')
    ax.set_ylabel(ylabel_left, fontsize=12, fontfamily='sans-serif')

    if show_legend:
        ax.legend(loc='best', framealpha=0.95, edgecolor='#cccccc', fontsize=10)

    ax.set_xscale('log', base=2)
    ax.set_xticks(threads)
    ax.set_xticklabels([str(t) for t in threads], fontsize=10)
    ax.tick_params(axis='y', labelsize=10)
    ax.grid(True, alpha=0.4, linestyle='-')
    ax.set_ylim(bottom=0)

    # Create secondary y-axis with actual values
    ax2 = ax.twinx()

    # Get the current y-axis limits and convert to actual values
    y_min, y_max = ax.get_ylim()

    # Use the average Hoard value as reference for the right axis scale
    avg_hoard = np.mean(hoard_vals)

    ax2.set_ylim(y_min * avg_hoard, y_max * avg_hoard)
    ax2.set_ylabel(unit_label, fontsize=11, fontfamily='sans-serif', color='#666666')
    ax2.tick_params(axis='y', labelsize=10, colors='#666666')

    # Format the right axis labels appropriately
    if 'seconds' in unit_label.lower():
        ax2.yaxis.set_major_formatter(plt.FuncFormatter(lambda x, p: f'{x:.2f}'))
    else:  # MB
        ax2.yaxis.set_major_formatter(plt.FuncFormatter(lambda x, p: f'{x:.0f}'))

    return ax2

def plot_benchmark(threads, time_data, mem_data, bench_name, filename):
    """Create a two-panel figure with time and memory."""
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(14, 5))

    plot_panel(ax1, threads, time_data, 'Time (relative to Hoard)', 'seconds', show_legend=False)
    ax1.set_title('Execution Time', fontsize=13, fontfamily='sans-serif', fontweight='bold', pad=10)

    plot_panel(ax2, threads, mem_data, 'Memory (relative to Hoard)', 'MB', show_legend=False)
    ax2.set_title('Memory Usage', fontsize=13, fontfamily='sans-serif', fontweight='bold', pad=10)

    # Shared legend at bottom
    handles = [plt.Line2D([0], [0], marker=MARKERS[name], color=COLORS[name],
                          linewidth=2.5, markersize=8, markeredgecolor='white', markeredgewidth=0.8)
               for name in ALLOCATORS]
    fig.legend(handles, ALLOCATORS, loc='upper center', ncol=4, bbox_to_anchor=(0.5, 0.02),
               framealpha=0.95, edgecolor='#cccccc', fontsize=11)

    # Main title with explanation
    fig.suptitle(f'{bench_name}\nHoard is the green line (1.0). Above = slower/more memory. Below = faster/less memory.',
                 fontsize=13, fontfamily='sans-serif', fontweight='bold', y=0.98)

    plt.tight_layout()
    plt.subplots_adjust(bottom=0.18, top=0.85)
    save_fig(fig, filename)
    print(f'Generated {filename}.png')

# =============================================================================
# GENERATE PLOTS
# =============================================================================

# Individual benchmark plots - ALL with BOTH time AND memory
plot_benchmark(larson_threads, larson_time, larson_mem, 'Larson (server workload)', 'bench_larson')
plot_benchmark(threadtest_threads, threadtest_time, threadtest_mem, 'threadtest (malloc/free throughput)', 'bench_threadtest')
plot_benchmark(linuxscal_threads, linuxscal_time, linuxscal_mem, 'linux-scalability', 'bench_linuxscal')
plot_benchmark(phong_threads, phong_time, phong_mem, 'Phong (realloc-heavy)', 'bench_phong')

# Summary plot - 2x2 grid showing key metrics for each benchmark
fig, axes = plt.subplots(2, 2, figsize=(14, 10))

def plot_summary_panel(ax, threads, data, title, unit_label):
    """Plot a single panel for the summary graph."""
    norm_data = normalize_to_hoard(data)
    hoard_vals = data['Hoard']

    for name in ALLOCATORS:
        ax.plot(threads, norm_data[name],
                marker=MARKERS[name],
                color=COLORS[name],
                linewidth=2.5,
                markersize=8,
                label=name,
                markeredgecolor='white',
                markeredgewidth=0.8)

    ax.axhline(y=1.0, color=COLORS['Hoard'], linestyle='-', alpha=0.6, linewidth=2.5)
    ax.set_xlabel('Threads', fontsize=11, fontfamily='sans-serif')
    ax.set_ylabel('Relative to Hoard', fontsize=11, fontfamily='sans-serif')
    ax.set_title(title, fontsize=12, fontfamily='sans-serif', fontweight='bold', pad=8)
    ax.set_xscale('log', base=2)
    ax.set_xticks(threads)
    ax.set_xticklabels([str(t) for t in threads], fontsize=9)
    ax.tick_params(axis='y', labelsize=9)
    ax.grid(True, alpha=0.4, linestyle='-')
    ax.set_ylim(bottom=0)

    # Secondary axis
    ax2 = ax.twinx()
    y_min, y_max = ax.get_ylim()
    avg_hoard = np.mean(hoard_vals)
    ax2.set_ylim(y_min * avg_hoard, y_max * avg_hoard)
    ax2.set_ylabel(unit_label, fontsize=10, fontfamily='sans-serif', color='#666666')
    ax2.tick_params(axis='y', labelsize=9, colors='#666666')
    if 'seconds' in unit_label.lower():
        ax2.yaxis.set_major_formatter(plt.FuncFormatter(lambda x, p: f'{x:.2f}'))
    else:
        ax2.yaxis.set_major_formatter(plt.FuncFormatter(lambda x, p: f'{x:.0f}'))

plot_summary_panel(axes[0, 0], larson_threads, larson_mem, 'Larson - Memory', 'MB')
plot_summary_panel(axes[0, 1], threadtest_threads, threadtest_time, 'threadtest - Time', 'seconds')
plot_summary_panel(axes[1, 0], linuxscal_threads, linuxscal_time, 'linux-scalability - Time', 'seconds')
plot_summary_panel(axes[1, 1], phong_threads, phong_time, 'Phong - Time', 'seconds')

# Shared legend
handles = [plt.Line2D([0], [0], marker=MARKERS[name], color=COLORS[name],
                      linewidth=2.5, markersize=8, markeredgecolor='white', markeredgewidth=0.8)
           for name in ALLOCATORS]
fig.legend(handles, ALLOCATORS, loc='upper center', ncol=4, bbox_to_anchor=(0.5, 0.02),
           framealpha=0.95, edgecolor='#cccccc', fontsize=11)

fig.suptitle('Hoard Performance (192-core NUMA system)\nHoard is the green line (1.0). Above = slower/more memory. Below = faster/less memory.',
             fontsize=14, fontfamily='sans-serif', fontweight='bold', y=0.98)

plt.tight_layout()
plt.subplots_adjust(bottom=0.08, top=0.88)
save_fig(fig, 'bench_summary')
print('Generated bench_summary.png')

print('\nAll graphs generated in doc/')
