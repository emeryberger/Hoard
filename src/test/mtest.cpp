/* malloc-test.c
 * by Wolfram Gloger 1995, 1996
 *
 * This program is provided `as is', there is no warranty.
 */

#if !defined(__STDC__)
#define __STDC__ 1
#endif

#include <stdlib.h>
#include <stdio.h>

#if !defined(_WIN32)
#include <unistd.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>
#endif

#if !defined(__APPLE__) && !defined(__FreeBSD__)
#include <malloc.h>
#endif

#ifndef MEMORY
#define MEMORY          4000000l
#endif
#ifndef BINS_MAX
#define BINS_MAX        32768
#endif
#define SBINS_MAX       1024
#define SIZE            10000
#define I_MAX           5000
#ifndef I_AVERAGE
#define I_AVERAGE       200
#endif
#define ACTIONS_MAX     50
#ifndef SBRK_AVG
#define SBRK_AVG        0
#endif
#ifndef MMAP_THRESH
#define MMAP_THRESH 0
#endif
#ifndef TEST
#define TEST 4 /* maximal testing */
#endif
#ifndef TEST_INC
#define TEST_INC 1027
#endif
#if defined(__i386__) || defined(__sparc__) || defined(mips) || defined(_WIN32)
#define PAGE_SIZE 4096
#elif defined(__alpha__)
#define PAGE_SIZE 8192
#elif defined(__SVR4)
#define PAGE_SIZE 8192
#else
#if !defined(PAGE_SIZE)
#define PAGE_SIZE 4096 // #error "Define PAGE_SIZE !"
#endif
#endif
#define RANDOM(s)       (lran2(0) % (s))

/* All probabilities are parts in 1024. */
#ifndef PROB_MEMALIGN
#define PROB_MEMALIGN 0 // 128
#endif
#ifndef PROB_REALLOC
#define PROB_REALLOC 128
#endif
#ifndef PROB_CALLOC
#define PROB_CALLOC 128
#endif

struct bin {
        unsigned char *ptr;
        unsigned long size;
} m[BINS_MAX], sm[SBINS_MAX];

unsigned long size = SIZE, bins=0, sbins=0;
unsigned long total_size=0, total_size_max=0;
unsigned char *base_ptr;
unsigned long base_save;

long
#if __STDC__
lran2(long seed)
#else
lran2(seed) long seed;
#endif
#define LRAN2_MAX       714025l /* constants for portable */
#define IA                      1366l   /* random number generator */
#define IC                      150889l /* (see Numerical Recipes p. 211) */
{
        static int first = 1;
        static long x, y, v[97];
        int j;

        if(seed || first) {
                first = 0;
                x = (IC - seed) % LRAN2_MAX;
                if(x < 0) x = -x;
                for(j=0; j<97; j++) {
                        x = (IA*x + IC) % LRAN2_MAX;
                        v[j] = x;
                }
                x = (IA*x + IC) % LRAN2_MAX;
                y = x;
        }
        j = y % 97;
        y = v[j];
        x = (IA*x + IC) % LRAN2_MAX;
        v[j] = x;
        return y;
}
#undef IA
#undef IC

void
#if __STDC__
mem_init(unsigned char *ptr, unsigned long size)
#else
mem_init(ptr, size) unsigned char *ptr; unsigned long size;
#endif
{
        unsigned long i, j;

        if(size == 0) return;
        if(size > sizeof(unsigned long)) {
                /* Try the complete initial word. */
                *(unsigned long *)ptr = (unsigned long)ptr ^ size;
                i = TEST_INC;
        } else
                i = 0;
        for(; i<size; i+=TEST_INC) {
                j = (unsigned long)ptr ^ i;
                ptr[i] = ((j ^ (j>>8)) & 0xFF);
        }
        j = (unsigned long)ptr ^ (size-1);
        ptr[size-1] = ((j ^ (j>>8)) & 0xFF);
}

