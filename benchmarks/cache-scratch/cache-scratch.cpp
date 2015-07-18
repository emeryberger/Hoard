///-*-C++-*-//////////////////////////////////////////////////////////////////
//
// Hoard: A Fast, Scalable, and Memory-Efficient Allocator
//        for Shared-Memory Multiprocessors
// Contact author: Emery Berger, http://www.cs.umass.edu/~emery
//
// This library is free software; you can redistribute it and/or modify
// it under the terms of the GNU Library General Public License as
// published by the Free Software Foundation, http://www.fsf.org.
//
// This library is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Library General Public License for more details.
//
//////////////////////////////////////////////////////////////////////////////

/**
 * @file cache-scratch.cpp
 *
 * cache-scratch is a benchmark that exercises a heap's cache locality.
 * An allocator that allows multiple threads to re-use the same small
 * object (possibly all in one cache-line) will scale poorly, while
 * an allocator like Hoard will exhibit near-linear scaling.
 *
 * Try the following (on a P-processor machine):
 *
 *  cache-scratch 1 1000 1 1000000
 *  cache-scratch P 1000 1 1000000
 *
 *  cache-scratch-hoard 1 1000 1 1000000
 *  cache-scratch-hoard P 1000 1 1000000
 *
 *  The ideal is a P-fold speedup.
*/

#include <stdio.h>
#include <stdlib.h>

#include "fred.h"
#include "cpuinfo.h"
#include "timer.h"

// This class just holds arguments to each thread.
class workerArg {
public:

  workerArg() {}

  workerArg (char * obj, int objSize, int repetitions, int iterations)
    : _object (obj),
      _objSize (objSize),
      _iterations (iterations),
      _repetitions (repetitions)
  {}

  char * _object;
  int _objSize;
  int _iterations;
  int _repetitions;
};


#if defined(_WIN32)
extern "C" void worker (void * arg)
#else
extern "C" void * worker (void * arg)
#endif
{
  // free the object we were given.
  // Then, repeatedly do the following:
  //   malloc a given-sized object,
  //   repeatedly write on it,
  //   then free it.
  workerArg * w = (workerArg *) arg;
  delete w->_object;
  workerArg w1 = *w;
  for (int i = 0; i < w1._iterations; i++) {
    // Allocate the object.
    char * obj = new char[w1._objSize];
    // Write into it a bunch of times.
    for (int j = 0; j < w1._repetitions; j++) {
      for (int k = 0; k < w1._objSize; k++) {
	obj[k] = (char) k;
	volatile char ch = obj[k];
	ch++;
      }
    }
    // Free the object.
    delete [] obj;
  }

#if !defined(_WIN32)
  return NULL;
#endif
}


int main (int argc, char * argv[])
{
  int nthreads;
  int iterations;
  int objSize;
  int repetitions;

  if (argc > 4) {
    nthreads = atoi(argv[1]);
    iterations = atoi(argv[2]);
    objSize = atoi(argv[3]);
    repetitions = atoi(argv[4]);
  } else {
    fprintf (stderr, "Usage: %s nthreads iterations objSize repetitions\n", argv[0]);
    return 1;
  }

  HL::Fred * threads = new HL::Fred[nthreads];
  HL::Fred::setConcurrency (HL::CPUInfo::getNumProcessors());

  workerArg * w = new workerArg[nthreads];

  int i;

  // Allocate nthreads objects and distribute them among the threads.
  char ** objs = new char * [nthreads];
  for (i = 0; i < nthreads; i++) {
    objs[i] = new char[objSize];
  }

  HL::Timer t;
  t.start();

  for (i = 0; i < nthreads; i++) {
    w[i] = workerArg (objs[i], objSize, repetitions / nthreads, iterations);
    threads[i].create (&worker, (void *) &w[i]);
  }
  for (i = 0; i < nthreads; i++) {
    threads[i].join();
  }
  t.stop();

  delete [] threads;
  delete [] objs;
  delete [] w;

  printf ("Time elapsed = %f seconds.\n", (double) t);
  return 0;
}
