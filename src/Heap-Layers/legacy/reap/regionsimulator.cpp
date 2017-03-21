/*

  C-functions that allow (opaque) use of the region simulator.

  		Emery Berger
		emery@cs.utexas.edu
		http://www.cs.utexas.edu/users/emery

 */


#include <stdlib.h>

#include "mallocheap.h"
#include "regionsimulator.h"
#include "sanitycheckheap.h"

extern "C" void dlmalloc_stats(void);
extern "C" void * dlmalloc(size_t);
extern "C" void dlfree (void *);
extern "C" size_t dlmalloc_usable_size(void *);


class WinMallocHeap {
public:
  static inline void * malloc (size_t sz) {
		return malloc (sz);
	}

  static inline void free (void * p) {
		free (p);
	}

  static inline size_t getSize (const void * p) {
		return _msize ((void *) p);
	}
};


//class PseudoRegion : public RegionSimulator<LeaMallocHeap> {};

class PseudoRegion : public RegionSimulator<mallocHeap> {};


// class PseudoRegion : public RegionSimulator<SanityCheckHeap<mallocHeap> > {};

extern "C" void regionCreate (void ** reg, void ** parent)
{
  PseudoRegion * psr = new PseudoRegion();
//  printf ("size = %d\n", sizeof(PseudoRegion));
  if (parent != NULL) {
    PseudoRegion * par = *((PseudoRegion **) parent);
    par->addChild (psr);
  }
//  printf ("create region %x\n", *reg);
//  printf ("psr = %x\n", psr);
  *((PseudoRegion **) reg) = psr;
}

extern "C" void regionDestroy (void ** reg) 
{
//  printf ("delete region %x\n", *reg);
  ((PseudoRegion *) *reg)->freeAll ();
  delete ((PseudoRegion *) *reg);
  *reg = NULL;
}

extern "C" void * regionAllocate (void ** reg, size_t sz)
{
  return ((PseudoRegion *) *reg)->malloc (sz);
}

extern "C" void regionFreeAll (void ** reg)
{
  ((PseudoRegion *) *reg)->freeAll ();
}

extern "C" size_t regionSize (void * ptr)
{
  abort();
  //  return PseudoRegion::getSize (ptr);
}

/* And obstack functions */

#include "obstackreap.h"
#include "regionheap.h"

// class ObstackType : public PseudoObstack<mallocHeap> {};
class ObstackType : public ObstackReap<Reap<mallocHeap> > {};

extern "C" void obstack_init (struct obstack *obstack)
{
	obstack->theObstack = (void *) new ObstackType();
}


extern "C" void * obstack_alloc (struct obstack *obstack, size_t size)
{
	ObstackType * ob = (ObstackType *) obstack->theObstack;
	void * ptr = ob->malloc (size);
	return ptr;
}


extern "C" void * obstack_copy (struct obstack *obstack, void *address, size_t size)
{
	ObstackType * ob = (ObstackType *) obstack->theObstack;
	void * ptr = ob->grow (size);
	if (ptr != NULL) {
		memcpy (ptr, address, size);
	}
	ob->finalize();
	return ptr;
}


extern "C" void * obstack_copy0 (struct obstack *obstack, void *address, size_t size)
{
	ObstackType * ob = (ObstackType *) obstack->theObstack;
	void * ptr = ob->grow (size + 1);
	if (ptr != NULL) {
		memcpy (ptr, address, size);
		*((char *) ptr + size) = '\0';
	}
	ob->finalize();
	return ptr;
}


extern "C" void obstack_free (struct obstack *obstack, void *block)
{
	ObstackType * ob = (ObstackType *) obstack->theObstack;
	ob->freeAfter (block);
}


extern "C" void obstack_blank (struct obstack *obstack, size_t size)
{
	ObstackType * ob = (ObstackType *) obstack->theObstack;
	ob->grow (size);
}


extern "C" void obstack_grow (struct obstack *obstack, void *data, size_t size)
{
	ObstackType * ob = (ObstackType *) obstack->theObstack;
	void * ptr = ob->grow (size);
	if (ptr != NULL) {
		memcpy (ptr, data, size);
	}
}


extern "C" void obstack_grow0 (struct obstack *obstack, void *data, int size)
{
	ObstackType * ob = (ObstackType *) obstack->theObstack;
	void * ptr = ob->grow (size + 1);
	if (ptr != NULL) {
		memcpy (ptr, data, size);
		*((char *) ptr + size) = '\0';
	}
}


extern "C" void obstack_1grow (struct obstack *obstack, long c)
{
	ObstackType * ob = (ObstackType *) obstack->theObstack;
	long * ptr = (long *) ob->grow (sizeof(long));
	*ptr = c;
}


extern "C" void obstack_ptr_grow (struct obstack *obstack, void *data)
{
	ObstackType * ob = (ObstackType *) obstack->theObstack;
	void ** ptr = (void **) ob->grow (sizeof(void *));
	*ptr = data;
}


extern "C" void obstack_int_grow (struct obstack *obstack, int data)
{
	ObstackType * ob = (ObstackType *) obstack->theObstack;
	int * ptr = (int *) ob->grow (sizeof(int));
	*ptr = data;
}


extern "C" void * obstack_finish (struct obstack * obstack)
{
	ObstackType * ob = (ObstackType *) obstack->theObstack;
	void * ptr = ob->getObjectBase();
	ob->finalize();
	return ptr;
}


extern "C" void * obstack_base (struct obstack *obstack)
{
	ObstackType * ob = (ObstackType *) obstack->theObstack;
	return ob->getObjectBase();
}


extern "C" void * obstack_next_free (struct obstack *obstack)
{
	ObstackType * ob = (ObstackType *) obstack->theObstack;
	return ob->grow(0);
}


extern "C" int _obstack_get_alignment (struct obstack *)
{
  // Double alignment.
  return sizeof(double) - 1;
}
