/*

  The Hoard Multiprocessor Memory Allocator
  www.hoard.org

  Author: Emery Berger, http://www.emeryberger.com
  Copyright (c) 1998-2020 Emery Berger

  See the LICENSE file at the top-level directory of this
  distribution and at http://github.com/emeryberger/Hoard.

*/

#include "dynamichashtable.h"

#include <iostream>
#include <sstream>

using namespace std;

//#include "heaplayers.h"
using namespace HL;

class Item {
public:

  Item()
    : _hash (0)
  {}

  Item (unsigned long value,
	string s)
    : _hash (value),
      _str (s)
  {}

  unsigned long hashCode() const {
    return _hash;
  }

  const string& getString() const {
    return _str;
  }

private:
  unsigned long _hash;
  string _str;
};

int
main()
{
  const unsigned int NUM_ITERATIONS = 100000;
  DynamicHashTable<Item, 2, 128> dh;

  for (unsigned int i = 0; i < NUM_ITERATIONS; i++) {
    stringstream ss;
    ss << "foo:" << i;
    dh.insert (Item (i, ss.str()));
  }

  Item k;
  for (unsigned int i = 0; i < 100000; i++) {
    bool r0 = dh.erase (rand() % NUM_ITERATIONS);
  }

  for (unsigned int i = 0; i < NUM_ITERATIONS-1; i++) {
    bool r = dh.get (i, k);
    cout << "r = " << r << endl;
    //    assert (r);
  }


  return 0;
}
