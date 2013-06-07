// -*- C++ -*-

#ifndef HOARD_THRESHOLD_SEGHEAP_H
#define HOARD_THRESHOLD_SEGHEAP_H

namespace Hoard {

  // Allows superheap to hold at least ThresholdSlop but no more than
  // ThresholdFraction% more memory than client currently holds.

  template <int ThresholdFraction, // % over current allowed in superheap.
	    int ThresholdSlop,     // constant amount allowed in superheap.
	    int NumBins,
	    int (*getSizeClass) (const size_t),
	    size_t (*getClassMaxSize) (const int),
	    class LittleHeap,
	    class BigHeap>
  class ThresholdSegHeap :
    public StrictSegHeap<NumBins,
			 getSizeClass,
			 getClassMaxSize,
			 LittleHeap,
			 BigHeap>
  {
  private:
    typedef StrictSegHeap<NumBins,
			  getSizeClass,
			  getClassMaxSize,
			  LittleHeap,
			  BigHeap> SuperHeap;
  public:

    ThresholdSegHeap()
      : _currLive (0),
	_maxLive (0)
    {}

    void * malloc (size_t sz) {
      // Once the amount of cached memory in the superheap exceeds the
      // desired threshold over max live requested by the client, dump
      // it all.
      void * ptr = SuperHeap::malloc (sz);
      _currLive += SuperHeap::getSize (ptr);
      if (_currLive > _maxLive) {
	_maxLive = _currLive;
      }
      return ptr;
    }

    void free (void * ptr) {
      // Update current live memory stats, then free the object.
      size_t sz = SuperHeap::getSize(ptr);
      assert (_currLive >= sz);
      _currLive -= sz;
      SuperHeap::free (ptr);
      double maxFraction = (1.0 + (double) ThresholdFraction / 100.0);
      double currentFraction = (double) _maxLive / (double) _currLive;
      if ((_currLive > ThresholdSlop) && (currentFraction > maxFraction))
	{
	  SuperHeap::clear();
	}
    }

  private:

    /// The current amount of live memory held by a client of this heap.
    unsigned long _currLive;

    /// The maximum amount of live memory held by a client of this heap.
    unsigned long _maxLive;
  };

}

#endif

