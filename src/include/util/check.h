// -*- C++ -*-

/*

  The Hoard Multiprocessor Memory Allocator
  www.hoard.org

  Author: Emery Berger, http://www.emeryberger.com
  Copyright (c) 1998-2020 Emery Berger

  See the LICENSE file at the top-level directory of this
  distribution and at http://github.com/emeryberger/Hoard.

*/

#ifndef HOARD_CHECK_H
#define HOARD_CHECK_H

/**
 * @class Check
 * @brief Checks preconditions and postconditions on construction and destruction.
 *
 * Example usage:
 * 
 * in a method of ThisClass:
 * 
 * void foo() {
 *   Check<ThisClass, ThisClassChecker> t (this);
 *   ....
 * }
 *
 * and defined in ThisClass:
 *
 * class ThisClassChecker {
 * public:
 *   static void precondition (ThisClass * obj) { ... }
 *   static void postcondition (ThisClass * obj) { ... }
 *
 **/

namespace Hoard {

  template <class TYPE, class CHECK>
  class Check {
  public:
#ifndef NDEBUG
    Check (TYPE * t)
      : _object (t)
#else
    Check (TYPE *)
#endif
    {
#ifndef NDEBUG
      CHECK::precondition (_object);
#endif
    }
    
    ~Check() {
#ifndef NDEBUG
      CHECK::postcondition (_object);
#endif
    }
    
  private:
    Check (const Check&);
    Check& operator=(const Check&);
    
#ifndef NDEBUG
    TYPE * _object;
#endif
    
  };

}
#endif
