#ifndef GCD_H
#define GCD_H

template <int a, int b> struct gcd
{
  static const int value = gcd<b, a%b>::value;
};

template <int a> struct gcd<a, 0>
{
  static const int value = a;
};


#endif
