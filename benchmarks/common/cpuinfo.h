// -*- C++ -*-

/*

  Heap Layers: An Extensible Memory Allocation Infrastructure
  
  Copyright (C) 2000-2003 by Emery Berger
  http://www.cs.umass.edu/~emery
  emery@cs.umass.edu
  
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.
  
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.
  
  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

*/



#ifndef HL_CPUINFO_H
#define HL_CPUINFO_H

#if defined(_WIN32)
#include <windows.h>
#include <process.h>
#else
#include <unistd.h>
#endif


#if !defined(_WIN32)
#include <pthread.h>
#endif

#if defined(__SVR4) // Solaris
#include <sys/lwp.h>
extern "C" unsigned int lwp_self(void);
#include <thread.h>
extern "C" int _thr_self(void);
#endif

#if defined(__linux)
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#endif

#if defined(__APPLE__)
#include <sys/types.h>
#include <sys/sysctl.h>
#endif

#if defined(__sgi)
#include <sys/types.h>
#include <sys/sysmp.h>
#include <sys/sysinfo.h>
#endif

#if defined(hpux)
#include <sys/mpctl.h>
#endif

#if defined(_WIN32)
extern __declspec(thread) int localThreadId;
#endif

#if defined(__SVR4) && defined(MAP_ALIGN)
extern volatile int anyThreadStackCreated;
#endif

namespace HL {

/**
 * @class CPUInfo
 * @author Emery Berger <http://www.cs.umass.edu/~emery>
 *
 * @brief Architecture-independent wrapper to get number of CPUs. 
 */

class CPUInfo {
public:
  CPUInfo (void)
  {}

  inline static int getNumProcessors (void) {
    static int _numProcessors = computeNumProcessors();
    return _numProcessors;
  }

  static inline unsigned long getThreadId (void);
  inline static int computeNumProcessors (void);

};


int CPUInfo::computeNumProcessors (void)
{
  static int np = 0;
  if (!np) {
#if defined(__linux) || defined(__APPLE__)
    np = (int) sysconf(_SC_NPROCESSORS_ONLN);
#elif defined(_WIN32)
    SYSTEM_INFO infoReturn[1];
    GetSystemInfo (infoReturn);
    np = (int) (infoReturn->dwNumberOfProcessors);
#elif defined(__sgi)
    np = (int) sysmp(MP_NAPROCS);
#elif defined(hpux)
    np = mpctl(MPC_GETNUMSPUS, NULL, NULL); // or pthread_num_processors_np()?
#elif defined(_SC_NPROCESSORS_ONLN)
    np = (int) (sysconf(_SC_NPROCESSORS_ONLN));
#else
    np = 2;
    // Unsupported platform.
    // Pretend we have at least two processors. This approach avoids the risk of assuming
    // we're on a uniprocessor, which might lead clever allocators to avoid using atomic
    // operations for all locks.
#endif
    return np;
  } else {
    return np;
  }
}

  // Note: when stacksize arg is NULL for pthread_attr_setstacksize [Solaris],
// stack size is 1 MB for 32-bit arch, 2 MB for 64-bit arch.
// pthread_attr_getstacksize
// pthread_attr_setstackaddr
// pthread_attr_getstackaddr
// PTHREAD_STACK_SIZE is minimum.
// or should we just assume we have __declspec(thread) or __thread?

#if defined(USE_THREAD_KEYWORD)
  extern __thread int localThreadId;
#endif

  // FIX ME FIXME
  //#include <stdio.h>

unsigned long CPUInfo::getThreadId (void) {
#if defined(__SVR4)
  size_t THREAD_STACK_SIZE;
  if (sizeof(size_t) <= 4) {
    THREAD_STACK_SIZE = 1048576;
  } else {
    // 64-bits.
    THREAD_STACK_SIZE = 1048576 * 2;
  }
  if (0) { // !anyThreadStackCreated) {
    // We know a priori that all stack variables
    // are on different stacks. Since no one has created
    // a special one, we are in control, and thus all stacks
    // are 1 MB in size and on 1 MB boundaries.
    // (Actually: 1 MB for 32-bits, 2 MB for 64-bits.)
    char buf;
    return (((size_t) &buf) & ~(THREAD_STACK_SIZE-1)) >> 20;
  } else {
    return (int) pthread_self();
  }
#elif defined(_WIN32)
  // It looks like thread id's are always multiples of 4, so...
  return GetCurrentThreadId() >> 2;
#elif defined(__APPLE__)
  // Consecutive thread id's in Mac OS are 4096 apart;
  // dividing off the 4096 gives us an appropriate thread id.
  int tid = (int) ((unsigned long) pthread_self()) >> 12;
  return tid;
#elif defined(__BEOS__)
  return find_thread(0);
#elif defined(USE_THREAD_KEYWORD)
  return localThreadId;
#elif defined(__linux) || defined(PTHREAD_KEYS_MAX)
  // Consecutive thread id's in Linux are 1024 apart;
  // dividing off the 1024 gives us an appropriate thread id.
  return (unsigned long) pthread_self() >> 10;
#elif defined(POSIX)
  return (unsigned long) pthread_self();
#elif USE_SPROC
  // This hairiness has the same effect as calling getpid(),
  // but it's MUCH faster since it avoids making a system call
  // and just accesses the sproc-local data directly.
  unsigned long pid = (unsigned long) PRDA->sys_prda.prda_sys.t_pid;
  return pid;
#else
  return 0;
#endif
}

}

#endif
