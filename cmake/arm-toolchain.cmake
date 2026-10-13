# SPDX-License-Identifier: MIT
#
# CMake toolchain file for baremetal ARM Cortex-M targets, built with the
# arm-none-eabi GNU toolchain.
#
# Usage:
#   cmake -B build-arm -S . \
#         -DCMAKE_TOOLCHAIN_FILE=cmake/arm-toolchain.cmake \
#         -DARM_CPU=cortex-m3 -G Ninja
#
# Like the AVR toolchain, this produces freestanding firmware: no OS, no heap,
# no C++ exceptions/RTTI. Unlike AVR, ARM Cortex-M has no vendor-supplied
# startup object, so the example provides its own vector table + linker script.

set(CMAKE_SYSTEM_NAME Generic)        # freestanding / baremetal (no OS)
set(CMAKE_SYSTEM_PROCESSOR arm)

# ---- Target selection (override on the command line) ------------------------
set(ARM_CPU "cortex-m3" CACHE STRING "Cortex-M core passed to -mcpu")

# ---- Toolchain binaries -----------------------------------------------------
find_program(ARM_CC  arm-none-eabi-gcc REQUIRED)
find_program(ARM_CXX arm-none-eabi-g++ REQUIRED)

set(CMAKE_C_COMPILER   "${ARM_CC}")
set(CMAKE_CXX_COMPILER "${ARM_CXX}")
set(CMAKE_ASM_COMPILER "${ARM_CC}")

# LTO objects in a static archive must be indexed by the gcc-ar/ranlib wrappers
# (they load the LTO plugin); the plain binutils ar/ranlib cannot read them.
find_program(ARM_GCC_AR     arm-none-eabi-gcc-ar)
find_program(ARM_GCC_RANLIB arm-none-eabi-gcc-ranlib)
if(ARM_GCC_AR)
  set(CMAKE_AR "${ARM_GCC_AR}")
endif()
if(ARM_GCC_RANLIB)
  set(CMAKE_RANLIB "${ARM_GCC_RANLIB}")
endif()

# The compiler cannot link a hosted executable (no startup, no syscalls), so
# probe it by building a static library instead.
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# ---- Common baremetal flags -------------------------------------------------
# -mthumb              Cortex-M only executes the Thumb instruction set
# -Os                  optimise for size
# -ffreestanding       no hosted runtime: main() is an ordinary function and
#                      the compiler assumes no standard library environment
# -ffunction-sections  + --gc-sections lets the linker drop unused code
# -fdata-sections      ditto for data
set(_arm_common "-mcpu=${ARM_CPU} -mthumb -Os -ffreestanding \
-ffunction-sections -fdata-sections -fno-common")

set(CMAKE_C_FLAGS_INIT "${_arm_common}")

# C++ on a microcontroller: no exceptions, no RTTI, no thread-safe static guards.
set(CMAKE_CXX_FLAGS_INIT "${_arm_common} -fno-exceptions -fno-rtti \
-fno-threadsafe-statics")

# We supply our own startup (-nostartfiles) and our own vector table. nosys.specs
# stubs out the few libc syscalls that might be pulled in. The actual linker
# script is attached per-firmware target (kp_add_arm_firmware).
set(CMAKE_EXE_LINKER_FLAGS_INIT "-mcpu=${ARM_CPU} -mthumb -nostartfiles \
--specs=nosys.specs -Wl,--gc-sections")

# ---- Search behaviour -------------------------------------------------------
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
