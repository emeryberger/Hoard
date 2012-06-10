/* shbench.cpp:
 *   SmartHeap (tm) Portable multi-thread memory management benchmark.
 *
 * Copyright (C) 1991-1998 Compuware Corporation.
 * All Rights Reserved.
 *
 * No part of this source code may be copied, modified or reproduced
 * in any form without retaining the above copyright notice.
 * This source code, or source code derived from it, may not be redistributed
 * without express written permission of the copyright owner.
 *
 *
 *  Flag                   Meaning
 *  -----------------------------------------------------------------------
 *  SYS_MULTI_THREAD=1  Test with multiple threads (OS/2, NT, HP, Solaris only)
 *  SYS_SMP=1           Test with multiple processors (NT, HP-UX, Solaris only)
 * 
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <assert.h>
#include <limits.h>

#define SYS_MULTI_THREAD 1
#define SYS_SMP 1
#define _REENTRANT 1
#define _MT 1

//#include "smrtheap.h"

#if defined(SHANSI)
#include "shmalloc.h"
int SmartHeap_malloc = 0;
#endif


#ifdef USE_ROCKALL
//#include "FastHeap.hpp"
//FAST_HEAP theFastHeap (1024 * 1024, true, true, true);

typedef int SBIT32;
typedef void VOID;
#define BOOLEAN moolean
typedef bool moolean;
#include "SmpHeap.hpp"
#undef BOOLEAN


SMP_HEAP theFastHeap (1024 * 1024, true, true, true);

void * operator new( unsigned int cb )
{
  void *pRet = theFastHeap.New ((size_t)cb) ;
  return pRet;
}

void operator delete(void *pUserData )
{
  theFastHeap.Delete (pUserData) ;
}

#define malloc(s) theFastHeap.New(s)
#define free(p) theFastHeap.Delete(p)

#endif



#if defined(WIN32) || defined(__WIN32__) || defined(_WIN32)
#ifndef WIN32
#define WIN32 1
#endif
#include <windows.h>
#ifdef _MT
#define SYS_MULTI_THREAD 1
#include <process.h>
typedef HANDLE ThreadID;
#endif /* _MT */

#elif defined(__APPLE__) || defined(__hpux) || defined(_AIX) || defined(__osf__) || defined(sgi)
#if defined(_THREAD_SAFE) || defined(_REENTRANT)
#define SYS_MULTI_THREAD 1
#define _INCLUDE_POSIX_SOURCE
#include <unistd.h>
#include <sys/signal.h>
#include <pthread.h>
typedef pthread_t ThreadID;
#if defined(_DECTHREADS_) && !defined(__osf__)
ThreadID ThreadNULL = {0, 0, 0};
#define THREAD_NULL ThreadNULL
#define THREAD_EQ(a,b) pthread_equal(a,b)
#endif /* _DECTHREADS */
#endif /* _THREAD_SAFE */

#elif defined(__sun) || defined(linux)
#ifdef _REENTRANT
#define SYS_MULTI_THREAD 1
#include <pthread.h>
typedef pthread_t ThreadID;
#endif /* _REENTRANT */

#elif defined(__OS2__) || defined(__IBMC__)
#ifdef _MT
#define SYS_MULTI_THREAD 1
#include <process.h>
#define INCL_DOS
#define INCL_DOSPROCESS    /* thread control	*/
#define INCL_DOSMEMMGR     /* Memory Manager values */
#define INCL_DOSERRORS     /* Get memory error codes */
#include <os2.h>
#include <bsememf.h>      /* Get flags for memory management */
typedef TID ThreadID; 
#endif /* _MT */

#endif /* end of environment-specific header files */

#ifndef THREAD_NULL
#define THREAD_NULL 0
#endif
#ifndef THREAD_EQ
#define THREAD_EQ(a,b) ((a)==(b))
#endif


typedef struct
{
	unsigned long size;
	char *block;
} AllocRec;

class AllocArray
{
public:
	AllocArray(unsigned long size) : count(0), allocArray(new AllocRec[size]) {}
	~AllocArray() { delete allocArray; }
	unsigned long count;
	AllocRec *allocArray;
};

#define MAX_ALLOC (UINT_MAX - 0x0100)

#define TRUE 1
#define FALSE 0
typedef int Bool;

