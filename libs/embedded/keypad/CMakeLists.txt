# SPDX-License-Identifier: MIT
# keypad library + example + tests

add_library(keypad src/keypad.cpp)
add_library(kp::keypad ALIAS keypad)

target_include_directories(keypad
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_compile_features(keypad PUBLIC cxx_std_17)
kp_set_warnings(keypad)

if(CMAKE_CROSSCOMPILING)
  # --- Baremetal targets (selected by the active toolchain file) -------------
  # Real register-level firmware. Each produces a .elf, a flashable .hex and a
  # size report. The library itself is the same code as the host build, proving
  # the driver is genuinely hardware-agnostic.
  if(CMAKE_SYSTEM_PROCESSOR STREQUAL "avr")
    # ATmega328P (AVR, 8-bit) — see cmake/avr-toolchain.cmake.
    add_executable(keypad_avr_atmega328p examples/avr_atmega328p.cpp)
    target_link_libraries(keypad_avr_atmega328p PRIVATE kp::keypad)
    kp_set_warnings(keypad_avr_atmega328p)
    kp_add_avr_firmware(keypad_avr_atmega328p)
  elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "arm")
    # STM32F103 "Blue Pill" (ARM Cortex-M3) — see cmake/arm-toolchain.cmake.
    # Includes our own startup/vector table; the linker script sets the memory
    # map. Same kp::Keypad driver as the AVR build above.
    add_executable(keypad_stm32f103
      examples/stm32f103_bluepill.cpp
      examples/stm32/startup_stm32f103.cpp)
    target_link_libraries(keypad_stm32f103 PRIVATE kp::keypad)
    kp_set_warnings(keypad_stm32f103)
    kp_add_arm_firmware(keypad_stm32f103
      LINKER_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/examples/stm32/stm32f103.ld)
  endif()
else()
  # --- Host build: example + unit tests --------------------------------------
  # Buildable on a desktop with a stub HAL so CI verifies the example and the
  # portable logic (debounce state machine) without any hardware.
  add_executable(keypad_basic_scan examples/basic_scan.cpp)
  target_link_libraries(keypad_basic_scan PRIVATE kp::keypad)
  kp_set_warnings(keypad_basic_scan)

  if(KP_BUILD_TESTING)
    add_subdirectory(tests)
  endif()
endif()
