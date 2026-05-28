#!/usr/bin/env python3
"""Generate NUMA benchmark graphs for Hoard PR.

Graphs show:
1. Throughput comparison (ops/sec) across thread counts
2. Remote memory access percentages from perf counters
"""

import matplotlib.pyplot as plt
import numpy as np

# Use Times/serif font like academic papers
plt.rcParams.update({
    'font.family': 'serif',
    'font.serif': ['Times New Roman', 'Times', 'DejaVu Serif'],
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

COLORS = {
    'Hoard': '#2ecc71',
    'mimalloc': '#3498db',
    'jemalloc': '#e74c3c',
    'glibc': '#9b59b6',
}

MARKERS = {
    'Hoard': 'o',
    'mimalloc': 's',
    'jemalloc': '^',
    'glibc': 'D',
}

# NUMA stress test throughput data (M ops/sec, averaged over 3 runs)
# 256B objects, 20 touches per object to isolate NUMA effects
numa_threads = [16, 32, 64, 128]
numa_throughput = {
    'Hoard': [2.76, 5.43, 10.9, 19.5],
    'mimalloc': [2.42, 4.94, 10.1, 13.6],
    'jemalloc': [2.40, 4.90, 10.0, 14.1],
    'glibc': [2.42, 5.07, 9.9, 12.1],
}

# Perf counter data: remote memory access percentages (64 threads)
perf_allocators = ['Hoard\n(NUMA opt)', 'Hoard\n(no NUMA)', 'mimalloc', 'jemalloc', 'glibc']
perf_remote_mem = [33.4, 40.6, 18.3, 15.5, 51.4]
perf_remote_cache = [37.1, 45.5, 33.4, 40.8, 47.8]
perf_throughput = [83.3, 76.4, 87.0, 53.6, 68.7]

def save_fig(fig, name):
    fig.savefig(f'/home/emerydb/git/Hoard/doc/{name}.png', dpi=150, bbox_inches='tight',
                facecolor='white', edgecolor='none')
    plt.close(fig)

# Graph 1: NUMA throughput comparison
fig, ax = plt.subplots(figsize=(8, 5))

for name in ['Hoard', 'mimalloc', 'jemalloc', 'glibc']:
    ax.plot(numa_threads, numa_throughput[name],
            marker=MARKERS[name],
            color=COLORS[name],
            linewidth=2,
            markersize=8,
            label=name,
            markeredgecolor='white',
            markeredgewidth=0.5)

ax.set_xlabel('Threads')
ax.set_ylabel('Throughput (M ops/sec)')
ax.set_title('NUMA Stress Test - Cross-Node Allocation\n(higher is better)', fontweight='bold')
ax.legend(loc='upper left', framealpha=0.95)
ax.set_xscale('log', base=2)
ax.set_xticks(numa_threads)
ax.set_xticklabels([str(t) for t in numa_threads])
ax.set_ylim(bottom=0)

plt.tight_layout()
save_fig(fig, 'numa_throughput')
print('Generated numa_throughput.png')

# Graph 2: Perf counter comparison (bar chart)
fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(12, 5))

x = np.arange(len(perf_allocators))
width = 0.35

# Left: Remote memory percentages
bars1 = ax1.bar(x - width/2, perf_remote_mem, width, label='Memory I/O', color='#3498db')
bars2 = ax1.bar(x + width/2, perf_remote_cache, width, label='Cache Fills', color='#e74c3c')

ax1.set_ylabel('Remote Access (%)')
ax1.set_title('Remote NUMA Access (64 threads)\n(lower is better)', fontweight='bold')
ax1.set_xticks(x)
ax1.set_xticklabels(perf_allocators)
ax1.legend(loc='upper right')
ax1.set_ylim(0, 60)
ax1.axhline(y=50, color='gray', linestyle='--', alpha=0.5, label='50% (no locality)')

# Add value labels on bars
for bar in bars1:
    height = bar.get_height()
    ax1.annotate(f'{height:.1f}%',
                xy=(bar.get_x() + bar.get_width() / 2, height),
                xytext=(0, 3), textcoords="offset points",
                ha='center', va='bottom', fontsize=9)

for bar in bars2:
    height = bar.get_height()
    ax1.annotate(f'{height:.1f}%',
                xy=(bar.get_x() + bar.get_width() / 2, height),
                xytext=(0, 3), textcoords="offset points",
                ha='center', va='bottom', fontsize=9)

# Right: Throughput comparison
colors = ['#2ecc71', '#90EE90', '#3498db', '#e74c3c', '#9b59b6']
bars3 = ax2.bar(x, perf_throughput, width=0.6, color=colors)

ax2.set_ylabel('Throughput (M ops/sec)')
ax2.set_title('Throughput at 64 Threads\n(higher is better)', fontweight='bold')
ax2.set_xticks(x)
ax2.set_xticklabels(perf_allocators)
ax2.set_ylim(0, 100)

for bar in bars3:
    height = bar.get_height()
    ax2.annotate(f'{height:.1f}',
                xy=(bar.get_x() + bar.get_width() / 2, height),
                xytext=(0, 3), textcoords="offset points",
                ha='center', va='bottom', fontsize=9)

plt.tight_layout()
save_fig(fig, 'numa_perf_counters')
print('Generated numa_perf_counters.png')

# Graph 3: Hoard speedup over other allocators
fig, ax = plt.subplots(figsize=(8, 5))

hoard_data = np.array(numa_throughput['Hoard'])
for name in ['mimalloc', 'jemalloc', 'glibc']:
    other_data = np.array(numa_throughput[name])
    speedup = hoard_data / other_data
    ax.plot(numa_threads, speedup,
            marker=MARKERS[name],
            color=COLORS[name],
            linewidth=2,
            markersize=8,
            label=f'vs {name}',
            markeredgecolor='white',
            markeredgewidth=0.5)

ax.axhline(y=1.0, color='black', linestyle='-', alpha=0.5, linewidth=1)
ax.axhspan(1.0, 2.0, alpha=0.1, color='#2ecc71')
ax.axhspan(0.5, 1.0, alpha=0.1, color='#e74c3c')

ax.set_xlabel('Threads')
ax.set_ylabel('Hoard Speedup (x)')
ax.set_title('Hoard Speedup on NUMA Stress Test\n(above 1.0 = Hoard faster)', fontweight='bold')
ax.legend(loc='upper right', framealpha=0.95)
ax.set_xscale('log', base=2)
ax.set_xticks(numa_threads)
ax.set_xticklabels([str(t) for t in numa_threads])
ax.set_ylim(0.5, 2.0)

plt.tight_layout()
save_fig(fig, 'numa_speedup')
print('Generated numa_speedup.png')

print('\nAll NUMA graphs generated in doc/')
