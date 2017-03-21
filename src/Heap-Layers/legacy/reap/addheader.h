#if !defined(_ADDHEADER_H_)
#define _ADDHEADER_H_

/**
 * @class AddHeader
 * @brief Adds LeaHeap-compatible metadata to objects.
 */

template <class SuperHeap>
class AddHeader : public SuperHeap {
public:

  inline AddHeader (void) 
    : prevSize (sizeof(Header))
  {
  }

  inline void * malloc (const size_t sz) {
    Header h (prevSize, sz);
    prevSize = sz;
    Header * p = (Header *) SuperHeap::malloc (sz + sizeof(Header));
    *p = h;
    return (p + 1);
  }

  inline void clear (void) {
    prevSize = sizeof(Header);
    SuperHeap::clear();
  }

private:

  inline void free (void * ptr);

  /// Object headers for use by the Lea allocator.
  class Header {
  public:
    Header (size_t p, size_t s)
      : prevSize (p),
	size (s)
    {}
    /// The size of the previous (contiguous) object.
    size_t prevSize;

    /// The size of the current (immediately-following) object.
    size_t size;
  };

  /// The previous size allocated.
  size_t prevSize;
};


#endif
