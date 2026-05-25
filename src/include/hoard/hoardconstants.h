// -*- C++ -*-

/*

  The Hoard Multiprocessor Memory Allocator
  www.hoard.org

  Author: Emery Berger, http://www.emeryberger.com
  Copyright (c) 1998-2020 Emery Berger

  See the LICENSE file at the top-level directory of this
  distribution and at http://github.com/emeryberger/Hoard.

*/

#ifndef HOARD_HOARDCONSTANTS_H
#define HOARD_HOARDCONSTANTS_H

namespace Hoard {
  
  /// The maximum amount of memory that each TLAB may hold, in bytes.
  enum { MAX_MEMORY_PER_TLAB = 16 * 1024 * 1024UL }; // 16MB
  
  /// The maximum number of threads supported (sort of).
  enum { MaxThreads = 2048 };
  
  /// The maximum number of heaps supported.
  enum { NumHeaps = 128 };
  
  /// Size, in bytes, of the largest object we will cache on a
  /// thread-local allocation buffer.
  enum { LargestSmallObject = 1024UL };
    
}

#endif
