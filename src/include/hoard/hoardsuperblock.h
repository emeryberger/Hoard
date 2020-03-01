// -*- C++ -*-

/*

  The Hoard Multiprocessor Memory Allocator
  www.hoard.org

  Author: Emery Berger, http://www.emeryberger.com
  Copyright (c) 1998-2020 Emery Berger

  See the LICENSE file at the top-level directory of this
  distribution and at http://github.com/emeryberger/Hoard.

*/

#ifndef HOARD_HOARDSUPERBLOCK_H
#define HOARD_HOARDSUPERBLOCK_H

#include <cassert>
#include <cstdlib>

#include "heaplayers.h"

namespace Hoard {

  template <class LockType,
	    int SuperblockSize,
	    typename HeapType,
	    template <class LockType_,
		      int SuperblockSize_,
		      typename HeapType_> class Header_>
  class HoardSuperblock {
  public:

    HoardSuperblock (size_t sz)
      : _header (sz, BufferSize)
    {
      assert (_header.isValid());
      assert (this == (HoardSuperblock *)
	      (((size_t) this) & ~((size_t) SuperblockSize-1)));
    }
    
    /// @brief Find the start of the superblock by bitmasking.
    /// @note  All superblocks <em>must</em> be naturally aligned, and powers of two.
    static inline constexpr HoardSuperblock * getSuperblock (void * ptr) {
      return (HoardSuperblock *)
	(((size_t) ptr) & ~((size_t) SuperblockSize-1));
    }

    constexpr INLINE size_t getSize (void * ptr) const {
      if (_header.isValid() && inRange (ptr)) {
	return _header.getSize (ptr);
      } else {
	return 0;
      }
    }


    constexpr INLINE size_t getObjectSize() const {
      if (_header.isValid()) {
	return _header.getObjectSize();
      } else {
	return 0;
      }
    }

    MALLOC_FUNCTION INLINE void * malloc (size_t) {
      assert (_header.isValid());
      auto * ptr = _header.malloc();
      if (ptr) {
	assert (inRange (ptr));
	assert ((size_t) ptr % HeapType::Alignment == 0);
      }
      return ptr;
    }

    INLINE void free (void * ptr) {
      if (_header.isValid() && inRange (ptr)) {
	// Pointer is in range.
	_header.free (ptr);
      } else {
	// Invalid free.
      }
    }
    
    void clear() {
      if (_header.isValid()) {
	_header.clear();
      }
    }
    
    // ----- below here are non-conventional heap methods ----- //
    
    constexpr INLINE bool isValidSuperblock() const {
      auto b = _header.isValid();
      return b;
    }
    
    constexpr INLINE unsigned int getTotalObjects() const {
      assert (_header.isValid());
      return _header.getTotalObjects();
    }
    
    /// Return the number of free objects in this superblock.
    constexpr INLINE unsigned int getObjectsFree() const {
      assert (_header.isValid());
      assert (_header.getObjectsFree() >= 0);
      assert (_header.getObjectsFree() <= _header.getTotalObjects());
      return _header.getObjectsFree();
    }
    
    inline void lock() {
      assert (_header.isValid());
      _header.lock();
    }
    
    inline void unlock() {
      assert (_header.isValid());
      _header.unlock();
    }
    
    constexpr inline HeapType * getOwner() const {
      assert (_header.isValid());
      return _header.getOwner();
    }

    inline void setOwner (HeapType * o) {
      assert (_header.isValid());
      assert (o != nullptr);
      _header.setOwner (o);
    }
    
    constexpr inline HoardSuperblock * getNext() const {
      assert (_header.isValid());
      return _header.getNext();
    }

    constexpr inline HoardSuperblock * getPrev() const {
      assert (_header.isValid());
      return _header.getPrev();
    }
    
    inline void setNext (HoardSuperblock * f) {
      assert (_header.isValid());
      assert (f != this);
      _header.setNext (f);
    }
    
    inline void setPrev (HoardSuperblock * f) {
      assert (_header.isValid());
      assert (f != this);
      _header.setPrev (f);
    }
    
    constexpr INLINE bool inRange (void * ptr) const {
      // Returns true iff the pointer is valid.
      auto ptrValue = (size_t) ptr;
      return ((ptrValue >= (size_t) _buf) &&
	      (ptrValue < (size_t) &_buf[BufferSize]));
    }
    
    constexpr INLINE void * normalize (void * ptr) const {
      auto * ptr2 = _header.normalize (ptr);
      assert (inRange (ptr));
      assert (inRange (ptr2));
      return ptr2;
    }

    typedef Header_<LockType, SuperblockSize, HeapType> Header;

  private:
    
    
    // Disable copying and assignment.
    
    HoardSuperblock (const HoardSuperblock&);
    HoardSuperblock& operator=(const HoardSuperblock&);
    
    enum { BufferSize = SuperblockSize - sizeof(Header) };
    
    /// The metadata.
    Header _header;

    
    /// The actual buffer. MUST immediately follow the header!
    char _buf[BufferSize];
  };

}


#endif
