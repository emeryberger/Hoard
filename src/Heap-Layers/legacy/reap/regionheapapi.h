#ifndef _REGIONHEAPAPI_H_
#define _REGIONHEAPAPI_H_

#if __cplusplus
#define C_LINKAGE extern "C"
#else
#define C_LINKAGE extern
#endif

C_LINKAGE void   regionCreate   (void ** reg, void ** parent);
C_LINKAGE void   regionDestroy  (void ** reg);
C_LINKAGE void * regionAllocate (void ** reg, size_t sz);
C_LINKAGE void   regionFreeAll  (void ** reg);
C_LINKAGE void   regionFree     (void ** reg, void * ptr);
C_LINKAGE int    regionFind     (void ** reg, void * ptr);

#endif
