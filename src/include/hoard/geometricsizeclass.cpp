/*

  The Hoard Multiprocessor Memory Allocator
  www.hoard.org

  Author: Emery Berger, http://www.emeryberger.com
  Copyright (c) 1998-2020 Emery Berger

  See the LICENSE file at the top-level directory of this
  distribution and at http://github.com/emeryberger/Hoard.

*/

#include <stdint.h>

constexpr int size2class (const uint64_t sz) {
  // Do a binary search to find the right size class.
  int left  = 0;
  int right = 100; // FIXME 
  while (left < right) {
    int mid = (left + right)/2;
    if (c2s(mid) < sz) {
      left = mid + 1;
    } else {
      right = mid;
    }
  }
  assert (c2s(left) >= sz);
  assert ((left == 0) || (c2s(left-1) < sz));
  return left;
}

int
main()
{
  auto const v = size2class(64);
#if 0
  Hoard::GeometricSizeClass<12, 16> gs1;
  Hoard::GeometricSizeClass<14, 16> gs2;
  Hoard::GeometricSizeClass<16, 8> gs3;
  Hoard::GeometricSizeClass<18, 8> gs4;
  Hoard::GeometricSizeClass<20, 16> gs5;
#endif
  return 0;
}
