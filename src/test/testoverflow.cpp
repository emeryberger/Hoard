#include <iostream>

#ifndef __APPLE__
#include <malloc.h>
#endif


#include <limits.h>

using namespace std;

int main()
{
  cout << "size = " << (size_t) -1 << endl;
  cout << "uint_max = " << UINT_MAX << endl;
  cout << "ulong_max = " << ULONG_MAX << endl;

  void * m = malloc((size_t) -1);
  void * c = calloc(UINT_MAX, UINT_MAX);
  int  * n = new int[UINT_MAX];

  cout << "Result of malloc = " << m << endl
       << "Result of calloc = " << c << endl
       << "Result of new    = " << n << endl;

  return 0;
}
