# Commands to compile Hoard for various targets.
# Run make (with no arguments) to see the complete target list.

CPPFLAGS = -O3 -Wall -Wextra -Wshadow -Wconversion

# Prefix for installations (Unix / Mac)

PREFIX ?= /usr/lib

# Compute platform (OS and architecture) and build accordingly.

ifeq ($(OS),Windows_NT)
all: windows
else
    UNAME_S := $(shell uname -s)
    UNAME_P := $(shell uname -p)
    ifeq ($(UNAME_S),SunOS)
      all: $(UNAME_S)-sunw-$(UNAME_P)
      install: $(UNAME_S)-sunw-$(UNAME_P)-install
    else
      all: $(UNAME_S)-gcc-$(UNAME_P)
      install: $(UNAME_S)-gcc-$(UNAME_P)-install
    endif
endif

help:
	@echo To build Hoard, specify the desired build target:
	@echo -------------------------------------------------
	@echo debian
	@echo freebsd
	@echo Linux-gcc-arm
	@echo Linux-gcc-x86
	@echo Linux-gcc-x86_64
	@echo Darwin-gcc-i386
	@echo SunOS-sunw-sparc
	@echo SunOS-sunw-i386
	@echo SunOS-gcc-sparc
	@echo SunOS-gcc-i386
	@echo generic-gcc
	@echo windows

.PHONY: Darwin-gcc-i386 debian freebsd Linux-gcc-x86 Linux-gcc-x86-debug SunOS-sunw-sparc SunOS-sunw-i386 SunOS-gcc-sparc generic-gcc Linux-gcc-arm Linux-gcc-x86_64 Linux-gcc-unknown windows windows-debug clean test

#
# Source files
#

MAIN_SRC  = source/libhoard.cpp
UNIX_SRC  = $(MAIN_SRC) source/unixtls.cpp
SUNW_SRC  = $(UNIX_SRC) Heap-Layers/wrappers/wrapper.cpp
GNU_SRC   = $(UNIX_SRC) Heap-Layers/wrappers/gnuwrapper.cpp
MACOS_SRC = $(MAIN_SRC) Heap-Layers/wrappers/macwrapper.cpp source/mactls.cpp

#
# All dependencies.
#

DEPS = $(MACOS_SRC) $(UNIX_SRC) source/libhoard.cpp

#
# Include directories
#

INCLUDES = -I. -Iinclude -Iinclude/util -Iinclude/hoard -Iinclude/superblocks -IHeap-Layers

WIN_INCLUDES = /I. /Iinclude /Iinclude/util /Iinclude/hoard /Iinclude/superblocks /IHeap-Layers

#
# Compile commands for individual targets.
#

FREEBSD_COMPILE = clang++ -std=c++11 -g $(CPPFLAGS) -fPIC -DNDEBUG  $(INCLUDES) -D_REENTRANT=1 -shared $(SUNW_SRC) -Bsymbolic -o libhoard.so -lpthread

DEBIAN_COMPILE = g++-4.8 -std=c++11 -g -O3 -fPIC -DNDEBUG -I. -Iinclude -Iinclude/util -Iinclude/hoard -Iinclude/superblocks -IHeap-Layers -D_REENTRANT=1 -shared source/libhoard.cpp source/unixtls.cpp Heap-Layers/wrappers/wrapper.cpp -Bsymbolic -o libhoard.so -lpthread -lstdc++ -ldl

MACOS_COMPILE = clang++ -std=c++11 -fno-builtin-malloc -ftemplate-depth=1024 -arch i386 -arch x86_64 -pipe -g $(CPPFLAGS) -DNDEBUG $(INCLUDES) -D_REENTRANT=1 -compatibility_version 1 -current_version 1 -dynamiclib -D'CUSTOM_PREFIX(x)=xx\#\#x' $(MACOS_SRC) -o libhoard.dylib -ldl -lpthread 

MACOS_COMPILE_DEBUG = clang++ -std=c++11 -fno-builtin-malloc -ftemplate-depth=1024 -arch i386 -arch x86_64 -pipe -g -O0 -Wall $(INCLUDES) -D_REENTRANT=1 -compatibility_version 1 -current_version 1 -dynamiclib $(MACOS_SRC) -o libhoard.dylib -ldl -lpthread

LINUX_GCC_ARM_COMPILE = arm-Linux-gnueabihf-g++ -std=c++11 $(CPPFLAGS) -g -W -Wconversion -Wall -I/usr/include/nptl -fno-builtin-malloc -pipe -fPIC -DNDEBUG  $(INCLUDES) -D_REENTRANT=1 -shared   $(GNU_SRC) -Bsymbolic -o libhoard.so -ldl -lpthread

