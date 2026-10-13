# SPDX-License-Identifier: MIT
#
# Helpers for turning a CMake executable target into flashable ARM Cortex-M
# firmware.
#
#   kp_add_arm_firmware(<target> LINKER_SCRIPT <path>)
#
# Attaches the linker script, renames the artefact to <target>.elf, emits an
# Intel-HEX image for flashing, and prints a size breakdown (text/data/bss).

find_program(ARM_OBJCOPY arm-none-eabi-objcopy)
find_program(ARM_SIZE    arm-none-eabi-size)

function(kp_add_arm_firmware target)
  cmake_parse_arguments(ARG "" "LINKER_SCRIPT" "" ${ARGN})

  set_target_properties(${target} PROPERTIES SUFFIX ".elf")

  if(ARG_LINKER_SCRIPT)
    target_link_options(${target} PRIVATE "-T${ARG_LINKER_SCRIPT}")
    # Relink when the linker script changes.
    set_target_properties(${target} PROPERTIES LINK_DEPENDS ${ARG_LINKER_SCRIPT})
  endif()

  if(ARM_OBJCOPY)
    add_custom_command(TARGET ${target} POST_BUILD
      COMMAND ${ARM_OBJCOPY} -O ihex
              $<TARGET_FILE:${target}>
              $<TARGET_FILE_DIR:${target}>/${target}.hex
      COMMENT "Generating ${target}.hex (Intel HEX, flashable image)"
      VERBATIM)
  endif()

  if(ARM_SIZE)
    add_custom_command(TARGET ${target} POST_BUILD
      COMMAND ${ARM_SIZE} -B $<TARGET_FILE:${target}>
      COMMENT "Firmware size for ${target}"
      VERBATIM)
  endif()
endfunction()
