# CMake toolchain file for PS2 EE processor
# Copyright (C) 2020 David Leiter
# based on the toolchain from Mathias Lafeldt <misfire@debugon.org>

set(PS2DEV "$ENV{PS2DEV}")
set(PS2SDK "${PS2DEV}/ps2sdk")

if (NOT PS2DEV)
    MESSAGE(FATAL_ERROR "PS2DEV env var not set.")
endif (NOT PS2DEV)

set(EE_PREFIX mips64r5900el-ps2-elf-)
set(EE_CC ${EE_PREFIX}gcc)
set(EE_CXX ${EE_PREFIX}g++)
set(EE_AS ${EE_PREFIX}as)
set(EE_LD ${EE_PREFIX}ld)
set(EE_AR ${EE_PREFIX}ar)
set(EE_OBJCOPY ${EE_PREFIX}objcopy)
set(EE_STRIP ${EE_PREFIX}strip)
set(EE_ADDR2LINE ${EE_PREFIX}addr2line)
set(EE_RANLIB ${EE_PREFIX}ranlib)

set(IOP_PREFIX mipsel-ps2-irx-)
set(IOP_CC ${IOP_PREFIX}gcc)
set(IOP_AS ${IOP_PREFIX}as)
set(IOP_LD ${IOP_PREFIX}ld)
set(IOP_AR ${IOP_PREFIX}ar)
set(IOP_OBJCOPY ${IOP_PREFIX}objcopy)
set(IOP_STRIP ${IOP_PREFIX}strip)
set(IOP_ADDR2LINE ${IOP_PREFIX}addr2line)
set(IOP_RANLIB ${IOP_PREFIX}ranlib)

# Helpers to make easy the use of lkernel-nopatch and newlib-nano
set(NODEFAULTLIBS 0)
set(LKERNEL -lkernel)
if(KERNEL_NOPATCH)
    set(NODEFAULTLIBS 1)
    set(LKERNEL -lkernel-nopatch)
endif()

set(LIBC -lc)
set(LIBM -lm)

if(NEWLIB_NANO)
    set(NODEFAULTLIBS 1)
    set(LIBC -lc_nano)
    set(LIBM -lm_nano)
endif()

set(EXTRA_LDFLAGS)
if(NODEFAULTLIBS EQUAL 1)
    set(EXTRA_LDFLAGS "-nodefaultlibs ${LIBM} -Wl,--start-group ${LIBC} -lps2sdkc ${LKERNEL} -Wl,--end-group -lgcc")
endif()

# Include directories
set(EE_INCS "-I${PS2SDK}/ee/include -I${PS2SDK}/common/include -I. ${EE_INCS}")

# Optimization compiler flags
set(EE_OPTFLAGS -O2)

# Warning compiler flags
set(EE_WARNFLAGS -Wall)

# C compiler flags
set(EE_CFLAGS "-D_EE -G0 ${EE_OPTFLAGS} ${EE_WARNFLAGS} ${EE_CFLAGS}")

# C++ compiler flags
set(EE_CXXFLAGS "-D_EE -G0 ${EE_OPTFLAGS} ${EE_WARNFLAGS} ${EE_CXXFLAGS}")

# Linker flags
set(EE_LDFLAGS "-L${PS2SDK}/ee/lib -Wl,-zmax-page-size=128 ${EXTRA_LDFLAGS} ${EE_LDFLAGS}")

# Assembler flags
set(EE_ASFLAGS "-G0 ${EE_ASFLAGS}")

# Default link file
if (NOT EE_LINKFILE)
    set(EE_LINKFILE ${PS2SDK}/ee/startup/linkfile)
endif()

# Externally defined variables: EE_BIN, EE_OBJS, EE_LIB

# These macros can be used to simplify certain build rules.
set(EE_C_COMPILE "${EE_CC} ${EE_CFLAGS} ${EE_INCS}")
set(EE_CXX_COMPILE "${EE_CXX} ${EE_CXXFLAGS} ${EE_INCS}")

set(CMAKE_STATIC_LIBRARY_PREFIX "lib")
set(CMAKE_STATIC_LIBRARY_PREFIX_C "lib")
set(CMAKE_STATIC_LIBRARY_PREFIX_CXX "lib")

set(CMAKE_STATIC_LIBRARY_SUFFIX ".a")
set(CMAKE_STATIC_LIBRARY_SUFFIX_C ".a")
set(CMAKE_STATIC_LIBRARY_SUFFIX_CXX ".a")

set(CMAKE_EXECUTABLE_SUFFIX ".elf")
set(CMAKE_EXECUTABLE_SUFFIX_C ".elf")
set(CMAKE_EXECUTABLE_SUFFIX_CXX ".elf")

set(CMAKE_FIND_LIBRARY_PREFIXES "lib")
set(CMAKE_FIND_LIBRARY_PREFIXES_C "lib")
set(CMAKE_FIND_LIBRARY_PREFIXES_CXX "lib")

set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
set(CMAKE_FIND_LIBRARY_SUFFIXES_C ".a")
set(CMAKE_FIND_LIBRARY_SUFFIXES_CXX ".a")

set(CMAKE_C_LINK_EXECUTABLE
        "${EE_CC} ${EE_CFLAGS} ${EE_INCS} <OBJECTS> -o <TARGET> <LINK_LIBRARIES>"
        )
set(CMAKE_CXX_LINK_EXECUTABLE
        "<EE_CXX> <FLAGS> <EE_CXXFLAGS> <EE_INCS> <OBJECTS> -o <TARGET> <LINK_LIBRARIES>"
        )

# build EE_LIB
set(CMAKE_C_CREATE_STATIC_LIBRARY
        "<CMAKE_AR> cru <TARGET> <LINK_FLAGS> <OBJECTS>"
        )
set(CMAKE_CXX_CREATE_STATIC_LIBRARY
        "<CMAKE_AR> cru <TARGET> <LINK_FLAGS> <OBJECTS>"
        )

set(PLATFORM_IS_PS2 TRUE)