int
#if __STDC__
mem_check(unsigned char *ptr, unsigned long size)
#else
mem_check(ptr, size) unsigned char *ptr; unsigned long size;
#endif
{
        unsigned long i, j;

        if(size == 0) return 0;
        if(size > sizeof(unsigned long)) {
                if(*(unsigned long *)ptr != ((unsigned long)ptr ^ size)) {
				  printf ("failed size check: expected %x, found %x!\n",
						  ((unsigned long) ptr ^ size), *(unsigned long *) ptr);
				  return 1;
				}
                i = TEST_INC;
        } else
                i = 0;
        for(; i<size; i+=TEST_INC) {
                j = (unsigned long)ptr ^ i;
                if(ptr[i] != ((j ^ (j>>8)) & 0xFF)) return 2;
        }
        j = (unsigned long)ptr ^ (size-1);
        if(ptr[size-1] != ((j ^ (j>>8)) & 0xFF)) {
		  printf ("failed last byte check: expected %x, found %x!\n",
				  ((unsigned long) ((j ^ (j>>8)) & 0xFF)), ptr[size-1]);
		  return 3;
		}
        return 0;
}

long
#if __STDC__
random_size(long max)
#else
random_size(max) long max;
#endif
{
        long r1, r2, r, max_pages;

        max_pages = max/PAGE_SIZE;
        if(max_pages > 0) {
                r1 = RANDOM(1024);
                r2 = (r1 & 7)*4;
                if(r1 < 512) {
                        /* small value near power of two */
                        r = (1L << (r1 >> 6)) + r2;
                } else if(r1 < 512+20) {
                        /* value near a multiple of the page size */
                        r = (RANDOM(max_pages)+1)*PAGE_SIZE + r2 - 16;
                        /*printf("r = %4lx\n", r);*/
                } else r = RANDOM(max) + 1;
        } else r = RANDOM(max) + 1;
        /*if(r <= 0) exit(-1);*/
        return r;
}

void
#if __STDC__
bin_alloc(struct bin *m)
#else
bin_alloc(m) struct bin *m;
#endif
{
        long r, key;
        unsigned long sz;

#if TEST > 0
        if(mem_check(m->ptr, m->size)) {
                printf("bin_alloc: memory corrupt at %p, size=%lu!\n", m->ptr, m->size);
                exit(1);
        }
#endif
        total_size -= m->size;
        r = RANDOM(1024);
        if(r < PROB_MEMALIGN) {
#if !defined(_WIN32) // && !defined(__APPLE__) && !defined(__FreeBSD__)
                if(m->size > 0) free(m->ptr);
                m->size = random_size(size);
                // m->ptr = (unsigned char *)memalign(4 << RANDOM(8), m->size);
				m->ptr = 0;
				size_t alignment = 4 << RANDOM(16);
				void * p = 0;
                int r = posix_memalign(&p, alignment, m->size);
				if (!r) {
				  perror("MEMALIGN WAT");
				}
				m->ptr = (unsigned char *) p;
        if(!r) {
		  printf("memalign: out of memory!\n");
		  printf("size = %lu\n", m->size);
		  printf("alignment = %lu\n", alignment);
                exit(1);
        }
#endif
        } else if(r < (PROB_MEMALIGN + PROB_REALLOC)) {
                if(m->size == 0) {
#ifndef __sparc__
                        m->ptr = NULL;
#else
                        /* SunOS4 does not realloc() a NULL pointer */
                        m->ptr = (unsigned char *)malloc(1);
#endif
                }
#if TEST > 2
                key = RANDOM(256);
                sz = m->size;
                for(r=0; r<sz; r++) m->ptr[r] = (r ^ key) & 0xFF;
#endif
                m->size = random_size(size);
				//                printf("realloc %p (%d) -> %d\n", m->ptr, sz, (int)m->size);
                m->ptr = (unsigned char *)realloc(m->ptr, m->size);
        if(!m->ptr) {
                printf("realloc: out of memory!\n");
                exit(1);
        }
#if TEST > 2
                if(m->size < sz) sz = m->size;
                for(r=0; r<sz; r++)
                        if(m->ptr[r] != ((r ^ key) & 0xFF)) {
						  printf("realloc bug !\n");
                                exit(1);
                        }
#endif
        } else if(r < (PROB_MEMALIGN + PROB_REALLOC + PROB_CALLOC)) {
                if(m->size > 0) free(m->ptr);
                m->size = random_size(size);
                m->ptr = (unsigned char *)calloc(m->size, 1);
        if(!m->ptr) {
                printf("calloc: out of memory!\n");
                exit(1);
        }
#if TEST > 2
                for(r=0; r<m->size; r++)
                        if(m->ptr[r] != '\0') {
						  printf("calloc bug !\n");
						  exit(1);
                        }
#endif
        } else { /* normal malloc call */
                if(m->size > 0) free(m->ptr);
                m->size = random_size(size);
                m->ptr = (unsigned char *)malloc(m->size);
        if(!m->ptr) {
                printf("malloc: out of memory!\n");
                exit(1);
        }
        }
        if(!m->ptr) {
                printf("out of memory!\n");
                exit(1);
        }
        total_size += m->size;
        if(total_size > total_size_max) total_size_max = total_size;
#if TEST > 0
        mem_init(m->ptr, m->size);
#endif
        if(m->ptr < base_ptr) {
#ifdef VERBOSE
                printf("hmmm, allocating below brk...\n");
#endif
                base_ptr = m->ptr;
        }
}

