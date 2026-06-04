// -*- C++ -*-

/*

  The Hoard Multiprocessor Memory Allocator
  www.hoard.org

  Author: Emery Berger, http://www.emeryberger.com
  Copyright (c) 1998-2020 Emery Berger

  See the LICENSE file at the top-level directory of this
  distribution and at http://github.com/emeryberger/Hoard.

*/

#ifndef HOARD_THRESHOLD_SEGHEAP_H
#define HOARD_THRESHOLD_SEGHEAP_H

#if defined(_MSC_VER)
#include <intrin.h>
static inline int hoard_ctzll(unsigned long long x) {
  unsigned long idx;
#if defined(_WIN64)
  _BitScanForward64(&idx, x);
#else
  if ((unsigned long)x != 0) { _BitScanForward(&idx, (unsigned long)x); }
  else { _BitScanForward(&idx, (unsigned long)(x >> 32)); idx += 32; }
#endif
  return (int)idx;
}
#else
#define hoard_ctzll(x) __builtin_ctzll(x)
#endif

namespace Hoard {

  // Allows superheap to hold at least ThresholdSlop but no more than
  // ThresholdFraction% more memory than client currently holds.

  template <int ThresholdFraction, // % over current allowed in superheap.
	    int ThresholdSlop,     // constant amount allowed in superheap.
	    int NumBins,
	    int (*getSizeClass) (const size_t),
	    size_t (*getClassMaxSize) (const int),
	    size_t MaxObjectSize,
	    class LittleHeap,
	    class BigHeap>
  class ThresholdSegHeap : public BigHeap
  {
  public:

    ThresholdSegHeap()
      : _currLive (0),
	_maxLive (0),
	_maxFraction (1.0 + (double) ThresholdFraction / 100.0),
	_cleared (false)
    {
      for (int i = 0; i < BITMAP_WORDS; i++) {
	_activeBins[i] = 0;
      }
    }

    size_t getSize (void * ptr) {
      return BigHeap::getSize(ptr);
    }

    void * malloc (size_t sz) {
      if (sz >= MaxObjectSize) {
	return BigHeap::malloc (sz);
      }
      // Once the amount of cached memory in the superheap exceeds the
      // desired threshold over max live requested by the client, dump
      // it all.
      const int sizeClass = getSizeClass (sz);
      const size_t maxSz = getClassMaxSize (sizeClass);
      if (sizeClass >= NumBins) {
	return BigHeap::malloc (maxSz);
      } else {
	void * ptr = _heap[sizeClass].malloc (maxSz);
	if (ptr == nullptr) {
	  return BigHeap::malloc (maxSz);
	}
	assert (getSize(ptr) <= maxSz);
	_currLive += getSize (ptr);
	// Mark this bin as active for selective clearing.
	_activeBins[sizeClass / 64] |= (1ULL << (sizeClass % 64));
	if (_currLive >= _maxLive) {
	  _maxLive = _currLive;
	  _cleared = false;
	}
	return ptr;
      }
    }

    void free (void * ptr, size_t sz) {
      if (sz >= MaxObjectSize) {
	BigHeap::free (ptr, sz);
	return;
      }
      int cl = getSizeClass (sz);
      if (cl >= NumBins) {
	BigHeap::free (ptr);
	return;
      }
      if (_currLive < sz) {
	_currLive = 0;
      } else {
	_currLive -= sz;
      }
      _heap[cl].free (ptr);
      // Mark this bin as active.
      _activeBins[cl / 64] |= (1ULL << (cl % 64));
      bool crossedThreshold = (double) _maxLive > _maxFraction * (double) _currLive;
      if ((_currLive > ThresholdSlop) && crossedThreshold && !_cleared)
	{
	  // When we drop below the threshold, clear only active bins.
	  for (int w = 0; w < BITMAP_WORDS; w++) {
	    auto bits = _activeBins[w];
	    while (bits) {
	      int bit = hoard_ctzll(bits);
	      int i = bit + w * 64;
	      if (i < NumBins) {
		_heap[i].clear();
	      }
	      bits &= bits - 1;  // Clear lowest set bit.
	    }
	    _activeBins[w] = 0;
	  }
	  // We won't clear again until we reach maxlive again.
	  _cleared = true;
	  _maxLive = _currLive;
	}
    }
    
    void free (void * ptr) {
      // Update current live memory stats, then free the object.
      size_t sz = getSize(ptr);
      free(ptr, sz);
    }

  private:

    /// Number of 64-bit words needed for the active bins bitmap.
    enum { BITMAP_WORDS = (NumBins + 63) / 64 };

    /// The current amount of live memory held by a client of this heap.
    unsigned long _currLive;

    /// The maximum amount of live memory held by a client of this heap.
    unsigned long _maxLive;

    /// Maximum fraction calculation.
    const double _maxFraction;

    /// Have we already cleared out the superheap?
    bool _cleared;

    /// Bitmap tracking which bins have been used since last clear.
    unsigned long long _activeBins[BITMAP_WORDS];

    LittleHeap _heap[NumBins];
  };

}

#endif

