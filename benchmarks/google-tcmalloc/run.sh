#!/bin/bash
# Run the binaries produced by build.sh. Same arguments, thread counts and
# medians as the cross-allocator table in README.md.
#
# Three configurations:
#   tcmalloc-percpu  TCMalloc in per-CPU mode, its design point. Needs glibc's
#                    own rseq registration OFF (GLIBC_TUNABLES, see README):
#                    a thread can have only one rseq area, and if glibc took it
#                    TCMalloc silently falls back to per-thread caches.
#   tcmalloc-asis    TCMalloc with the environment untouched -- what an
#                    unmodified program gets (per-thread fallback wherever
#                    glibc registers rseq: upstream >= 2.35, and backports
#                    such as Amazon Linux 2023's 2.34).
#   default          Same Bazel toolchain, default allocator (glibc).
#
# Other allocators MUST be compared on these same binaries, not on the
# make-built ones: how badly a false-sharing allocator does depends on how
# fast the benchmark's write loop is, and the clang/Bazel build's loop is
# faster than the gcc/make one (glibc's cache-scratch speedup moved from 3.7x
# to 1.8x between them). The -default binaries use the dynamically linked
# default allocator, so each allocator in PRELOADS is LD_PRELOADed onto
# exactly the code TCMalloc was statically linked into. Each preload is
# checked with LD_DEBUG=bindings to really serve malloc before it is timed.
#
# Usage: benchmarks/google-tcmalloc/run.sh
# Env:   WORK, REPS (default 5),
#        PRELOADS="name=/path/lib.so name2=/path/lib2.so ..." (optional)
set -euo pipefail
OUT=${WORK:-$HOME/hoard-bench/google-tcmalloc}/out
REPS=${REPS:-5}
NO_GLIBC_RSEQ=glibc.pthread.rseq=0
med() { sort -g | awk '{a[NR]=$1} END{print a[int((NR+1)/2)]}'; }

# cfg -> binary suffix and environment. A cfg named in PRELOADS runs the
# -default binary with that library preloaded.
declare -A PRE=(); NAMES=""
for kv in ${PRELOADS:-}; do PRE[${kv%%=*}]=${kv#*=}; NAMES="$NAMES ${kv%%=*}"; done
bin_suffix() { case $1 in tcmalloc-*) echo tcmalloc;; *) echo default;; esac; }
env_for() {
  case $1 in
    tcmalloc-percpu) echo "GLIBC_TUNABLES=$NO_GLIBC_RSEQ" ;;
    tcmalloc-asis|default) echo "GLIBC_TUNABLES=" ;;
    *) echo "GLIBC_TUNABLES= LD_PRELOAD=${PRE[$1]}" ;;
  esac
}
secs() { local cfg=$1; shift; env $(env_for "$cfg") "$@" 2>/dev/null | grep -oE "[0-9]+\.[0-9]+ seconds" | grep -oE "[0-9.]+"; }

CFGS="tcmalloc-percpu tcmalloc-asis default$NAMES"
for cfg in tcmalloc-percpu tcmalloc-asis; do
  echo "[$cfg] $(env $(env_for $cfg) "$OUT/probe" | tr '\n' ' ')"
done
for cfg in $NAMES; do
  who=$(env $(env_for $cfg) LD_DEBUG=bindings "$OUT/cache-scratch-default" 1 1 8 1 2>&1 \
        | grep -m1 "normal symbol \`malloc'" | sed -E 's/.*to (\S+) .*/\1/' || true)
  # (|| true: grep -m1 exits early, the benchmark then dies of SIGPIPE, and
  #  pipefail would abort the whole script silently. The comparison below
  #  is what actually decides.)
  [ "$who" = "${PRE[$cfg]}" ] || { echo "[$cfg] preload does NOT serve malloc (bound to: ${who:-?})" >&2; exit 1; }
  echo "[$cfg] malloc bound to $who"
done
for bench in cache-scratch cache-thrash; do
  echo "## $bench (median of $REPS s; speedup = t1/tN, ideal = N)"
  for cfg in $CFGS; do
    b="$OUT/$bench-$(bin_suffix $cfg)"
    t1=$(for i in $(seq $REPS); do secs $cfg "$b" 1 1000 8 1000000; done | med)
    t8=$(for i in $(seq $REPS); do secs $cfg "$b" 8 1000 8 1000000; done | med)
    t32=$(for i in $(seq $REPS); do secs $cfg "$b" 32 1000 8 1000000; done | med)
    printf "%-16s 1t=%-8s 8t=%-8s 32t=%-8s x8=%5.2f x32=%5.2f\n" "$cfg" "$t1" "$t8" "$t32" \
      "$(awk "BEGIN{print $t1/$t8}")" "$(awk "BEGIN{print $t1/$t32}")"
  done
done
echo "## ci cache_scratch 8t / 32t (100000 iters x 100 reps; wall s, median of $REPS)"
for cfg in $CFGS; do
  b="$OUT/cache_scratch_ci-$(bin_suffix $cfg)"
  w() { for i in $(seq $REPS); do s=$(date +%s%N); env $(env_for $cfg) "$b" "$1" 100000 8 100 >/dev/null 2>&1; e=$(date +%s%N); awk "BEGIN{print ($e-$s)/1e9}"; done | med; }
  printf "%-16s 8t=%-9s 32t=%-9s\n" "$cfg" "$(w 8)" "$(w 32)"
done
