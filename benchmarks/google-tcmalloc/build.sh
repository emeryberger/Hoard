#!/bin/bash
# Build Hoard's false-sharing benchmarks against Google's TCMalloc
# (https://github.com/google/tcmalloc -- NOT gperftools), the way TCMalloc
# supports: statically linked via Bazel's cc_binary `malloc` attribute.
# TCMalloc does not support LD_PRELOAD / dynamic loading
# (docs/compatibility.md), so no shared library is built.
#
# Linux x86_64 / aarch64 only. Needs: git, curl, sha256sum, a C++17 compiler.
# Everything else (Bazelisk, Bazel, Abseil, ...) is fetched into $WORK.
#
# Usage: benchmarks/google-tcmalloc/build.sh          # see README.md
# Env:   WORK=/tmp/google-tcmalloc  TCMALLOC_COMMIT=<sha>  BAZEL_VERSION=<x.y.z>

set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
BENCH=$(cd "$HERE/.." && pwd)                  # Hoard's benchmarks/ directory
WORK=${WORK:-/tmp/google-tcmalloc}

# Pins: the versions these instructions were validated with (2026-11-18).
TCMALLOC_COMMIT=${TCMALLOC_COMMIT:-6cb4939e97a057a7ecea2d277e0d77706c2fb820}
BAZELISK_VERSION=${BAZELISK_VERSION:-v1.30.0}
BAZEL_VERSION=${BAZEL_VERSION:-9.3.2}          # TCMalloc CI targets Bazel 9
LLVM_VERSION=${LLVM_VERSION:-23.1.6}           # see step 4 for why a compiler is pinned
# SHA-256 digests GitHub publishes for the llvmorg-23.1.6 release assets.
LLVM_SHA_X64=59e14234f145927f982b50627e8bafaf2fca50151b7d973d6a0b40796ddcfe9a
LLVM_SHA_ARM64=be9ce7840a8f5273cb22305cc6227f737f05735d84df889dad0e123660902698

case "$(uname -m)" in
  x86_64)  ARCH=amd64 ;;
  aarch64) ARCH=arm64 ;;
  *) echo "unsupported arch $(uname -m)" >&2; exit 1 ;;
esac

mkdir -p "$WORK/bin"
cd "$WORK"

# 1. Bazelisk (the official Bazel launcher), verified against its published
#    SHA-256. It then downloads the pinned Bazel itself.
BAZELISK="$WORK/bin/bazelisk"
if [ ! -x "$BAZELISK" ]; then
  base="https://github.com/bazelbuild/bazelisk/releases/download/$BAZELISK_VERSION"
  curl -fsSL -o "$BAZELISK.tmp" "$base/bazelisk-linux-$ARCH"
  want=$(curl -fsSL "$base/bazelisk-linux-$ARCH.sha256" | awk '{print $1}')
  got=$(sha256sum "$BAZELISK.tmp" | awk '{print $1}')
  if [ "$want" != "$got" ]; then
    echo "bazelisk checksum mismatch: want $want got $got" >&2; exit 1
  fi
  chmod +x "$BAZELISK.tmp" && mv "$BAZELISK.tmp" "$BAZELISK"
fi
export USE_BAZEL_VERSION="$BAZEL_VERSION"

# 2. TCMalloc at the pinned commit.
if [ ! -d tcmalloc/.git ]; then
  git init -q tcmalloc
  git -C tcmalloc remote add origin https://github.com/google/tcmalloc.git
fi
git -C tcmalloc fetch -q --depth 1 origin "$TCMALLOC_COMMIT"
git -C tcmalloc checkout -q --detach FETCH_HEAD
echo "tcmalloc: $(git -C tcmalloc log -1 --format='%H %cs %s')"

# 3. A package inside the TCMalloc workspace holding Hoard's benchmark
#    sources. Each benchmark is built twice: with TCMalloc, and with the
#    default allocator (glibc) as a toolchain control -- the default build must
#    reproduce the usual glibc numbers, or the Bazel build itself is suspect.
PKG=tcmalloc/hoard_bench
rm -rf "$PKG" && mkdir -p "$PKG"
cp "$BENCH/cache-scratch/cache-scratch.cpp" "$BENCH/cache-thrash/cache-thrash.cpp" \
   "$BENCH/common/fred.h" "$BENCH/common/cpuinfo.h" "$BENCH/common/timer.h" \
   "$BENCH/ci/cache_scratch.c" "$BENCH/ci/bench_common.h" \
   "$HERE/probe.cc" "$PKG/"
cat > "$PKG/BUILD.bazel" <<'EOF'
load("@rules_cc//cc:cc_binary.bzl", "cc_binary")

HL = ["fred.h", "cpuinfo.h", "timer.h"]
OPTS = ["-O3", "-DNDEBUG", "-g", "-w"]   # match benchmarks/Makefile.inc

[cc_binary(
    name = name + suffix,
    srcs = [src] + HL,
    copts = OPTS,
    linkopts = ["-lpthread"],
    malloc = m,
) for name, src in [("cache-scratch", "cache-scratch.cpp"), ("cache-thrash", "cache-thrash.cpp")]
  for suffix, m in [("", "//tcmalloc"), ("-default", "@bazel_tools//tools/cpp:malloc")]]

