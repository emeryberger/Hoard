# Commands to compile Hoard for various targets.
# Run make (with no arguments) to see the complete target list.

all:
	@echo To build Hoard, specify the desired build target:
	@echo -------------------------------------------------
	@echo freebsd
	@echo linux-gcc-x86
	@echo linux-gcc-x86-64
	@echo macos
	@echo solaris-sunw-sparc
	@echo solaris-sunw-x86
	@echo solaris-sunw-x86-64
	@echo solaris-gcc-sparc
	@echo generic-gcc
	@echo windows

.PHONY: macos freebsd linux-gcc-x86 linux-gcc-x86-debug solaris-sunw-sparc solaris-sunw-x86 solaris-gcc-sparc generic-gcc linux-gcc-x86-64 windows clean

#
# Source files
#

MAIN_SRC  = source/libhoard.cpp
UNIX_SRC  = $(MAIN_SRC) source/unixtls.cpp
SUNW_SRC  = $(UNIX_SRC) heaplayers/wrappers/wrapper.cpp
GNU_SRC   = $(UNIX_SRC) heaplayers/wrappers/gnuwrapper.cpp
MACOS_SRC = $(MAIN_SRC) heaplayers/wrappers/macwrapper.cpp source/mactls.cpp

#
# All dependencies.
#

DEPS = $(MACOS_SRC) $(UNIX_SRC) source/libhoard.cpp

#
# Include directories
#

INCLUDES = -I. -Iinclude -Iinclude/util -Iinclude/hoard -Iinclude/superblocks -Iheaplayers -Iheaplayers/arch-specific -Iheaplayers/wrappers -Iheaplayers/utility -Iheaplayers/locks -Iheaplayers/threads -Iheaplayers/heaps/buildingblock -Iheaplayers/heaps/threads -Iheaplayers/heaps/utility -Iheaplayers/heaps/general -Iheaplayers/heaps/combining -Iheaplayers/heaps/objectrep -Iheaplayers/heaps/top -Iheaplayers/heaps/debug -Iheaplayers/heaps/special

WIN_INCLUDES = /I. /Iinclude /Iinclude/util /Iinclude/hoard /Iinclude/superblocks /Iheaplayers /Iheaplayers/arch-specific /Iheaplayers/wrappers /Iheaplayers/utility /Iheaplayers/locks /Iheaplayers/threads /Iheaplayers/heaps/buildingblock /Iheaplayers/heaps/threads /Iheaplayers/heaps/utility /Iheaplayers/heaps/general /Iheaplayers/heaps/combining /Iheaplayers/heaps/objectrep /Iheaplayers/heaps/top /Iheaplayers/heaps/debug -Iheaplayers/heaps/special

#
# Compile commands for individual targets.
#

FREEBSD_COMPILE = g++ -O2 -DNDEBUG  -DNDEBUG  $(INCLUDES) -D_REENTRANT=1 -shared $(UNIX_SRC) -Bsymbolic -o libhoard.so -pthread -fPIC

MACOS_COMPILE = g++ -arch i386 -arch x86_64 -pipe -g -O3 -Wall -DNDEBUG $(INCLUDES) -D_REENTRANT=1 -compatibility_version 1 -current_version 1 -dynamiclib -D'CUSTOM_PREFIX(x)=xx\#\#x' $(MACOS_SRC) -o libhoard.dylib -ldl -lpthread 

MACOS_COMPILE_DEBUG = g++ -arch i386 -arch x86_64 -pipe -g -O0 -Wall $(INCLUDES) -D_REENTRANT=1 -compatibility_version 1 -current_version 1 -dynamiclib $(MACOS_SRC) -o libhoard.dylib -ldl -lpthread

LINUX_GCC_x86_COMPILE = g++ -m32 -I/usr/include/nptl -ffast-math -g -malign-double -pipe -march=pentium4 -O3 -finline-limit=20000 -finline-functions  -DNDEBUG  $(INCLUDES) -D_REENTRANT=1 -shared  $(GNU_SRC) -Bsymbolic -o libhoard.so -ldl -lpthread

