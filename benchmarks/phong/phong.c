#include	<errno.h>
#include	<stdlib.h>
#include	<string.h>
#include	<pthread.h>
#include        <stdio.h>
#include        <unistd.h>

#define	N_THREAD	256
#define N_ALLOC		1000000
static int		Nthread = N_THREAD;
static int		Nalloc = N_ALLOC;
static size_t		Minsize = 10;
static size_t		Maxsize = 1024;

int error(char* mesg)
{
	write(2, mesg, strlen(mesg));
	exit(1);
}

void* allocate(void* arg)
{
	int		k, p, q, c;
	size_t		sz, nalloc, len;
	char		**list;
	size_t		*size;
	int		thread = (int)((long)arg);

	unsigned int	rand = 0; /* use a local RNG so that threads work uniformly */
#define FNV_PRIME	((1<<24) + (1<<8) + 0x93)
#define FNV_OFFSET	2166136261
#define RANDOM()	(rand = rand*FNV_PRIME + FNV_OFFSET)

	nalloc = Nalloc/Nthread; /* do the same amount of work regardless of #threads */

	if(!(list = (char**)malloc(nalloc*sizeof(char*))) )
		error("failed to allocate space for list of objects\n");
	if(!(size = (size_t*)malloc(nalloc*sizeof(size_t))) )
		error("failed to allocate space for list of sizes\n");
	memset(list, 0, nalloc*sizeof(char*));
	memset(size, 0, nalloc*sizeof(size_t));

	for(k = 0; k < nalloc; ++k)
	{	
		/* get a random size favoring smaller over larger */
		len = Maxsize-Minsize+1;
		for(;;)
		{	sz = RANDOM() % len; /* pick a random size in [0,len-1] */
			if((RANDOM()%100) >= (100*sz)/len) /* this favors a smaller size */
				break;
			len = sz; /* the gods want a smaller length, try again */
		}
		sz += Minsize;

		if(!(list[k] = malloc(sz)) )
			error("malloc failed\n");
		else
		{	size[k] = sz;
			for(c = 0; c < 10; ++c)
				list[k][c*sz/10] = 'm';
		}

		if(k < 1000)
			continue;

		/* get an interval to check for free and realloc */
		p = RANDOM() % k;
		q = RANDOM() % k;
		if(p > q)
			{ c = p; p = q; q = c; }

		for(; p <= q; ++p)
		{	if(list[p])
			{	if(RANDOM()%2 == 0 ) /* 50% chance of being freed */
				{	free(list[p]);
					list[p] = 0;
					size[p] = 0;
				}
				else if(RANDOM()%4 == 0 ) /* survived free, check realloc */
				{	sz = size[p] > Maxsize ? size[p]/4 : 2*size[p];
					if(!(list[p] = realloc(list[p], sz)) )
						error("realloc failed\n");
					else
					{	size[p] = sz;
						for(c = 0; c < 10; ++c)
							list[p][c*sz/10] = 'r';
					}
				}
			}
		}
	}

	free(list);
	free(size);

	return (void*)0;
}

int main(int argc, char* argv[])
{
	int		i, rv;
	void		*status;
	pthread_t	th[N_THREAD];

	for(; argc > 1; --argc, ++argv)
	{	if(argv[1][0] != '-')
			continue;
		else if(argv[1][1] == 'a') /* # malloc calls */
			Nalloc = atoi(argv[1]+2);
		else if(argv[1][1] == 't') /* # threads */
			Nthread = atoi(argv[1]+2);
		else if(argv[1][1] == 'z') /* min block size */
			Minsize = atoi(argv[1]+2);
		else if(argv[1][1] == 'Z') /* max block size */
			Maxsize = atoi(argv[1]+2);
	}
		
	if(Nalloc <= 0 || Nalloc > N_ALLOC)
		Nalloc = N_ALLOC;
	if(Nthread <= 0 || Nthread > N_THREAD)
		Nthread = N_THREAD;
	if(Minsize <= 0)
		Minsize = 1;
	if(Maxsize < Minsize)
		Maxsize = Minsize;

	printf ("Running with %d allocations, %d threads, min size = %ld, max size = %ld\n",
		Nalloc, Nthread, Minsize, Maxsize);

	for(i = 0; i < Nthread; ++i)
	{	if((rv = pthread_create(&th[i], NULL, allocate, (void*)((long)i))) != 0 )
			error("Failed to create thread\n");
	}

	for(i = 0; i < Nthread; ++i)
	{	if((rv = pthread_join(th[i], &status)) != 0 )
			error("Failed waiting for thread\n");
	}

	return 0;
}
