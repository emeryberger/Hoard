# Regression probes (Linux)

Run each against a preloaded Hoard (`LD_PRELOAD=build/libhoard.so ./probe`);
each exits nonzero on failure. CI runs them in `.github/workflows/build-test.yml`.

- `overflow.cpp`: every near-`SIZE_MAX` request through malloc, realloc,
  memalign, posix_memalign, aligned_alloc, valloc, pvalloc, calloc,
  reallocarray and (aligned) operator new must fail instead of returning an
  undersized block.
- `overaligned.c`: allocations aligned to at least the superblock size must
  report a correct usable size, keep their data across realloc, and be
  reclaimed by free.
- `threadchurn.c`, `teardown.cpp`: TLABs of exiting threads must be flushed.
  The probes use the `pthread_create` the program binds (`@GLIBC_2.34` on
  glibc 2.34 and later), which does not resolve to Hoard's interposer.
  `teardown.cpp` also allocates from TSD and `thread_local` destructors that
  run after the TLAB has been torn down.
