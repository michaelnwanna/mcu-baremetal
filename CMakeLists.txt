# SPDX-License-Identifier: MIT
cmake_minimum_required(VERSION 3.20)

project(mcu-baremetal
  VERSION 0.1.0
  DESCRIPTION "Bare-metal microcontroller firmware & drivers in modern C++"
  LANGUAGES CXX
)

# ---- Global defaults --------------------------------------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Emit compile_commands.json for clangd / the VS Code C/C++ extension.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

# Honor BUILD_TESTING only when this is the top-level project.
option(KP_BUILD_TESTING "Build tests for this repository" ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CompilerWarnings)
include(AvrFirmware)
include(ArmFirmware)

if(KP_BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING)
  enable_testing()
endif()

# ---- Libraries --------------------------------------------------------------
# Embedded / baremetal libraries. Each is independently buildable and ships a
# host test build plus a real on-device example.
add_subdirectory(libs/embedded/keypad)
