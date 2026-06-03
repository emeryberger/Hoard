// -*- C++ -*-

/*

  The Hoard Multiprocessor Memory Allocator
  www.hoard.org

  Author: Emery Berger, http://www.emeryberger.com
  Copyright (c) 1998-2020 Emery Berger

  See the LICENSE file at the top-level directory of this
  distribution and at http://github.com/emeryberger/Hoard.

*/

#ifndef HOARD_HOARDSUPERBLOCKHEADER_H
#define HOARD_HOARDSUPERBLOCKHEADER_H

#include <stdio.h>


#if defined(_WIN32)
#pragma warning( push )
#pragma warning( disable: 4355 ) // this used in base member initializer list
#endif

#include "heaplayers.h"
#include "utility/cpp23compat.h"
#include "../util/purge.h"

#include <atomic>
#include <cstdlib>

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-variable"
#endif

namespace Hoard {

  template <class LockType,
	    int SuperblockSize,
	    typename HeapType,
	    template <class LockType_,
		      int SuperblockSize_,
		      typename HeapType_>
	    class Header_>
  class HoardSuperblock;

  template <class LockType,
	    int SuperblockSize,
	    typename HeapType>
  class HoardSuperblockHeader;
  
  template <class LockType,
	    int SuperblockSize,
	    typename HeapType>
  class HoardSuperblockHeaderHelper {
  public:

    enum { Alignment = sizeof(void *) * 2 };

  public:

    typedef HoardSuperblock<LockType, SuperblockSize, HeapType, HoardSuperblockHeader> BlockType;
    
    HoardSuperblockHeaderHelper (size_t sz, size_t bufferSize, char * start)
      : _magicNumber (MAGIC_NUMBER ^ (size_t) this),
	_objectSize (sz),
	_objectSizeIsPowerOfTwo (!(sz & (sz - 1)) && sz),
	_totalObjects ((unsigned int) (bufferSize / sz)),
	_magicMul (computeMagicMul(sz)),
	_magicShift (computeMagicShift(sz)),
	_owner (nullptr),
	_prev (nullptr),
	_next (nullptr),
	_reapableObjects (_totalObjects),
	_objectsFree (_totalObjects),
	_start (start),
	_position (start)
    {
      assert ((HL::align<Alignment>((size_t) start) == (size_t) start));
      assert (_objectSize >= Alignment);
      assert ((_totalObjects == 1) || (_objectSize % Alignment == 0));
    }

    virtual ~HoardSuperblockHeaderHelper() {
      clear();
    }

    INLINE void * malloc() {
      assert (isValid());
      // Fast path: bump-pointer allocation from reap region.
      void * ptr = reapAlloc();
      assert ((ptr == nullptr) || ((size_t) ptr % Alignment == 0));
      if (HL_EXPECT_FALSE(!ptr)) {
	// Slow path: allocate from freelist.
	ptr = freeListAlloc();
	assert ((ptr == nullptr) || ((size_t) ptr % Alignment == 0));
      }
      if (HL_EXPECT_TRUE(ptr != nullptr)) {
	assert (getSize(ptr) >= _objectSize);
	assert ((size_t) ptr % Alignment == 0);
      }
      return ptr;
    }

    INLINE void free (void * ptr) {
      assert ((size_t) ptr % Alignment == 0);
      assert (isValid());
      _freeList.insert (reinterpret_cast<FreeSLList::Entry *>(ptr));
      _objectsFree++;
      // Clearing is rare - only when superblock becomes completely empty.
      if (HL_EXPECT_FALSE(_objectsFree == _totalObjects)) {
	clear();
      }
    }

    void clear() {
      assert (isValid());
      // Clear out the freelist.
      _freeList.clear();
      // All the objects are now free.
      _objectsFree = _totalObjects;
      _reapableObjects = _totalObjects;
      _position = (char *) (HL::align<Alignment>((size_t) _start));
    }

    /// @brief Returns the actual start of the object.
    INLINE void * normalize (void * ptr) const {
      assert (isValid());
      auto offset = (size_t) ptr - (size_t) _start;
      void * p;

      if (_objectSizeIsPowerOfTwo) {
	p = (void *) ((size_t) ptr - (offset & (_objectSize - 1)));
      } else {
	// Use multiplicative inverse to replace expensive modulo.
	// A multiply+shift (~4 cycles) instead of division (~30+ cycles).
	auto remainder = fastModulo(offset);
	p = (void *) ((size_t) ptr - remainder);
      }
      return p;
    }


    size_t getSize (void * ptr) const {
      assert (isValid());
      auto offset = (size_t) ptr - (size_t) _start;
      size_t newSize;
      if (_objectSizeIsPowerOfTwo) {
	newSize = _objectSize - (offset & (_objectSize - 1));
      } else {
	newSize = _objectSize - fastModulo(offset);
      }
      return newSize;
    }

    /// Get object size without validation (for fast path).
    size_t getObjectSizeUnchecked() const {
      return _objectSize;
    }

    bool isValidSuperblock() const {
      return isValid();
    }

    size_t getObjectSize() const {
      return _objectSize;
    }

    unsigned int getTotalObjects() const {
      return _totalObjects;
    }

    unsigned int getObjectsFree() const {
      return _objectsFree;
    }

    HeapType * getOwner() const {
      return _owner;
    }

