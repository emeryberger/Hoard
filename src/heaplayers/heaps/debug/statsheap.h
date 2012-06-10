#ifndef HL_STATS_H_
#define HL_STATS_H_

#include <map>

namespace HL {

template <class SuperHeap>
class InUseHeap : public SuperHeap {
private:
  typedef std::map<void *, int> mapType;
public:
  InUseHeap (void)
    : inUse (0),
      maxInUse (0)
  {}
  void * malloc (size_t sz) {
    void * ptr = SuperHeap::malloc (sz);
    if (ptr != NULL) {
      inUse += sz;
      if (maxInUse < inUse) {
	maxInUse = inUse;
      }
      allocatedObjects.insert (std::pair<void *, int>(ptr, sz));
    }
    return ptr;
  }
  void free (void * ptr) {
    mapType::iterator i;
    i = allocatedObjects.find (ptr);
    if (i == allocatedObjects.end()) {
      // oops -- called free on object i didn't allocate.
      assert (0);
    }
    inUse -= (*i).second;
    allocatedObjects.erase (i);
    SuperHeap::free (ptr);
  }

  int getInUse (void) const {
    return inUse;
  }
  int getMaxInUse (void) const {
    return maxInUse;
  }
private:
  int inUse;
  int maxInUse;
  mapType allocatedObjects;
};


template <class SuperHeap>
class AllocatedHeap : public SuperHeap {
public:
  AllocatedHeap (void)
    : allocated (0),
      maxAllocated (0)
  {}
  void * malloc (size_t sz) {
    void * ptr = SuperHeap::malloc (sz);
    if (ptr != NULL) {
      allocated += getSize(ptr);
      if (maxAllocated < allocated) {
	maxAllocated = allocated;
      }
    }
    return ptr;
  }
  void free (void * ptr) {
    allocated -= getSize(ptr);
    SuperHeap::free (ptr);
  }

  int getAllocated (void) const {
    return allocated;
  }
  int getMaxAllocated (void) const {
    return maxAllocated;
  }
private:
  int allocated;
  int maxAllocated;
};


template <class SuperHeap>
class StatsHeap : public SuperHeap {
public:
  ~StatsHeap (void) {
    printf ("In use = %d, allocated = %d\n", getInUse(), getAllocated());
    printf ("Max in use = %d, max allocated = %d\n", getMaxInUse(), getMaxAllocated());
  }
};

}
#endif
