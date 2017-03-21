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

#ifndef _REGIONHEAP_H_
#define _REGIONHEAP_H_

/**
 * @file regionheap.h
 * @brief The support classes for reap.
 * @author Emery Berger <http://www.cs.umass.edu/~emery>
 */

#include <assert.h>

#include "heaplayers.h"
#include "chunkheap.h"
#include "nestedheap.h"
#include "slopheap.h"

// Reap-specific.
#include "addheader.h"
#include "clearoptimizeheap.h"

/**
 * @class LeaHeap2
 * @brief An implementation of the Lea heap, using only an sbrk heap.
 */

#include "dlheap.h"

using namespace HL;

template <class Sbrk>
class LeaHeap2 :
  public
  Threshold<4096,
	    DLSmallHeapType<DLBigHeapType<CoalesceableHeap<Sbrk> > > >
{};



#if 0 // USE stack for non-touching deletion stuff (for drag measurements)

// NB: The magic MaxGlobalElements here supports 1GB worth of 8K chunks.

template <class TYPE, int MaxGlobalElements = 131072>
class StaticStack
{
public:

  StaticStack (void)
    : head (NULL, NULL)
  {
  }

  /**
   * @brief Push an item onto the stack.
   * @return 0 iff the stack is full.
   */

  inline int push (TYPE v) {
    Entry * e = new Entry (v, head.next);
    head.next = e;
    return 1;
  }

  /**
   * @brief Pops an item off of the stack.
   * @return 0 iff the stack is empty.
   */

  inline int pop (TYPE& t) {
    Entry * e = head.next;
    if (e == NULL) {
      return 0;
    }
    head.next = head.next->next;
    TYPE r = e->datum;
    delete e;
    t = r;
    return 1;
  }

private:

  //	class Entry : public PerClassHeap<FreelistHeap<StaticHeap<MaxGlobalElements * (sizeof(TYPE) + sizeof(Entry *))> > > {
  class Entry : public PerClassHeap<FreelistHeap<ZoneHeap<SbrkHeap, 8192> > > {
  public:
    explicit Entry (TYPE d, Entry * n)
      : datum (d), next (n)
    {}
    TYPE datum;
    Entry * next;
  };

  Entry head;
};


template <class SuperHeap>
class RegionHeap : public SuperHeap {
public:

  RegionHeap (void) {}

  ~RegionHeap (void)
  {
    clear();
  }

  inline void * malloc (const size_t sz) {
    void * ptr = SuperHeap::malloc (sz);
    if (!stk.push (ptr)) {
      return NULL;
    } else {
      return ptr;
    }
  }
  
  inline void clear (void) {
    void * ptr;
    while (stk.pop(ptr)) {
      SuperHeap::free (ptr);
    }
  }

private:

  StaticStack<void *> stk;

};

#else

/**
 * @class RegionHeap
 * @brief A heap layer that provides region semantics.
 */

template <class SuperHeap>
class RegionHeap : public SuperHeap {
public:

  RegionHeap (void)
    : prev (NULL)
  {}

  ~RegionHeap (void)
  {
    clear();
  }

  inline void * malloc (const size_t sz) {
    char * ch = (char *) SuperHeap::malloc (sz + sizeof(Header));

    if (ch == NULL) {
      return NULL;
    }
    
    // Put the "header" at the end of the object.
    // This is just so we can overwrite the start of the object
    // with a boundary tag (metadata) that will let us free it
    // into a coalescing heap.
    
    // The datum member points to the actual start of the object,
    // while the prev member is our linked-list pointer.
    
    Header * ptr = (Header *) (ch + sz);
    ptr->datum = ch;
    ptr->prev = prev;
    prev = ptr;

    return (void *) ch;
  }

  /**
   * @brief Checks to see if an object was allocated from this heap.
   * @return 1 iff the obj was in one of our chunks.
   */
  int find (void * obj) const {
    // Search backwards through the header links.
    Header * curr = prev;
    while (curr) {
      if ((curr->datum <= obj) && (curr > obj)) {
	return 1;
      }
      curr = curr->prev;
    }
    return 0;
  }

  /// Delete everything.
  inline void clear (void) {
    while (prev) {
      Header * ptr = prev->prev;
      SuperHeap::free (prev->datum);
      prev = ptr;
    }
  }

  // Just a NOP.
  inline void free (void *) {}

private:


  /// The header for allocated objects.
  class Header {
  public:
    /// Points to the start of the allocated object.
    void * datum;
    
    /// The previous header.
    Header * prev;
  };

  Header * prev;
};

#endif

class NoHeap {};

/**
 * @class ReapTopHeap
 * @brief Grab 8K chunks, leaving room for the header to be added below.
 *
 */

template <class MainStore>
class ReapTopHeap :
  public ChunkHeap<8192 - sizeof(typename AddHeader<NoHeap>::Header),
		   SlopHeap<RegionHeap<MainStore>, 16> > {};

/**
 * @class ReapBaseType
 * @brief The base implementation for reap.
 *
 */

template <class MainStore>
class ReapBaseType :
  public ClearOptimizeHeap<AddHeader<ReapTopHeap<MainStore> >,
                           LeaHeap2<ReapTopHeap<MainStore> > >
{};

/**
 * @class Reap
 * @brief A hybrid region-heap.
 *
 * This class uses a per-class freelist heap to optimize reap
 * allocation, and adds nested (hierarchical) heaps and ANSI
 * compliance.
 */

template <class MainStore>
class Reap :
  public PerClassHeap<FreelistHeap<MainStore> >, 
  public ANSIWrapper<NestedHeap<ReapBaseType<MainStore> > >
{};

#endif