LINUX_GCC_x86_COMPILE = g++ -std=c++11 -m32 $(CPPFLAGS) -I/usr/include/nptl -ffast-math -g -fno-builtin-malloc -malign-double -pipe -finline-limit=20000 -finline-functions  -DNDEBUG  $(INCLUDES) -D_REENTRANT=1 -shared  $(GNU_SRC) -Bsymbolic -o libhoard.so -ldl -lpthread

LINUX_GCC_x86_64_COMPILE = $(CXX) -std=c++11 $(CPPFLAGS) -g -W -Wconversion -Wall -I/usr/include/nptl -fno-builtin-malloc -pipe -fPIC -DNDEBUG  $(INCLUDES) -D_REENTRANT=1 -shared   $(GNU_SRC) -Bsymbolic -o libhoard.so -ldl -lpthread

LINUX_GCC_UNKNOWN_COMPILE = $(CXX) -std=c++11 $(CPPFLAGS) -g -W -Wconversion -Wall -I/usr/include/nptl -fno-builtin-malloc -pipe -fPIC -DNDEBUG  $(INCLUDES) -D_REENTRANT=1 -shared   $(GNU_SRC) -Bsymbolic -o libhoard.so -ldl -lpthread


# LINUX_GCC_x86_64_COMPILE = $(CXX) -std=c++11 $(CPPFLAGS) -g -W -Wconversion -Wall -I/usr/include/nptl -fno-builtin-malloc -pipe -fPIC -finline-limit=20000 -finline-functions -DNDEBUG  $(INCLUDES) -D_REENTRANT=1 -shared   $(GNU_SRC) -Bsymbolic -o libhoard.so -ldl -lpthread

LINUX_GCC_x86_64_COMPILE_DEBUG = g++ -std=c++11 $(CPPFLAGS) -g -W -Wconversion -Wall -I/usr/include/nptl -fno-builtin-malloc -pipe -fPIC $(INCLUDES) -D_REENTRANT=1 -shared $(GNU_SRC) -Bsymbolic -o libhoard.so -ldl -lpthread

LINUX_GCC_x86_COMPILE_STATIC = g++ -std=c++11 $(CPPFLAGS) -g -I/usr/include/nptl -static -pipe -finline-limit=20000 -fno-builtin-malloc -finline-functions  -DNDEBUG  $(INCLUDES) -D_REENTRANT=1  -c $(GNU_SRC) ; ar cr libhoard.a libhoard.o

LINUX_GCC_x86_64_COMPILE_STATIC = g++ -std=c++11 $(CPPFLAGS) -g -W -Wconversion -Wall -I/usr/include/nptl -static -pipe -fPIC -fno-builtin-malloc -finline-limit=20000 -finline-functions  -DNDEBUG  $(INCLUDES) -D_REENTRANT=1 -shared -c $(GNU_SRC) -Bsymbolic ; ar cr libhoard.a libhoard.o

LINUX_GCC_x86_COMPILE_DEBUG = g++ -m32 -std=c++11 $(CPPFLAGS) -fPIC -fno-inline -I/usr/include/nptl -fno-builtin-malloc -g -pipe $(INCLUDES) -D_REENTRANT=1  -shared $(GNU_SRC) -Bsymbolic -o libhoard.so -ldl -lpthread

SUNOS_SUNW_SPARC_COMPILE_32_DEBUG = CC -dalign -xbuiltin=%all -fast -mt -g -xildoff -xthreadvar=dynamic -L/usr/lib/lwp -R/usr/lib/lwp -DNDEBUG $(INCLUDES) -D_REENTRANT=1 -G -PIC $(SUNW_SRC) Heap-Layers/wrappers/arch-specific/sparc-interchange.il -o libhoard_32.so -lthread -ldl -lCrun

SUNOS_SUNW_SPARC_COMPILE_32 = CC -dalign -xbuiltin=%all -fast -xO5 -DNDEBUG -mt -g -xildoff -xthreadvar=dynamic -L/usr/lib/lwp -R/usr/lib/lwp $(INCLUDES) -D_REENTRANT=1 -G -PIC $(SUNW_SRC) Heap-Layers/wrappers/arch-specific/sparc-interchange.il -o libhoard_32.so -lthread -ldl -lCrun 

SUNOS_SUNW_SPARC_COMPILE_64 = CC -g -xcode=pic13 -m64 -mt -fast -dalign -xbuiltin=%all -xO5 -xildoff -xthreadvar=dynamic -L/usr/lib/lwp -R/usr/lib/lwp -DNDEBUG $(INCLUDES) -D_REENTRANT=1 -G -PIC $(SUNW_SRC) Heap-Layers/wrappers/arch-specific/sparc-interchange.il -o libhoard_64.so -lthread -ldl -lCrun

