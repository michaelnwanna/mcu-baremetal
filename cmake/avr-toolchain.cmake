# SPDX-License-Identifier: MIT
#
# CMake toolchain file for baremetal AVR (8-bit) targets, built with avr-gcc.
#
# Usage:
#   cmake -B build-avr -S . \
#         -DCMAKE_TOOLCHAIN_FILE=cmake/avr-toolchain.cmake \
#         -DAVR_MCU=atmega328p -DAVR_F_CPU=16000000 -G Ninja
#
# This produces freestanding firmware: no OS, no heap, no C++ exceptions/RTTI.

set(CMAKE_SYSTEM_NAME Generic)       # freestanding / baremetal (no OS)
set(CMAKE_SYSTEM_PROCESSOR avr)

# ---- Target selection (override on the command line) ------------------------
set(AVR_MCU   "atmega328p" CACHE STRING "Target AVR MCU passed to -mmcu")
set(AVR_F_CPU "16000000"   CACHE STRING "CPU clock in Hz, exported as F_CPU")

# ---- Toolchain binaries -----------------------------------------------------
find_program(AVR_CC  avr-gcc REQUIRED)
find_program(AVR_CXX avr-g++ REQUIRED)

set(CMAKE_C_COMPILER   "${AVR_CC}")
set(CMAKE_CXX_COMPILER "${AVR_CXX}")
set(CMAKE_ASM_COMPILER "${AVR_CC}")

# LTO objects in a static archive must be indexed by the gcc-ar/ranlib wrappers
# (they load the LTO plugin); the plain binutils ar/ranlib cannot read them.
find_program(AVR_GCC_AR     avr-gcc-ar)
find_program(AVR_GCC_RANLIB avr-gcc-ranlib)
if(AVR_GCC_AR)
  set(CMAKE_AR "${AVR_GCC_AR}")
endif()
if(AVR_GCC_RANLIB)
  set(CMAKE_RANLIB "${AVR_GCC_RANLIB}")
endif()

# The compiler cannot link a hosted executable (no _exit, no startup main loop),
# so probe it by building a static library instead.
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# ---- Common baremetal flags -------------------------------------------------
# -Os                  optimise for size (the currency of an 8-bit MCU)
# -ffunction-sections  + --gc-sections lets the linker drop unused code
# -fdata-sections      ditto for data
# -flto                whole-program optimisation across translation units
set(_avr_common "-mmcu=${AVR_MCU} -DF_CPU=${AVR_F_CPU}UL -Os -flto \
-ffunction-sections -fdata-sections -fno-common")

set(CMAKE_C_FLAGS_INIT   "${_avr_common}")

# C++ on a microcontroller: no exceptions, no RTTI, no thread-safe static guards.
set(CMAKE_CXX_FLAGS_INIT "${_avr_common} -fno-exceptions -fno-rtti \
-fno-threadsafe-statics")

set(CMAKE_EXE_LINKER_FLAGS_INIT "-mmcu=${AVR_MCU} -flto -Wl,--gc-sections")

# ---- Search behaviour -------------------------------------------------------
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