void
#if __STDC__
bin_free(struct bin *m)
#else
bin_free(m) struct bin *m;
#endif
{
        if(m->size == 0) return;
#if TEST > 0
        if(mem_check(m->ptr, m->size)) {
                printf("bin_free: memory corrupt!\n");
                exit(1);
        }
#endif
        total_size -= m->size;
        free(m->ptr);
        m->size = 0;
}

void
bin_test()
{
        unsigned int b;
		int v;
		//  printf ("bin_test.\n");

        for(b=0; b<bins; b++) {
                if(v = mem_check(m[b].ptr, m[b].size)) {
                        printf("bin_test: memory corrupt! m[%d].ptr = %x, m[%d].size = %d\n",
							   b, m[b].ptr, b, m[b].size);
						printf ("error = %d\n", v);
                        exit(1);
                }
        }
        for(b=0; b<sbins; b++) {
                if(mem_check(sm[b].ptr, sm[b].size)) {
                        printf("bin_test: memory corrupt! sm[%d].ptr = %x, sm[%d].size = %d\n",
							   b, sm[b].ptr, b, sm[b].size);
                        exit(1);
                }
        }
}

void
print_times()
{
#if !defined(_WIN32)
        struct rusage ru;
        long total_sec, total_usec;

        getrusage(RUSAGE_SELF, &ru);
        printf(" u=%ld.%06ldsec",
                   (long)ru.ru_utime.tv_sec, (long)ru.ru_utime.tv_usec);
        printf(" s=%ld.%06ldsec",
                   (long)ru.ru_stime.tv_sec, (long)ru.ru_stime.tv_usec);
        total_usec = (long)ru.ru_utime.tv_usec + (long)ru.ru_stime.tv_usec;
        total_sec = (long)ru.ru_utime.tv_sec + (long)ru.ru_stime.tv_sec;
        if(total_usec >= 1000000) {
                total_usec -= 1000000;
                total_sec++;
        }
        printf(" t=%ld.%06ldsec", total_sec, total_usec);
#endif
}