    void setOwner (HeapType * o) {
      _owner = o;
    }

    bool isValid() const {
      return (_magicNumber == (MAGIC_NUMBER ^ (size_t) this));
    }

    BlockType * getNext() const {
      return _next;
    }

    BlockType* getPrev() const {
      return _prev;
    }

    void setNext (BlockType* n) {
      _next = n;
    }

    void setPrev (BlockType* p) {
      _prev = p;
    }

    void lock() {
      _theLock.lock();
    }

    void unlock() {
      _theLock.unlock();
    }

    /// Purge (decommit) the data region so OS can reclaim physical RAM.
    /// Called when a superblock becomes completely empty.
    void purgeData() {
      purgePages(const_cast<char*>(_start), (size_t)_totalObjects * _objectSize);
    }

  private:

    /// Compute offset % _objectSize using precomputed multiplicative inverse.
    /// A multiply+shift (~4 cycles) instead of division (~30+ cycles).
    INLINE size_t fastModulo(size_t offset) const {
#if defined(_MSC_VER) && !defined(__clang__)
      // MSVC doesn't support __uint128_t; fall back to regular modulo.
      return offset % _objectSize;
#else
      size_t quotient = (size_t)(((__uint128_t)offset * _magicMul) >> _magicShift);
      return offset - quotient * _objectSize;
#endif
    }

    /// Compute the multiplicative inverse for a given divisor.
    static size_t computeMagicMul(size_t d) {
      if (d == 0 || (!(d & (d - 1)) && d)) return 0;  // power of two or zero
#if defined(_MSC_VER) && !defined(__clang__)
      return 0;  // Not used on MSVC (fastModulo falls back to %)
#else
      unsigned s = computeMagicShift(d);
      __uint128_t one = 1;
      __uint128_t power = one << s;
      return (size_t)((power + d - 1) / d);
#endif
    }

    /// Compute the shift amount for the multiplicative inverse.
    static unsigned computeMagicShift(size_t d) {
      if (d == 0 || (!(d & (d - 1)) && d)) return 0;  // power of two or zero
#if defined(_MSC_VER) && !defined(__clang__)
      return 0;  // Not used on MSVC
#else
      return 64 + (63 - __builtin_clzll(d));
#endif
    }

    MALLOC_FUNCTION INLINE void * reapAlloc() {
      assert (isValid());
      assert (_position);
      // Fast bump-pointer allocation from virgin memory.
      if (HL_EXPECT_TRUE(_reapableObjects > 0)) {
	auto * ptr = _position;
	_position = ptr + _objectSize;
	_reapableObjects--;
	_objectsFree--;
	assert ((size_t) ptr % Alignment == 0);
	return ptr;
      }
      return nullptr;
    }

    MALLOC_FUNCTION INLINE void * freeListAlloc() {
      assert (isValid());
      // Freelist mode.
      auto * ptr = reinterpret_cast<char *>(_freeList.get());
      if (ptr) {
	assert (_objectsFree >= 1);
	_objectsFree--;
      }
      return ptr;
    }

    enum { MAGIC_NUMBER = 0xcafed00d };

    /// A magic number used to verify validity of this header.
    const size_t _magicNumber;

    /// The object size.
    const size_t _objectSize;

    /// True iff size is a power of two.
    const bool _objectSizeIsPowerOfTwo;

    /// Total objects in the superblock.
    const unsigned int _totalObjects;

    /// Multiplicative inverse of _objectSize for fast modulo computation.
    const size_t _magicMul;

    /// Shift amount for the multiplicative inverse.
    const unsigned _magicShift;

    /// The lock.
    LockType _theLock;

    /// The owner of this superblock.
    HeapType * _owner;

    /// The preceding superblock in a linked list.
    BlockType* _prev;

    /// The succeeding superblock in a linked list.
    BlockType* _next;
    
    /// The number of objects available to be 'reap'ed.
    unsigned int _reapableObjects;

    /// The number of objects available for (re)use.
    unsigned int _objectsFree;

    /// The start of reap allocation.
    const char * _start;

    /// The cursor into the buffer following the header.
    char * _position;

    /// The list of freed objects.
    FreeSLList _freeList;
  };

  // A helper class that pads the header to the desired alignment.

  template <class LockType,
	    int SuperblockSize,
	    typename HeapType>
  class HoardSuperblockHeader :
    public HoardSuperblockHeaderHelper<LockType, SuperblockSize, HeapType> {
  public:

    
    HoardSuperblockHeader (size_t sz, size_t bufferSize)
      : HoardSuperblockHeaderHelper<LockType,SuperblockSize,HeapType> (sz, bufferSize, (char *) (this + 1))
    {
      static_assert(sizeof(HoardSuperblockHeader) % Parent::Alignment == 0,
		    "Superblock header size must be a multiple of the parent's alignment.");
    }

  private:

    //    typedef Header_<LockType, SuperblockSize, HeapType> Header;
    typedef HoardSuperblockHeaderHelper<LockType,SuperblockSize,HeapType> Parent;
    char _dummy[Parent::Alignment - (sizeof(Parent) % Parent::Alignment)];
  };

}

#if defined(__clang__)
#pragma clang diagnostic pop
#endif

#if defined(_WIN32)
#pragma warning( pop )
#endif

#endif