SUNOS_SUNW_x86_COMPILE_32 = CC -std=c++11 -g -fns -fsimple=2 -ftrap=%none -xbuiltin=%all -mt -xO5 -xildoff -xthreadvar=dynamic -L/usr/lib/lwp -R/usr/lib/lwp -DNDEBUG $(INCLUDES) -D_REENTRANT=1 -G -PIC $(SUNW_SRC) Heap-Layers/wrappers/arch-specific/x86-interchange.il -o libhoard_32.so -lthread -ldl -lCrun

SUNOS_SUNW_x86_COMPILE_32_DEBUG = CC -std=c++11 -mt -g -xildoff -xthreadvar=dynamic -L/usr/lib/lwp -R/usr/lib/lwp $(INCLUDES) -D_REENTRANT=1 -G -PIC $(SUNW_SRC) Heap-Layers/wrappers/arch-specific/x86-interchange.il -o libhoard_32.so -lthread -ldl -lCrun

SUNOS_SUNW_x86_COMPILE_64 = CC -std=c++11 -g -m64 -fns -fsimple=2 -ftrap=%none -xbuiltin=%all -xO5 -xildoff -xthreadvar=dynamic -L/usr/lib/lwp -R/usr/lib/lwp -DNDEBUG $(INCLUDES) -D_REENTRANT=1 -G -PIC $(SUNW_SRC) Heap-Layers/wrappers/arch-specific/x86_64-interchange.il -o libhoard_64.so -lthread -ldl -lCrun

SUNOS_GCC_SPARC_COMPILE_32 = g++ -std=c++11 -g -fno-builtin-malloc -nostartfiles -pipe -DNDEBUG -mcpu=ultrasparc -m32 $(CPPFLAGS) -finline-limit=20000 -fPIC -fkeep-inline-functions -finline-functions -ffast-math $(INCLUDES) -D_REENTRANT=1 -shared $(SUNW_SRC) -lthread -lpthread -ldl -o libhoard_32.so

SUNOS_GCC_SPARC_COMPILE_64 = g++ -std=c++11 -g -fno-builtin-malloc -nostartfiles -pipe -DNDEBUG -mcpu=ultrasparc -m64 $(CPPFLAGS) -finline-limit=20000 -fPIC -fkeep-inline-functions -finline-functions -ffast-math $(INCLUDES) -D_REENTRANT=1 -shared $(SUNW_SRC) -lthread -lpthread -ldl -o libhoard_64.so

SUNOS_GCC_I386_COMPILE_32 = g++ -std=c++11 -g -fno-builtin-malloc -nostartfiles -pipe -DNDEBUG -m32 $(CPPFLAGS) -finline-limit=20000 -fPIC -fkeep-inline-functions -finline-functions -ffast-math $(INCLUDES) -D_REENTRANT=1 -shared $(SUNW_SRC) -lthread -ldl -o libhoard_32.so

SUNOS_GCC_I386_COMPILE_64 = g++ -std=c++11 -g -fno-builtin-malloc -nostartfiles -pipe -DNDEBUG -m64 $(CPPFLAGS) -finline-limit=20000 -fPIC -fkeep-inline-functions -finline-functions -ffast-math $(INCLUDES) -D_REENTRANT=1 -shared $(SUNW_SRC) -lthread -ldl -o libhoard_64.so

SUNOS_GCC_SPARC_COMPILE_DEBUG = g++ -std=c++11 -g -fno-builtin-malloc -nostartfiles -pipe -mcpu=ultrasparc -g -fPIC $(INCLUDES) -D_REENTRANT=1 -shared $(SUNW_SRC) -lthread -lpthread -ldl -o libhoard.so

GENERIC_GCC_COMPILE = g++ -std=c++11 -I/usr/include/nptl -fno-builtin-malloc -pipe -g $(CPPFLAGS) -finline-limit=20000 -finline-functions  -DNDEBUG  $(INCLUDES) -D_REENTRANT=1 -shared $(GNU_SRC) -Bsymbolic -o libhoard.so -ldl -lpthread

WIN_DEFINES = /D "NDEBUG" /D "_WINDOWS" /D "_WINDLL" /D "_WINRT_DLL" /D "_UNICODE" /D "UNICODE"
WIN_DEBUG_DEFINES = /D "_WINDOWS" /D "_WINDLL" /D "_WINRT_DLL" /D "_UNICODE" /D "UNICODE"

WIN_FLAGS         = /Zi /Ox /MD /nologo /W1 /WX- /Ox /Oi /Oy- /Gm- /EHsc /MD /GS /Gy /Zc:wchar_t /Zc:forScope /Gd /errorReport:queue
WIN_DEBUG_FLAGS   = /Zi /MD /nologo /W1 /WX- /Gm- /EHsc /MD /GS /Gy /Zc:wchar_t /Zc:forScope /Gd /errorReport:queue