unsigned uMaxBlockSize = 100;
unsigned uMinBlockSize = 8;
unsigned long ulCallCount = 50000000;
unsigned long ulHeapSize = 10000;
unsigned uThreadCount = 16;

unsigned randSize(void);
unsigned long promptAndRead(char *msg, unsigned long defaultVal, char fmtCh);
void AddAlloc(AllocArray *a, char *block, unsigned sz);
void RemoveAlloc(AllocArray *a, AllocRec *r);
AllocRec *GetAlloc(AllocArray *a);
void Benchmark(AllocArray *);

#ifdef SYS_MULTI_THREAD
ThreadID RunThread(void (*fn)(void *), void *arg);
void WaitForThreads(ThreadID[], unsigned);
#endif

void doBench(void *);

#include "timer.h"
using namespace HL;


int main(int argc, char *argv[])
{
  Timer startTime;
	double elapsedTime;
	
	setbuf(stdout, NULL);  /* turn off buffering for output */

#ifdef SMARTHEAP
	MemRegisterTask();
#endif
	ulCallCount = promptAndRead("call count (per pool)", ulCallCount, 'u');

#ifdef SYS_MULTI_THREAD
	{
		unsigned i;
		ThreadID *tids;

#if 0
#if defined(WIN32) && defined(SYS_SMP)
		unsigned uCPUs = promptAndRead("CPUs (0 for all)", 0, 'u');

		if (uCPUs)
		{
			DWORD m1, m2;

			if (GetProcessAffinityMask(GetCurrentProcess(), &m1, &m2))
			{
				i = 0;
				m1 = 1;

				/*
				 * iterate through process affinity mask m2, counting CPUs up to
				 * the limit specified in uCPUs
				 */
				do
					if (m2 & m1)
						i++;
				while ((m1 <<= 1) && i < uCPUs);

				/* clear any extra CPUs in affinity mask */
				do
					if (m2 & m1)
						m2 &= ~m1;
				while (m1 <<= 1);

				if (SetProcessAffinityMask(GetCurrentProcess(), m2))
					printf("\nThreads in benchmark will run on max of %u CPUs", i);
			}
		}
#endif /* WIN32 && SMP */
#endif
		
		uThreadCount = (int)promptAndRead("threads", 2, 'u');

		if (uThreadCount < 1)
			uThreadCount = 1;
		ulCallCount /= uThreadCount;
		ulHeapSize /= uThreadCount;
		tids = new ThreadID[uThreadCount];

		startTime.start ();
		if (0) { // uThreadCount = 1) {
		  doBench(NULL);
		} else {
		  for (i = 0;  i < uThreadCount;  i++)
		    if (THREAD_EQ(tids[i] = RunThread(doBench, NULL), THREAD_NULL))
		      {
			printf("\nfailed to start thread #%d", i);
			break;
		      } else {
			printf ("\ncreated thread id %d.", tids[i]);
		      }
		  
		  WaitForThreads(tids, uThreadCount);
		}

		delete tids;
	}
#else
	uThreadCount = 1;
	startTime.start();
	doBench(NULL);
#endif

	startTime.stop();
	elapsedTime = (double) startTime;
	printf("Total elapsed time to perform %lu heap operations"
#ifdef SYS_MULTI_THREAD
			 "\n   in %d concurrent threads"
#endif
			 ": %f seconds\n\n", ulCallCount * uThreadCount,
#ifdef SYS_MULTI_THREAD
			 uThreadCount,
#endif
			 elapsedTime);

	return 0;
}

void doBench(void *)
{
	AllocArray *allocArray = new AllocArray(ulHeapSize);

	srand(0);
	Benchmark(allocArray);

	delete allocArray;
}

/* mixed new/delete's */
void Benchmark(AllocArray *arr)
{
	unsigned long call = ulCallCount;
	unsigned char size = 0;

	// first alloc one of each size
	while (--size != 0)
	{
		malloc(size+1);
	}

	// then do randomly interspersed alloc/frees
	// but don't call rand() as this is itself a source of SMP
	// contention in some CRT implementations
	while (call--)
	{
		AllocRec *a;

//		switch (rand() % 2)
		if (size-- % 2 == 0)
		{
//			case 0: /* free */
				if ((a = GetAlloc(arr)) != NULL)
				{
//					delete a->block;
					free(a->block);
					RemoveAlloc(arr, a);
				}
//				break;
				else

//			case 1:
				if (arr->count < ulHeapSize)
				{
//					unsigned size = randSize();
//					char *mem = new char[size];
					char *mem = (char *)malloc(size+1);
					
					if (mem)
						AddAlloc(arr, mem, size);
				}
//				break;				
		}
	}
}

