
[The Hoard Memory Allocator](http://www.hoard.org)
--------------------------

Copyright (C) 1998-2020 by [Emery Berger](http://www.emeryberger.org)

The Hoard memory allocator is a fast, scalable, and memory-efficient
memory allocator that works on a range of platforms, including Linux,
Mac OS X, and Windows.

Hoard is a drop-in replacement for malloc that can dramatically
improve application performance, especially for multithreaded programs
running on multiprocessors and multicore CPUs. No source code changes
necessary: just link it in or set one environment variable (see
[Building Hoard](#building-hoard-unixmac), below).

Press
-----

*   "If you'll be running on multiprocessor machines, ... use Emery
Berger's excellent Hoard multiprocessor memory management code. It's a
drop-in replacement for the C and C++ memory routines and is very fast
on multiprocessor machines."

    * [Debugging Applications for Microsoft .NET and Microsoft Windows, Microsoft Press](http://www.microsoft.com/mspress/books/5822.aspx)

*   "(To improve scalability), consider an open source alternative such as
the Hoard Memory Manager..."

    * [Windows System Programming, Addison-Wesley](http://www.amazon.com/Windows-Programming-Addison-Wesley-Microsoft-Technology/dp/0321657748/)

*   "Hoard dramatically improves program performance through its more
efficient use of memory. Moreover, Hoard has provably bounded memory
blowup and low synchronization costs."

    * [Principles of Parallel Programming, Addison-Wesley](http://www.amazon.com/Principles-Parallel-Programming-Calvin-Lin/dp/0321487907/)

Users
-----

Companies using Hoard in their products and servers include AOL,
British Telecom, Blue Vector, Business Objects (formerly Crystal
Decisions), Cisco, Credit Suisse, Entrust, InfoVista, Kamakura,
Novell, Oktal SE, OpenText, OpenWave Systems (for their Typhoon and
Twister servers), Pervasive Software, Plath GmbH, Quest Software,
Reuters, Royal Bank of Canada, SAP, Sonus Networks, Tata
Communications, and Verite Group.

Open source projects using Hoard include the Asterisk Open Source
Telephony Project, Bayonne GNU telephony server, the Cilk parallel
programming language, the GNU Common C++ system, the OpenFOAM
computational fluid dynamics toolkit, and the SafeSquid web proxy.

Hoard is now a standard compiler option for the Standard Performance
Evaluation Corporation's CPU2006 benchmark suite for the Intel and
Open64 compilers.

Licensing
---------

Hoard has now been released under the widely-used and permissive
Apache license, version 2.0.


Why Hoard?
----------

There are a number of problems with existing memory allocators that
make Hoard a better choice.

### Contention ###


Multithreaded programs often do not scale because the heap is a
bottleneck. When multiple threads simultaneously allocate or
deallocate memory from the allocator, the allocator will serialize
them. Programs making intensive use of the allocator actually slow
down as the number of processors increases. Your program may be
allocation-intensive without you realizing it, for instance, if your
program makes many calls to the C++ Standard Template Library (STL). Hoard eliminates this bottleneck.

### False Sharing ###

System-provided memory allocators can cause insidious problems for multithreaded code. They can
lead to a phenomenon known as "false sharing": threads on different CPUs
can end up with memory in the same cache line, or chunk of
memory. Accessing these falsely-shared cache lines is hundreds of
times slower than accessing unshared cache lines. Hoard is designed to prevent false sharing.

### Blowup ###

Multithreaded programs can also lead the allocator to blowup memory
consumption. This effect can multiply the amount of memory needed to
run your application by the number of CPUs on your machine: four CPUs
could mean that you need four times as much memory. Hoard is guaranteed (provably!) to bound memory consumption.


## Installation

### Homebrew (Mac OS X)

You can use Homebrew to install the current version of Hoard as follows:

    brew tap emeryberger/hoard
    brew install --HEAD emeryberger/hoard/libhoard

This not only installs the Hoard library, but also creates a `hoard` command you can use to run Hoard with anything at the command-line.

    hoard myprogram-goes-here

-------------------------
### Building Hoard from source (Mac OS X, Linux, and Windows WSL2)

To build Hoard from source, do the following:

    git clone https://github.com/emeryberger/Hoard
    cd src
    make

You can then use Hoard by linking it with your executable, or
by setting the `LD_PRELOAD` environment variable, as in

    export LD_PRELOAD=/path/to/libhoard.so

or, in Mac OS X:

    export DYLD_INSERT_LIBRARIES=/path/to/libhoard.dylib

------------------------
### Building Hoard (Windows)

Change into the `src` directory and build the Windows version:

    C:\hoard\src> nmake

To use Hoard, link your executable with `source\uselibhoard.cpp` and `libhoard.lib`.
You *must* use the `/MD` flag.

Example:

    C:\hoard\src> cl /Ox /MD yourapp.cpp source\uselibhoard.cpp libhoard.lib

To run `yourapp.exe`, you will need to have `libhoard.dll` in your path.

Benchmarks
----------

The directory `benchmarks/` contains a number of benchmarks used to
evaluate and tune Hoard.


Technical Information
---------------------

Hoard has changed quite a bit over the years, but for technical details of the first version of Hoard, read [Hoard: A
Scalable Memory Allocator for Multithreaded Applications](http://dl.acm.org/citation.cfm?id=379232),
by Emery D. Berger, Kathryn S. McKinley, Robert D. Blumofe, and Paul
R. Wilson. The Ninth International Conference on Architectural Support
for Programming Languages and Operating Systems
(ASPLOS-IX). Cambridge, MA, November 2000.

