/*
 * aligned_test.c - Windows _aligned_* family correctness test.
 *
 * Exercises the full CRT aligned family -- _aligned_malloc / _aligned_realloc /
 * _aligned_recalloc / _aligned_msize / _aligned_free and the _aligned_offset_*
 * variants -- across a range of alignments (including >= the 64KB Windows
 * SUPERBLOCK_SIZE) and offsets.
 *
 * It is meant to run under `withdll.exe /d:hoard.dll`, so what is actually
 * tested is Hoard's Detours interception of the aligned family: Hoard must back
 * both the allocation and its matching free, and it recovers an aligned/offset
 * pointer from a one-word-below stash. If base allocation and base free were
 * ever split across heaps (a cross-heap free), or a large-alignment pointer
 * could not be recovered, the heavy churn and full-range writes here would
 * corrupt the heap or crash.
 *
 * Exit code: 0 = all checks passed, otherwise the failure count (capped). A
 * one-line "ALIGNED TEST: PASS/FAIL (n failures)" summary is also written to
 * the file named by the ALIGNED_OUT environment variable, because stdout is not
 * reliably captured through DLL injection (same reason the benchmarks use
 * BENCH_OUT).
 *
 * Windows-only: the _aligned_* family and <malloc.h> declarations do not exist
 * on POSIX, so the CMake target is guarded by WIN32.
 */

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>

static int failures = 0;

/* Breadcrumb trace. stdout is captured by the CI watchdog even through DLL
   injection, and each line is flushed, so if the process crashes under Hoard
   the last line printed pinpoints the exact operation that faulted. Enabled by
   default; set ALIGNED_QUIET to silence the per-operation chatter. */
static int g_trace = 1;
static void trace(const char *fmt, ...) {
  if (!g_trace) return;
  va_list ap;
  va_start(ap, fmt);
  vfprintf(stdout, fmt, ap);
  va_end(ap);
  fputc('\n', stdout);
  fflush(stdout);
}

#define CHECK(c, ...)                                                   \
  do {                                                                  \
    if (!(c)) {                                                         \
      printf("  FAIL: " __VA_ARGS__);                                   \
      printf("\n");                                                     \
      if (failures < 1000000) failures++;                              \
    }                                                                   \
  } while (0)

/* Spot-check a byte pattern across a region without walking every byte. */
static int pattern_ok(const unsigned char *p, size_t n, unsigned char v) {
  for (size_t i = 0; i < n; i += 4093) {
    if (p[i] != v) return 0;
  }
  if (n && p[n - 1] != v) return 0;
  return 1;
}

/* One (alignment, size, offset) allocation round-trip. */
static void one(size_t align, size_t size, size_t offset) {
  trace("one: malloc sz=%zu al=%zu off=%zu", size, align, offset);
  void *p = _aligned_offset_malloc(size, align, offset);
  trace("  -> p=%p", p);
  if (p == NULL) {
    printf("  _aligned_offset_malloc(sz=%zu al=%zu off=%zu) returned NULL\n",
           size, align, offset);
    if (failures < 1000000) failures++;
    return;
  }
  CHECK(((uintptr_t)p + offset) % align == 0,
        "sz=%zu al=%zu off=%zu: (p+off)=%p not aligned", size, align, offset,
        (void *)((uintptr_t)p + offset));

  trace("  msize");
  size_t u = _aligned_msize(p, align, offset);
  trace("  -> msize=%zu", u);
  CHECK(u >= size, "sz=%zu al=%zu off=%zu: msize=%zu < size", size, align,
        offset, u);

  /* Writing the full requested range must stay within the backing block. */
  trace("  memset %zu", size);
  memset(p, 0xC3, size);
  CHECK(pattern_ok((unsigned char *)p, size, 0xC3), "readback sz=%zu al=%zu",
        size, align);

  /* Grow via the matching realloc, preserving contents. */
  size_t nsz = size * 2 + 4096;
  trace("  realloc -> %zu", nsz);
  void *q;
  if (offset == 0) {
    q = _aligned_realloc(p, nsz, align);
  } else {
    q = _aligned_offset_realloc(p, nsz, align, offset);
  }
  trace("  -> q=%p", q);
  CHECK(q != NULL, "realloc grow sz=%zu al=%zu off=%zu failed", size, align,
        offset);
  if (q != NULL) {
    CHECK(((uintptr_t)q + offset) % align == 0, "realloc broke alignment");
    CHECK(pattern_ok((unsigned char *)q, size, 0xC3),
          "realloc did not preserve contents (sz=%zu al=%zu off=%zu)", size,
          align, offset);
    memset(q, 0x7E, nsz);
    trace("  free q");
    _aligned_free(q);
  } else {
    trace("  free p");
    _aligned_free(p);
  }
  trace("  done");
}

