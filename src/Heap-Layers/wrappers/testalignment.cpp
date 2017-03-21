#include <iostream>
#include <assert.h>
#include <string.h>
#include <stddef.h>
#include <stdalign.h>

#include <stdalign.h>
using namespace std;

int main()
{
  size_t sz = 0;
#if defined(__APPLE__)
  // Mac OS ABI requires 16-byte alignment, so we round up the size
  // to the next multiple of 16.
  if (sz < 16) {
    sz = 16;
  }
  if (sz % 16 != 0) {
    sz += 16 - (sz % 16);
  }
#endif
  cout << sz << endl;
  cout << alignof(max_align_t) << endl;
  return 0;
}