int
#if __STDC__
main(int argc, char *argv[])
#else
main(argc, argv) int argc; char *argv[];
#endif
{
        int i, j, next_i, count, max=I_MAX, actions;
        unsigned int b;
        long sbrk_max, sum;
        double sbrk_used_sum, total_size_sum;
        void* dummy = 0;

        if(argc > 1) max = atoi(argv[1]);
        if(argc > 2) size = atoi(argv[2]);
        lran2((long)max ^ size);
        bins = (MEMORY/size)*4;
        if(bins > BINS_MAX) bins = BINS_MAX;
#if 0 // FIX ME? Disable sbrk...
        base_ptr = (unsigned char *)sbrk(0);
        sum = (long)base_ptr % PAGE_SIZE;
        if(sum > 0) {
                if((char *)sbrk((long)PAGE_SIZE - sum) == (char *)-1) exit(1);
                base_ptr += (long)PAGE_SIZE - sum;
                /*printf("base_ptr = %lx\n", (long)base_ptr);*/
        }
        /* attempt to fill up the region below the initial brk */
        for(i=0; i<10000; i++) {
                dummy = malloc(1);
                if(dummy >= (void*)base_ptr) break;
        }
        free(dummy);
        base_save = ((unsigned long)base_ptr >> 24) << 24;
#endif

#if MMAP_THRESH > 0
        if(!mallopt(-3, MMAP_THRESH)) printf("mallopt failed!\n");
        if(!mallopt(-4, 200)) printf("mallopt failed!\n");
#endif
#ifdef VERBOSE
        printf("# mmap_thresh=%d\n", MMAP_THRESH);
        printf("# bins=%d max=%d size=%d\n", bins, max, size);
        printf("# base=%lx\n", base_save);
#endif
        for(b=0; b<bins; b++) {
                if(RANDOM(2) == 0) bin_alloc(&m[b]);
                else m[b].size = 0;
        }
        sbrk_max = 0;
        sbrk_used_sum = total_size_sum = 0.0;
        for(i=next_i=count=0; i<=max;) {
#if TEST > 1
                bin_test();
#endif
#ifdef MSTATS
                malloc_stats();
#endif
                actions = RANDOM(ACTIONS_MAX);
                for(j=0; j<actions; j++) {
                        b = RANDOM(bins);
                        bin_free(&m[b]);
#if TEST > 3
                        bin_test();
#endif
                }
                i += actions;
#ifdef AFTER_FREE
                AFTER_FREE;
#endif
#if SBRK_AVG > 0
                if(sbins<SBINS_MAX && RANDOM(SBRK_AVG)==0) {
                        /* throw in an explicit sbrk call */
                        sm[sbins].size = RANDOM(10000)+1;
                        sm[sbins].ptr = sbrk(sm[sbins].size);
                        if(sbins>0 && sm[sbins].ptr==(sm[sbins-1].ptr+sm[sbins-1].size)) {
                                sm[sbins-1].size += sm[sbins].size;
                                sbins--;
                        }
#ifdef VERBOSE
                        printf("sbrk #%d %p %ld\n", sbins, sm[sbins].ptr, sm[sbins].size);
#endif
#if TEST > 0
                        mem_init(sm[sbins].ptr, sm[sbins].size);
#endif
                        sbins++;
                }
#endif
                actions = RANDOM(ACTIONS_MAX);
                for(j=0; j<actions; j++) {
                        b = RANDOM(bins);
                        bin_alloc(&m[b]);
#if TEST > 3
                        bin_test();
#endif
                }
                i += actions;
                if(i >= next_i) { /* gather statistics */
                        count++;
#if !defined(_WIN32)
                        sum = (long)sbrk(0);
#else
						sum = 0;
#endif
                        if(sum > sbrk_max) sbrk_max = sum;
                        sbrk_used_sum += sum;
                        total_size_sum += (double)total_size;
#ifdef VERBOSE
                        printf("%8d %7lu\n", i, total_size);
#endif
                        next_i += I_AVERAGE;
                }
        }

        /* Correct sbrk values. */
        sbrk_max -= (long)base_ptr;
        sbrk_used_sum -= (double)count*(long)base_ptr;
#ifdef VERBOSE
        printf("# initial brk: %lx\n", (long)base_ptr);
        printf("# max. sbrk()'ed memory: %ld bytes\n", sbrk_max);
        printf("# avg. sbrk()'ed memory: %ld bytes\n",
                   (long)(sbrk_used_sum/count));
        printf("# current size allocated: %ld bytes\n", total_size);
        printf("# maximum size allocated: %ld bytes\n", total_size_max);
        printf("# average size allocated: %.1f bytes\n", total_size_sum/count);
        printf("# current heap waste: %.2f%%\n",
                   (1.0 - (double)total_size_max/sbrk_max)*100.0);
        printf("# average heap waste: %.2f%%\n",
                   (1.0 - (double)total_size_sum/sbrk_used_sum)*100.0);
        printf("# total sbrk calls performed: %d\n", sbins);
#else
        printf("size=%7ld waste=%7.3f%%", size,
                   /* (1.0 - (double)total_size_max/sbrk_max)*100.0, */
                   (1.0 - (double)total_size_sum/sbrk_used_sum)*100.0);
        print_times();
        printf("\n");
#endif
        return 0;
}

/*
 * Local variables:
 * tab-width:4
 * compile-command: "gcc -Wall malloc-test.c -o malloc-test"
 * End:
 */