int main(void) {
  setvbuf(stdout, NULL, _IONBF, 0);
  if (getenv("ALIGNED_QUIET")) g_trace = 0;
  trace("aligned_test start");

  /* Alignments span small, exactly the 64KB Windows superblock, and well
     above it; sizes span tiny to multi-megabyte. */
  const size_t aligns[] = { 8,       16,      64,      4096,
                            1 << 16, 1 << 17, 1 << 18, 1 << 20,
                            1 << 22 };
  const size_t sizes[] = { 1, 7, 100, 4096, 300000, 1 << 20, (3 << 20) + 7 };

  for (size_t a = 0; a < sizeof aligns / sizeof *aligns; a++) {
    for (size_t s = 0; s < sizeof sizes / sizeof *sizes; s++) {
      one(aligns[a], sizes[s], 0);
      if (sizes[s] > 1) {
        one(aligns[a], sizes[s], 1);
        one(aligns[a], sizes[s], sizes[s] / 2);
        one(aligns[a], sizes[s], sizes[s] - 1);
      }
    }
  }

  /* Churn: many large-aligned blocks, freed in shuffled (even/odd) order, so
     memory must recirculate without the free path misrouting. */
  trace("churn begin");
  enum { N = 120 };
  const size_t churnAlign = 1 << 20;
  const size_t churnSize = 200000;
  void *v[N];
  for (int i = 0; i < N; i++) {
    v[i] = _aligned_malloc(churnSize, churnAlign);
    if (v[i] == NULL) {
      printf("  churn alloc %d failed\n", i);
      if (failures < 1000000) failures++;
    } else {
      CHECK(((uintptr_t)v[i] % churnAlign) == 0, "churn %d misaligned", i);
      memset(v[i], i & 0xff, churnSize);
    }
  }
  for (int i = 0; i < N; i += 2) {
    if (v[i]) { _aligned_free(v[i]); v[i] = NULL; }
  }
  for (int i = 0; i < N; i++) {
    if (v[i]) {
      CHECK(((unsigned char *)v[i])[churnSize / 2] == (i & 0xff),
            "churn survivor %d corrupted", i);
      _aligned_free(v[i]);
    }
  }

  trace("recalloc begin");
  /* _aligned_recalloc: grow must zero the new tail and preserve the old head. */
  {
    void *p = _aligned_malloc(64, 4096);
    CHECK(p != NULL, "recalloc setup alloc failed");
    if (p != NULL) {
      memset(p, 0xFF, 64);
      size_t oldU = _aligned_msize(p, 4096, 0);
      void *q = _aligned_recalloc(p, 1, 8192, 4096);
      CHECK(q != NULL, "recalloc failed");
      if (q != NULL) {
        int keep = 1;
        for (int i = 0; i < 64; i++)
          if (((unsigned char *)q)[i] != 0xFF) keep = 0;
        CHECK(keep, "recalloc did not preserve old head");
        int zero = 1;
        for (size_t i = oldU; i < 8192; i++)
          if (((unsigned char *)q)[i] != 0) zero = 0;
        CHECK(zero, "recalloc did not zero grown region [%zu,8192)", oldU);
        _aligned_free(q);
      }
    }
  }

  trace("corners begin");
  /* NULL/zero contract corners. */
  {
    void *p = _aligned_malloc(0, 64);
    CHECK(p != NULL && ((uintptr_t)p % 64) == 0, "_aligned_malloc(0,64)");
    _aligned_free(p);
    _aligned_free(NULL); /* must be a no-op */
  }

  const char *verdict = failures ? "FAIL" : "PASS";
  printf("ALIGNED TEST: %s (%d failures)\n", verdict, failures);

  /* Mirror the summary to a file: stdout can be swallowed under injection. */
  const char *out = getenv("ALIGNED_OUT");
  if (out && *out) {
    FILE *f = fopen(out, "a");
    if (f) {
      fprintf(f, "ALIGNED TEST: %s (%d failures)\n", verdict, failures);
      fclose(f);
    }
  }

  return failures;
}
