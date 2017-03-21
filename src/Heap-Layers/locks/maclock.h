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

#ifndef HL_MACLOCK_H
#define HL_MACLOCK_H

#if defined(__APPLE__)

/**
 * @class MacLockType
 * @brief Locking using OS X spin locks.
 */

#if __MAC_OS_X_VERSION_MIN_REQUIRED >= 1012
#define USE_UNFAIR_LOCKS 1
#include <os/lock.h>
#else
#define USE_UNFAIR_LOCKS 0
#include <libkern/OSAtomic.h>
#endif


namespace HL {

  class MacLockType {
  public:

    MacLockType()
    {
#if USE_UNFAIR_LOCKS
      mutex = OS_UNFAIR_LOCK_INIT;
#else
      mutex = OS_SPINLOCK_INIT;
#endif      
    }

    ~MacLockType()
    {
#if USE_UNFAIR_LOCKS
      mutex = OS_UNFAIR_LOCK_INIT;
#else
      mutex = OS_SPINLOCK_INIT;
#endif      
    }

    inline void lock() {
#if USE_UNFAIR_LOCKS
      os_unfair_lock_lock(&mutex);
#else
      OSSpinLockLock (&mutex);
#endif
    }

    inline void unlock() {
#if USE_UNFAIR_LOCKS
      os_unfair_lock_unlock(&mutex);
#else
      OSSpinLockUnlock (&mutex);
#endif
    }

  private:

#if USE_UNFAIR_LOCKS
    os_unfair_lock mutex;
#else
    OSSpinLock mutex;
#endif

  };

}

#endif

#endif