LINUX_GCC_x86_64_COMPILE = g++ -g -W -Wconversion -Wall -m64 -I/usr/include/nptl -pipe -fPIC -O3 -finline-limit=20000 -finline-functions  -DNDEBUG  $(INCLUDES) -D_REENTRANT=1 -shared   $(GNU_SRC) -Bsymbolic -o libhoard.so -ldl -lpthread

LINUX_GCC_x86_64_COMPILE_DEBUG = g++ -W -Wconversion -Wall -march=nocona -m64 -I/usr/include/nptl -pipe -fPIC -O0 -g $(INCLUDES) -D_REENTRANT=1 -shared   $(GNU_SRC) -Bsymbolic -o libhoard.so -ldl -lpthread

LINUX_GCC_x86_COMPILE_STATIC = g++ -m32 -I/usr/include/nptl -static -pipe -march=pentium4 -O3 -finline-limit=20000 -finline-functions  -DNDEBUG  $(INCLUDES) -D_REENTRANT=1  -c $(GNU_SRC) ; ar cr libhoard.a libhoard.o

LINUX_GCC_x86_COMPILE_DEBUG = g++ -m32 -O0 -fPIC -fno-inline -I/usr/include/nptl  -g -pipe $(INCLUDES) -D_REENTRANT=1  -shared $(GNU_SRC) -Bsymbolic -o libhoard.so -ldl -lpthread

SOLARIS_SUNW_SPARC_COMPILE_32_DEBUG = CC -dalign -xbuiltin=%all -fast -mt -g -xildoff -xthreadvar=dynamic -L/usr/lib/lwp -R/usr/lib/lwp -DNDEBUG $(INCLUDES) -D_REENTRANT=1 -G -PIC $(SUNW_SRC) heaplayers/arch-specific/sparc-interchange.il -o libhoard_32.so -lthread -ldl -lCrun

SOLARIS_SUNW_SPARC_COMPILE_32 = CC -dalign -xbuiltin=%all -fast -xO5 -DNDEBUG -mt -g -xtarget=ultra2 -xildoff -xthreadvar=dynamic -L/usr/lib/lwp -R/usr/lib/lwp $(INCLUDES) -D_REENTRANT=1 -G -PIC $(SUNW_SRC) heaplayers/arch-specific/sparc-interchange.il -o libhoard_32.so -lthread -ldl -lCrun 

SOLARIS_SUNW_SPARC_COMPILE_64 = CC -xcode=pic13 -m64 -mt -fast -dalign -xbuiltin=%all -xO5 -xtarget=ultra2 -xildoff -xthreadvar=dynamic -L/usr/lib/lwp -R/usr/lib/lwp -DNDEBUG $(INCLUDES) -D_REENTRANT=1 -G -PIC $(SUNW_SRC) heaplayers/arch-specific/sparc-interchange.il -o libhoard_64.so -lthread -ldl -lCrun

SOLARIS_SUNW_x86_COMPILE_32 = CC -fns -fsimple=2 -ftrap=%none -xbuiltin=%all -mt -xO5 -fast -xildoff -xthreadvar=dynamic -L/usr/lib/lwp -R/usr/lib/lwp -DNDEBUG $(INCLUDES) -D_REENTRANT=1 -G -PIC $(SUNW_SRC) heaplayers/util/x86-interchange.il -o libhoard_32.so -lthread -ldl -lCrun

SOLARIS_SUNW_x86_COMPILE_32_DEBUG = CC -mt -g -xildoff -xthreadvar=dynamic -L/usr/lib/lwp -R/usr/lib/lwp $(INCLUDES) -D_REENTRANT=1 -G -PIC $(SUNW_SRC) heaplayers/util/x86-interchange.il -o libhoard_32.so -lthread -ldl -lCrun

