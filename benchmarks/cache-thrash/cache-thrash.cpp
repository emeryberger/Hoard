///-*-C++-*-//////////////////////////////////////////////////////////////////
//
// Hoard: A Fast, Scalable, and Memory-Efficient Allocator
//        for Shared-Memory Multiprocessors
// Contact author: Emery Berger, http://www.cs.umass.edu/~emery
//
// Copyright (c) 1998-2003, The University of Texas at Austin.
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
 * @file  cache-thrash.cpp
 * @brief cache-thrash is a benchmark that exercises a heap's cache-locality.
 *
 * Try the following (on a P-processor machine):
 *
 *  cache-thrash 1 1000 1 1000000
 *  cache-thrash P 1000 1 1000000
 *
 *  cache-thrash-hoard 1 1000 1 1000000
 *  cache-thrash-hoard P 1000 1 1000000
 *
 *  The ideal is a P-fold speedup.
*/


#include <iostream>
#include <stdlib.h>

using namespace std;

#include "cpuinfo.h"
#include "fred.h"
#include "timer.h"

// This class just holds arguments to each thread.
class workerArg {
public:
  workerArg() {}
  workerArg (int objSize, int repetitions, int iterations)
    : _objSize (objSize),
      _iterations (iterations),
      _repetitions (repetitions)
  {}

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
  // Repeatedly do the following:
  //   malloc a given-sized object,
  //   repeatedly write on it,
  //   then free it.
  workerArg * w = (workerArg *) arg;
  workerArg w1 = *w;
  for (int i = 0; i < w1._iterations; i++) {
    // Allocate the object.
    char * obj = new char[w1._objSize];
    //    printf ("obj = %p\n", obj);
    // Write into it a bunch of times.
    for (int j = 0; j < w1._repetitions; j++) {
      for (int k = 0; k < w1._objSize; k++) {
#if 0
	volatile double d = 1.0;
	d = d * d + d * d;
#else
	obj[k] = (char) k;
	volatile char ch = obj[k];
	ch++;
#endif
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
    cerr << "Usage: " << argv[0] << " nthreads iterations objSize repetitions" << endl;
    exit(1);
  }

  HL::Fred * threads = new HL::Fred[nthreads];
  HL::Fred::setConcurrency (HL::CPUInfo::getNumProcessors());
    
  int i;
  
  HL::Timer t;
  t.start();
  
  workerArg * w = new workerArg[nthreads];
    
  for (i = 0; i < nthreads; i++) {
    w[i] = workerArg (objSize, repetitions / nthreads, iterations);
    threads[i].create (&worker, (void *) &w[i]);
  }
  for (i = 0; i < nthreads; i++) {
    threads[i].join();
  }
  t.stop();

  delete [] threads;
  delete [] w;

  cout << "Time elapsed = " << (double) t << " seconds." << endl;
}
