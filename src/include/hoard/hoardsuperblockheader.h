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
	_owner (nullptr),
	_ownerTid (0),
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

      // Optimization note: the modulo operation (%) is *really* slow on
      // some architectures (notably x86-64). To reduce its overhead, we
      // optimize for the case when the size request is a power of two,
      // which is often enough to make a difference.

      if (_objectSizeIsPowerOfTwo) {
	p = (void *) ((size_t) ptr - (offset & (_objectSize - 1)));
      } else {
	p = (void *) ((size_t) ptr - (offset % _objectSize));
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
	newSize = _objectSize - (offset % _objectSize);
      }
      return newSize;
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

    size_t getOwnerTid() const {
      return _ownerTid;
    }

    void setOwnerTid (size_t tid) {
      _ownerTid = tid;
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

    /// The lock.
    LockType _theLock;

    /// The owner of this superblock.
    HeapType * _owner;

    /// The thread ID of the owning thread (for fast same-thread detection).
    size_t _ownerTid;

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

    /// Lock-free cross-thread free list entry (intrusive, uses object memory).
    struct CrossThreadEntry {
      CrossThreadEntry* next;
    };

    /// Lock-free cross-thread free list head (MPSC queue).
    std::atomic<CrossThreadEntry*> _crossThreadFrees{nullptr};

  public:
    /// Push a pointer to the cross-thread free list (lock-free, multiple producers).
    void crossThreadFree(void* ptr) {
      auto* entry = reinterpret_cast<CrossThreadEntry*>(ptr);
      CrossThreadEntry* oldHead = _crossThreadFrees.load(std::memory_order_relaxed);
      do {
        entry->next = oldHead;
      } while (!_crossThreadFrees.compare_exchange_weak(oldHead, entry,
                                                        std::memory_order_release,
                                                        std::memory_order_relaxed));
    }

    /// Pop all entries from cross-thread free list (single consumer).
    /// Returns head of linked list, or nullptr if empty.
    CrossThreadEntry* drainCrossThreadFrees() {
      return _crossThreadFrees.exchange(nullptr, std::memory_order_acquire);
    }

    /// Check if cross-thread free list has pending entries.
    bool hasCrossThreadFrees() const {
      return _crossThreadFrees.load(std::memory_order_relaxed) != nullptr;
    }
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