[cc_binary(
    name = "cache_scratch_ci" + suffix,
    srcs = ["cache_scratch.c", "bench_common.h"],
    copts = OPTS,
    linkopts = ["-lpthread"],
    malloc = m,
) for suffix, m in [("", "//tcmalloc"), ("-default", "@bazel_tools//tools/cpp:malloc")]]

# Confirms TCMalloc (and its per-CPU caches) is what a binary built this way
# actually allocates with.
cc_binary(
    name = "probe",
    srcs = ["probe.cc"],
    deps = ["//tcmalloc:malloc_extension"],
    malloc = "//tcmalloc",
)
EOF

# 4. Compiler. TCMalloc's CI builds in Abseil's "oss-linux-latest" container
#    (recent Clang/GCC), and that is the only toolchain it tests. Its version
#    check in tcmalloc/internal/config.h (GCC >= 12.1, Clang >= 11) is NOT a
#    reliable minimum: on 2026-11-18 Clang 15 passed the check but failed to
#    compile span.h (restrict-qualified member calls), and GCC 11.5 was
#    rejected outright. So by default this pins an official LLVM release
#    (SHA-256 from its GitHub release asset), unpacked under $WORK -- nothing
#    is installed system-wide. Set CC=/path/to/clang-or-gcc to use your own.
#    Bazel's auto-configured local toolchain uses what CC names; --repo_env
#    makes it re-detect the toolchain when CC changes.
if [ -z "${CC:-}" ]; then
  case "$ARCH" in
    amd64) LLVM_ASSET=LLVM-$LLVM_VERSION-Linux-X64.tar.zst;   LLVM_SHA=$LLVM_SHA_X64 ;;
    arm64) LLVM_ASSET=LLVM-$LLVM_VERSION-Linux-ARM64.tar.zst; LLVM_SHA=$LLVM_SHA_ARM64 ;;
  esac
  LLVM_DIR="$WORK/${LLVM_ASSET%.tar.zst}"
  if [ ! -x "$LLVM_DIR/bin/clang" ]; then
    sha() { sha256sum "$1" | awk '{print $1}'; }
    if [ ! -f "$WORK/$LLVM_ASSET" ] || [ "$(sha "$WORK/$LLVM_ASSET")" != "$LLVM_SHA" ]; then
      curl -fsSL -o "$WORK/$LLVM_ASSET" \
        "https://github.com/llvm/llvm-project/releases/download/llvmorg-$LLVM_VERSION/$LLVM_ASSET"
    fi
    got=$(sha "$WORK/$LLVM_ASSET")
    if [ "$got" != "$LLVM_SHA" ]; then
      echo "LLVM checksum mismatch: want $LLVM_SHA got $got" >&2; exit 1
    fi
    # LLVM compresses with a 1 GiB window; zstd refuses that without --long.
    tar -C "$WORK" -I 'zstd -d --long=31' -xf "$WORK/$LLVM_ASSET"
    [ -x "$LLVM_DIR/bin/clang" ] || { echo "no clang at $LLVM_DIR/bin/clang after extracting" >&2; exit 1; }
    rm -f "$WORK/$LLVM_ASSET"
  fi
  CC="$LLVM_DIR/bin/clang"
  # A standalone clang uses the host's libstdc++, and picks the GCC
  # installation by the highest version number -- which can be one with no
  # libstdc++ headers at all (Amazon Linux 2023: the gcc14 C compiler next to
  # the system GCC 11's libstdc++), giving "'cassert' file not found". Pin the
  # GCC installation that the system g++ itself uses, via a wrapper so Bazel's
  # toolchain detection still sees (and probes) a clang.
  if command -v g++ >/dev/null 2>&1; then
    GCC_DIR=$(dirname "$(g++ -print-libgcc-file-name)")
    cat > "$WORK/bin/clang" <<EOF
#!/bin/sh
exec "$LLVM_DIR/bin/clang" --gcc-install-dir="$GCC_DIR" "\$@"
EOF
    chmod +x "$WORK/bin/clang"
    CC="$WORK/bin/clang"
    echo "libstdc++ from: $GCC_DIR"
  fi
fi
CC_BIN=$(command -v "$CC")
echo "compiler: $CC_BIN ($("$CC_BIN" --version | head -1))"

# 5. Build, using TCMalloc's own .bazelrc (its CI flags: C++17, absl, ...).
TARGETS="//hoard_bench:all"
cd tcmalloc
"$BAZELISK" --output_user_root="$WORK/bazel-root" build -c opt \
    --repo_env=CC="$CC_BIN" $TARGETS > "$WORK/bazel-build.log" 2>&1 \
  || { grep -E "error:|ERROR" "$WORK/bazel-build.log" | head -20; exit 1; }
echo "build: OK (log: $WORK/bazel-build.log)"

OUT="$WORK/out"
rm -rf "$OUT" && mkdir -p "$OUT"
for b in cache-scratch cache-thrash cache_scratch_ci; do
  cp bazel-bin/hoard_bench/$b "$OUT/$b-tcmalloc"
  cp bazel-bin/hoard_bench/$b-default "$OUT/$b-default"
done
cp bazel-bin/hoard_bench/probe "$OUT/probe"
echo "binaries in $OUT:"; ls "$OUT"
"$OUT/probe"
