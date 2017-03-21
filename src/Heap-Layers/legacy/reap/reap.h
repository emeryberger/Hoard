/*

  Heap Layers: An Extensible Memory Allocation Infrastructure
  
  Copyright (C) 2000-2003 by Emery Berger
  http://www.cs.umass.edu/~emery
  emery@cs.umass.edu
  
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.
  
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.
  
  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

*/

#if !defined(_REAP_H_)
#define _REAP_H_

/**
 * @file reap.h
 * @brief The Reap API (plus the deprecated region-named API).
 * @author Emery Berger <http://www.cs.umass.edu/~emery>
 */

#if defined(_cplusplus)
extern "C" {
#endif

void reapcreate (void ** reap, void ** parent);
void * reapmalloc (void ** reap, size_t sz);
void reapfree (void ** reap, void * ptr);
void reapclear (void ** reap);
void reapdestroy (void ** reap);

int  regionFind (void ** reap, void * ptr);
void regionCreate (void ** reap, void ** parent);
void * regionAllocate (void ** reap, size_t sz);
void regionFree (void ** reap, void * ptr);
void regionFreeAll (void ** reap);
void regionDestroy (void ** reap);

#if defined(_cplusplus)
}
#endif

#endif
