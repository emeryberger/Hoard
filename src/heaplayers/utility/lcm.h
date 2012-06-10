#ifndef LCM_H
#define LCM_H

#include "gcd.h"

template <int a, int b> struct lcm
{
  static const int value = (a * b) / (gcd<a, b>::value);
};

#endif