windows: $(DEPS)
	cl $(WIN_INCLUDES) $(WIN_DEFINES) $(WIN_FLAGS) "source\libhoard.cpp" "Heap-Layers\wrappers\winwrapper.cpp" "source\wintls.cpp" /GL /link /DLL /subsystem:console /OUT:libhoard.dll
	cl $(WIN_INCLUDES) $(WIN_DEFINES) $(WIN_FLAGS) /c "source\uselibhoard.cpp"

windows-debug: $(DEPS)
	cl /analyze /analyze:stacksize131072 $(WIN_INCLUDES) $(WIN_DEBUG_DEFINES) $(WIN_DEBUG_FLAGS) "source\libhoard.cpp" "Heap-Layers\wrappers\winwrapper.cpp" "source\wintls.cpp" /GL /link /DLL /subsystem:console /OUT:libhoard.dll
	cl $(WIN_INCLUDES) $(WIN_DEBUG_DEFINES) $(WIN_DEBUG_FLAGS) /c "source\uselibhoard.cpp"

Darwin-gcc-i386:
	$(MACOS_COMPILE)

Darwin-gcc-i386-install: Darwin-gcc-i386
	cp libhoard.dylib $(PREFIX)

generic-gcc:
	$(GENERIC_GCC_COMPILE)

generic-gcc-install: generic-gcc
	cp libhoard.so $(PREFIX)

Linux-gcc-arm:
	$(LINUX_GCC_ARM_COMPILE)

Linux-gcc-arm-install: Linux-gcc-arm
	cp libhoard.so $(PREFIX)

Linux-gcc-x86:
	$(LINUX_GCC_x86_COMPILE)

Linux-gcc-x86-install: Linux-gcc-x86
	cp libhoard.so $(PREFIX)

Linux-gcc-x86_64:
	$(LINUX_GCC_x86_64_COMPILE)

Linux-gcc-x86_64-install: Linux-gcc-x86_64
	cp libhoard.so $(PREFIX)

Linux-gcc-unknown:
	$(LINUX_GCC_UNKNOWN_COMPILE)

Linux-gcc-unknown-install: Linux-gcc-unknown
	cp libhoard.so $(PREFIX)

SunOS-sunw-sparc:
	$(SUNOS_SUNW_SPARC_COMPILE_32)
	$(SUNOS_SUNW_SPARC_COMPILE_64)

SunOS-sunw-sparc-install: SunOS-sunw-sparc
	cp libhoard_32.so $(PREFIX)
	cp libhoard_64.so $(PREFIX)

SunOS-gcc-sparc:
	$(SUNOS_GCC_SPARC_COMPILE_32)
	$(SUNOS_GCC_SPARC_COMPILE_64)

SunOS-gcc-sparc-install: SunOS-gcc-sparc
	cp libhoard_32.so $(PREFIX)
	cp libhoard_64.so $(PREFIX)

SunOS-gcc-i386:
	$(SUNOS_GCC_I386_COMPILE_32)
	$(SUNOS_GCC_I386_COMPILE_64)

SunOS-gcc-i386-install: SunOS-gcc-sparc
	cp libhoard_32.so $(PREFIX)
	cp libhoard_64.so $(PREFIX)

SunOS-sunw-i386:
	$(SUNOS_SUNW_x86_COMPILE_32)
	$(SUNOS_SUNW_x86_COMPILE_64)

SunOS-sunw-i386-install: SunOS-sunw-i386
	cp libhoard_32.so $(PREFIX)
	cp libhoard_64.so $(PREFIX)

#
#
#

Linux-gcc-x86-static:
	$(LINUX_GCC_x86_COMPILE_STATIC)

Linux-gcc-x86-debug:
	$(LINUX_GCC_x86_COMPILE_DEBUG)

SunOS-gcc-sparc-debug:
	$(SUNOS_GCC_SPARC_COMPILE_DEBUG)

Darwin-gcc-i386-debug:
	$(MACOS_COMPILE_DEBUG)

Linux-gcc-x86_64-static:
	$(LINUX_GCC_x86_64_COMPILE_STATIC)

Linux-gcc-x86_64-debug:
	$(LINUX_GCC_x86_64_COMPILE_DEBUG)

SunOS-sunw-sparc-debug:
	$(SUNOS_SUNW_SPARC_COMPILE_32_DEBUG)

freebsd:
	$(FREEBSD_COMPILE)

debian:
	$(DEBIAN_COMPILE)

clean:
	rm -rf libhoard.*


