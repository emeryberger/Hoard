// -*- C++ -*-

/*

  The Hoard Multiprocessor Memory Allocator
  www.hoard.org

  Author: Emery Berger, http://www.emeryberger.com
  Copyright (c) 1998-2020 Emery Berger

  See the LICENSE file at the top-level directory of this
  distribution and at http://github.com/emeryberger/Hoard.

*/

/**
 * @file alignedmmap.h
 * @author Emery Berger <http://www.emeryberger.com>
 */


#ifndef HOARD_ALIGNEDMMAP_H
#define HOARD_ALIGNEDMMAP_H

#include <unordered_map>

#include "heaplayers.h"
#include "mmapalloc.h"

#define TRACK_SIZE 0

using namespace std;
using namespace HL;

namespace Hoard {

  /**
   * @class AlignedMmapInstance
   * @brief Memory allocated from here is aligned with respect to Alignment.
   * @author Emery Berger <http://www.cs.umass.edu/~emery>
   */

  template <size_t Alignment,
	    class LockType>
  class AlignedMmap;

  template <size_t Alignment_>
  class AlignedMmapInstance {
  public:

    AlignedMmapInstance()
#if TRACK_SIZE
      : MyMap (16381) // entries in the hash map.
#endif
    {}

    enum { Alignment = Alignment_ };

    void clear() {
      // NOP: this heap never holds any memory.
    }

    inline void * malloc (size_t sz) {

      // Round up sz to the nearest page.
      sz = HL::align<HL::MmapWrapper::Size>(sz);

      // If the memory is already suitably aligned, just track size requests.
      if ((size_t) HL::MmapWrapper::Alignment % (size_t) Alignment == 0) {
	void * ptr = HL::MmapWrapper::map (sz);
#if TRACK_SIZE
	MyMap.set (ptr, sz);
#endif
	assert ((size_t) ptr % Alignment == 0);
	return ptr;
      }

      void * ptr = nullptr;

      // Try a map call and hope that it's suitably aligned. If we get lucky,
      // we're done.

      ptr = HL::MmapWrapper::map (sz);

      if ((size_t) ptr == HL::align<Alignment>((size_t) ptr)) {
	// We're done.
#if TRACK_SIZE
	MyMap.set (ptr, sz);
#endif
	return ptr;
      }

      // Try again.
      HL::MmapWrapper::unmap ((void *) ptr, sz);

      return slowMap (sz);
    }

    inline void free (void * ptr, size_t sz) {
      HL::MmapWrapper::unmap (ptr, sz);
    }
    
#if 0
    inline void free (void * ptr) {

      // Find the object. If we don't find it, we didn't allocate it.
      // For now, just ignore such an invalid free...

      size_t requestedSize = getSize (ptr);

      if (requestedSize == 0) {
	return;
      }

      HL::MmapWrapper::unmap (ptr, requestedSize);

      // Finally, undo the mapping.
      MyMap.erase (ptr);
    }
  
    inline size_t getSize (void * ptr) {
      return MyMap.get (ptr);
    }
#endif

  private:

    void * slowMap (size_t sz) {

      // We have to align it ourselves. We get memory from
      // mmap, align a pointer in the space, and free the space before
      // and after the aligned segment.

      void * ptr = reinterpret_cast<char *>(HL::MmapWrapper::map (sz + Alignment));

      if (ptr == nullptr) {
	return nullptr;
      }

      char * newptr = (char *) HL::align<Alignment>((size_t) ptr);

      // Unmap the part before (prolog) and after.

      size_t prolog = (size_t) newptr - (size_t) ptr;

      if (prolog > 0) {
	// Get rid of the prolog.
	HL::MmapWrapper::unmap (ptr, prolog);
      }

      // Get rid of the epilog.
      size_t epilog = Alignment - prolog;
      HL::MmapWrapper::unmap ((char *) newptr + sz, epilog);

      // Now record the size associated with this pointer.

#if TRACK_SIZE
      MyMap.set (newptr, sz);
#endif
      return newptr;
    }

    // Manage information in a map that uses a custom heap for
    // allocation.

    /// The key is an mmapped pointer.
    typedef void * keyType;

    /// The value is the requested size.
    typedef size_t valType;

    // The heap from which memory comes for the Map's purposes.
    // Objects come from chunks via mmap, and we manage these with a free list.
    class SourceHeap : public HL::FreelistHeap<BumpAlloc<65536, MmapAlloc> > { };

    /// The map type, with all the pieces in place.
    typedef std::unordered_map<keyType,
      valType,
      std::hash<keyType>,
      std::equal_to<keyType>,
      HL::STLAllocator<std::pair<keyType const, valType>,
		       SourceHeap>> mapType;

    /// The map that maintains the size of each mmapped chunk.
#if TRACK_SIZE
    mapType MyMap;
#endif

  };


  /**
   * @class AlignedMmap
   * @brief Route requests to the one aligned mmap instance.
   * @author Emery Berger <http://www.cs.umass.edu/~emery>
   */

  template <size_t Alignment_,
	    class LockType>
  class AlignedMmap :
    public ExactlyOneHeap<LockedHeap<LockType, AlignedMmapInstance<Alignment_> > > {};

}

#endif
