#ifndef _HL_HASH_H_
#define _HL_HASH_H_

#include <cstdlib>

namespace HL {
  template <typename Key>
    extern size_t hash (Key k);
  
  template <>
    inline size_t hash (void * v) {
    return (size_t) v;
  }
  
  template <>
    inline size_t hash (const void * v) {
    return (size_t) ((size_t) v);
  }
  
  template <>
    inline size_t hash (int v) {
    return (size_t) v;
  }
}

#endif