SOLARIS_SUNW_x86_COMPILE_64 = CC -xarch=amd64 -fns -fsimple=2 -ftrap=%none -xbuiltin=%all -xO5 -xildoff -xthreadvar=dynamic -L/usr/lib/lwp -R/usr/lib/lwp -DNDEBUG $(INCLUDES) -D_REENTRANT=1 -G -PIC $(SUNW_SRC) heaplayers/util/x86_64-interchange.il -o libhoard_64.so -lthread -ldl -lCrun

SOLARIS_GCC_SPARC_COMPILE_32 = g++ -nostartfiles -pipe -DNDEBUG -mcpu=ultrasparc -m32 -O3 -finline-limit=20000 -fPIC -fkeep-inline-functions -finline-functions -ffast-math $(INCLUDES) -D_REENTRANT=1 -shared $(GNU_SRC) -lthread -lpthread -ldl -o libhoard_32.so

SOLARIS_GCC_SPARC_COMPILE_64 = g++ -nostartfiles -pipe -DNDEBUG -mcpu=ultrasparc -m64 -O3 -finline-limit=20000 -fPIC -fkeep-inline-functions -finline-functions -ffast-math $(INCLUDES) -D_REENTRANT=1 -shared $(GNU_SRC) -lthread -lpthread -ldl -o libhoard_64.so

SOLARIS_GCC_SPARC_COMPILE_DEBUG = g++ -nostartfiles -pipe -mcpu=ultrasparc -g -fPIC $(INCLUDES) -D_REENTRANT=1 -shared $(GNU_SRC) -lthread -lpthread -ldl -o libhoard.so

GENERIC_GCC_COMPILE = g++ -I/usr/include/nptl -pipe -O3 -finline-limit=20000 -finline-functions  -DNDEBUG  $(INCLUDES) -D_REENTRANT=1 -shared $(GNU_SRC) -Bsymbolic -o libhoard.so -ldl -lpthread


windows: libhoard.dll

libhoard.dll: $(DEPS)
	cl $(WIN_INCLUDES) /Zi /nologo /W1 /WX- /Ox /Oi /Oy- /GL /D "_WINRT_DLL" /D "NDEBUG" /D "_WINDOWS" /D "_WINDLL" /D "_UNICODE" /D "UNICODE" /D "WINAPI_FAMILY=2" /Gm- /EHsc /MD /GS /Gy /fp:precise /Zc:wchar_t /Zc:forScope /Gd /errorReport:queue "source\libhoard.cpp" "heaplayers\wrappers\winwrapper.cpp" "source\wintls.cpp" /link /DLL /subsystem:console /OUT:libhoard.dll

macos:
	$(MACOS_COMPILE)

macos-debug:
	$(MACOS_COMPILE_DEBUG)

freebsd:
	$(FREEBSD_COMPILE)

generic-gcc:
	$(GENERIC_GCC_COMPILE)

linux-gcc-x86:
	$(LINUX_GCC_x86_COMPILE)

linux-gcc-x86-static:
	$(LINUX_GCC_x86_COMPILE_STATIC)

linux-gcc-x86-debug:
	$(LINUX_GCC_x86_COMPILE_DEBUG)

linux-gcc-x86-64:
	$(LINUX_GCC_x86_64_COMPILE)

linux-gcc-x86-64-debug:
	$(LINUX_GCC_x86_64_COMPILE_DEBUG)

solaris-sunw-sparc-debug:
	$(SOLARIS_SUNW_SPARC_COMPILE_32_DEBUG)

solaris-sunw-sparc:
	$(SOLARIS_SUNW_SPARC_COMPILE_32)
	$(SOLARIS_SUNW_SPARC_COMPILE_64)

solaris-gcc-sparc:
	$(SOLARIS_GCC_SPARC_COMPILE_32)
	$(SOLARIS_GCC_SPARC_COMPILE_64)

solaris-gcc-sparc-debug:
	$(SOLARIS_GCC_SPARC_COMPILE_DEBUG)

solaris-sunw-x86:
	$(SOLARIS_SUNW_x86_COMPILE_32)

solaris-sunw-x86-64:
	$(SOLARIS_SUNW_x86_COMPILE_64)

clean:
	rm -f libhoard.*


