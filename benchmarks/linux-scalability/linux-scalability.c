/*
 *  malloc-test
 *  cel - Thu Jan  7 15:49:16 EST 1999
 *
 *  Benchmark libc's malloc, and check how well it
 *  can handle malloc requests from multiple threads.
 *
 *  Syntax:
 *  malloc-test [ size [ iterations [ thread count ]]]  
 *
 */

/*
 * June 9, 2013: Modified by Emery Berger to use barriers so all
 * threads start at the same time; added statistics gathering.
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <unistd.h>
#include <math.h>
#include <pthread.h>
#include <stdint.h>

#define USECSPERSEC 1000000
#define pthread_attr_default NULL
#define MAX_THREADS 50

double * executionTime;
void * run_test (void *);
void *dummy (unsigned);

static unsigned long size = 512;
static uint64_t iteration_count = 1000000;
static unsigned int thread_count = 1;

#include "ptbarrier.h"

pthread_barrier_t barrier;

int 
main (int argc, char *argv[])
{
  unsigned int i;
  pthread_t thread[MAX_THREADS];

  /*          * Parse our arguments          */
  switch (argc)
    {
    case 4:			/* size, iteration count, and thread count were specified */
      thread_count = atoi (argv[3]);
      if (thread_count > MAX_THREADS)
	thread_count = MAX_THREADS;
    case 3:			/* size and iteration count were specified; others default */
      iteration_count = atoll (argv[2]);
    case 2:			/* size was specified; others default */
      size = atoi (argv[1]);

    case 1:			/* use default values */
      break;
    default:
      printf ("Unrecognized arguments.\n");
      exit (1);
    }

  printf ("Object size: %lu, Iterations: %lu, Threads: %d\n",
	  size, iteration_count, thread_count);

  executionTime = (double *) malloc (sizeof(double) * thread_count);
  pthread_barrier_init (&barrier, NULL, thread_count);

  /*          * Invoke the tests          */
  printf ("Starting test...\n");
  for (i = 0; i < thread_count; i++) {
    int * tid = (int *) malloc(sizeof(int));
    *tid = i;
    pthread_attr_t attr;
    pthread_attr_init (&attr);
#ifdef PTHREAD_SCOPE_SYSTEM
    pthread_attr_setscope (&attr, PTHREAD_SCOPE_SYSTEM); /* bound behavior */
#endif
    if (pthread_create (&(thread[i]), &attr, &run_test, tid))
      printf ("failed.\n");
  }

  /*          * Wait for tests to finish          */

  for (i = 0; i < thread_count; i++)
    pthread_join (thread[i], NULL);

  /* EDB: moved to outer loop. */
  /* Statistics gathering and reporting. */
  double sum = 0.0;
  double stddev = 0.0;
  double average;
  for (i = 0; i < thread_count; i++) {
    sum += executionTime[i];
  }
  average = sum / thread_count;
  for (i = 0; i < thread_count; i++) {
    double diff = executionTime[i] - average;
    stddev += diff * diff;
  }
  stddev = sqrt (stddev / (thread_count - 1));
  if (thread_count > 1) {
    printf ("Average execution time = %f seconds, standard deviation = %f.\n", average, stddev);
  } else {
    printf ("Average execution time = %f seconds.\n", average);
  }
  exit (0);
}

void * 
run_test (void * arg)
{
  register unsigned int i;
  register unsigned long request_size = size;
  register uint64_t total_iterations = iteration_count;
  int tid = *((int *) arg);
  struct timeval start, end, null, elapsed, adjusted;

  pthread_barrier_wait (&barrier);

#if 0
  /* Time a null loop.  We'll subtract this from the final malloc loop
     results to get a more accurate value. */

  gettimeofday (&start, NULL);

  for (i = 0; i < total_iterations; i++)
    {
      register void *buf;
      buf = dummy (i);
      buf = dummy (i);
    }

  gettimeofday (&end, NULL);
  null.tv_sec = end.tv_sec - start.tv_sec;
  null.tv_usec = end.tv_usec - start.tv_usec;
  if (null.tv_usec < 0)
    {
      null.tv_sec--;
      null.tv_usec += USECSPERSEC;
    }
#else
  null.tv_sec = 0;
  null.tv_usec = 0;
#endif

  /* Run the real malloc test */ 
  gettimeofday (&start, NULL);

  {
    void ** buf = (void **) malloc(sizeof(void *) * total_iterations);
    
    for (i = 0; i < total_iterations; i++)
      {
	buf[i] = malloc (request_size);
      }
    
    for (i = 0; i < total_iterations; i++)
      {
	free (buf[i]);
      }
  }

  gettimeofday (&end, NULL);
  elapsed.tv_sec = end.tv_sec - start.tv_sec;
  elapsed.tv_usec = end.tv_usec - start.tv_usec;
  if (elapsed.tv_usec < 0)
    {
      elapsed.tv_sec--;
      elapsed.tv_usec += USECSPERSEC;
    }

  /*          * Adjust elapsed time by null loop time          */
  adjusted.tv_sec = elapsed.tv_sec - null.tv_sec;
  adjusted.tv_usec = elapsed.tv_usec - null.tv_usec;
  if (adjusted.tv_usec < 0)
    {
      adjusted.tv_sec--;
      adjusted.tv_usec += USECSPERSEC;
    }
  pthread_barrier_wait (&barrier);
  unsigned int pt = tid;
  executionTime[pt % thread_count] = adjusted.tv_sec + adjusted.tv_usec / 1000000.0;
  //  printf ("Thread %u adjusted timing: %d.%06d seconds for %d requests" " of %d bytes.\n", pt, adjusted.tv_sec, adjusted.tv_usec, total_iterations, request_size);

  return NULL;
}

void *
dummy (unsigned i)
{
  return NULL;
}