void AddAlloc(AllocArray *a, char *block, unsigned sz)
{
	AllocRec *rec = &a->allocArray[a->count++];
	rec->block = block;
	rec->size = sz;
}

void RemoveAlloc(AllocArray *a, AllocRec *r)
{
	assert(r < a->allocArray + a->count);
	*r = a->allocArray[--a->count];
}

AllocRec *GetAlloc(AllocArray *a)
{
	if (a->count)
//		return &a->allocArray[(unsigned)(rand() + rand()) % a->count];
		return &a->allocArray[a->count-1];
	else
		return NULL; 
}

unsigned randSize()
{
	return uMinBlockSize + rand() % (uMaxBlockSize - uMinBlockSize + 1);
}

unsigned long promptAndRead(char *msg, unsigned long defaultVal, char fmtCh)
{
	char *arg = NULL, *err;
	unsigned long result;
	{
		char buf[12];
		static char fmt[] = "\n%s [%lu]: ";
		fmt[7] = fmtCh;
		printf(fmt, msg, defaultVal);
		if (fgets(buf, 11, stdin))
			arg = &buf[0];
	}
	if (arg && ((result = strtoul(arg, &err, 10)) != 0
					|| (*err == '\n' && arg != err)))
	{
		return result;
	}
	else
		return defaultVal;
}


/*** System-Specific Interfaces ***/

#ifdef SYS_MULTI_THREAD
ThreadID RunThread(void (*fn)(void *), void *arg)
{
	ThreadID result = THREAD_NULL;
	
#if defined(__OS2__) && (defined(__IBMC__) || defined(__IBMCPP__) || defined(__WATCOMC__))
	if ((result = _beginthread(fn, NULL, 8192, arg)) == THREAD_NULL)
		return THREAD_NULL;

#elif (defined(__OS2__) && defined(__BORLANDC__)) || defined(WIN32)
	if ((result = (ThreadID)_beginthread(fn, 8192, arg)) == THREAD_NULL)
		return THREAD_NULL;

#elif 0 // defined(__sun)
	return thr_create(NULL, 0, (void *(*)(void *))fn, arg, THR_BOUND, NULL)==0;

#elif defined(_DECTHREADS_) && !defined(__osf__)
	if (pthread_create(&result, pthread_attr_default,
							 (pthread_startroutine_t)fn, arg) == -1)
		 return THREAD_NULL;

#elif defined(_POSIX_THREADS) || defined(_POSIX_REENTRANT_FUNCTIONS) \
	|| _POSIX_C_SOURCE >= 199506L
	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setscope (&attr, PTHREAD_SCOPE_SYSTEM);
#ifdef _AIX
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_UNDETACHED);
#endif /* _AIX */
	if (pthread_create(&result, &attr, (void *(*)(void *))fn, arg) == -1)
		return THREAD_NULL;

#else
#error Unsupported threads package
#endif /* threading implementations */

	return result;
}

/* wait for all benchmark threads to terminate */
void WaitForThreads(ThreadID tids[], unsigned tidCnt)
{
#if defined(WIN32)
	WaitForMultipleObjects(tidCnt, tids, TRUE, INFINITE);

#elif defined(__OS2__)
 	while (tidCnt--)
 		DosWaitThread(&tids[tidCnt], DCWW_WAIT);

#elif 0 // defined(__sun)
	int prio;
	thr_getprio(thr_self(), &prio);
	int newPrio = (prio-1 > 0)? prio-1 : 0;
	thr_setprio(thr_self(), newPrio);
	while (tidCnt--)
		thr_join(0, NULL, NULL);
	
#elif defined(_POSIX_THREADS) || defined(_POSIX_REENTRANT_FUNCTIONS) \
	|| _POSIX_C_SOURCE >= 199506L
 	while (tidCnt--)
 		pthread_join(tids[tidCnt], NULL);

#else
#error Unsupported threads package
#endif /* threading implementations */
}
#endif /* SYS_MULTI_THREAD */
