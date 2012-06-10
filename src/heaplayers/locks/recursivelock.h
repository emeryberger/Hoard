/* -*- C++ -*- */

/*

  Heap Layers: An Extensible Memory Allocation Infrastructure
  
  Copyright (C) 2000-2012 by Emery Berger
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

#ifndef HL_RECURSIVELOCK_H
#define HL_RECURSIVELOCK_H

/**
 * @class RecursiveLockType
 * @brief Implements a recursive lock using some base lock representation.
 * @param BaseLock The base lock representation.
 */

namespace HL {

  template <class BaseLock>
  class RecursiveLockType : public BaseLock {
  public:

    inline RecursiveLockType (void);

    inline void lock (void);
    inline void unlock (void);

  private:
    int tid;	/// The lock owner's thread id. -1 if unlocked.
    int count;	/// The recursion depth of the lock.
  };


}

template <class BaseLock>
HL::RecursiveLockType<BaseLock>::RecursiveLockType (void)
  : tid (-1),
    count (0)
{}

template <class BaseLock>
void HL::RecursiveLockType<BaseLock>::lock (void) {
  int currthread = GetCurrentThreadId();
  if (tid == currthread) {
    count++;
  } else {
    BaseLock::lock();
    tid = currthread;
    count++;
  }
}

template <class BaseLock>
void HL::RecursiveLockType<BaseLock>::unlock (void) {
  int currthread = GetCurrentThreadId();
  if (tid == currthread) {
    count--;
    if (count == 0) {
      tid = -1;
      BaseLock::unlock();
    }
  } else {
    // We tried to unlock it but we didn't lock it!
    // This should never happen.
    assert (0);
    abort();
  }
}

#endif
