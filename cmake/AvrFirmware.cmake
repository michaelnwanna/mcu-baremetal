# SPDX-License-Identifier: MIT
#
# Helpers for turning a CMake executable target into flashable AVR firmware.
#
#   kp_add_avr_firmware(<target>)
#
# Renames the artefact to <target>.elf, emits an Intel-HEX image for flashing,
# and prints a size breakdown (text/data/bss vs. the MCU's flash & SRAM).

find_program(AVR_OBJCOPY avr-objcopy)
find_program(AVR_SIZE    avr-size)

function(kp_add_avr_firmware target)
  set_target_properties(${target} PROPERTIES SUFFIX ".elf")

  if(AVR_OBJCOPY)
    add_custom_command(TARGET ${target} POST_BUILD
      COMMAND ${AVR_OBJCOPY} -O ihex -R .eeprom
              $<TARGET_FILE:${target}>
              $<TARGET_FILE_DIR:${target}>/${target}.hex
      COMMENT "Generating ${target}.hex (Intel HEX, flashable image)"
      VERBATIM)
  endif()

  if(AVR_SIZE)
    # Note: the AVR-patched "--format=avr" (flash/RAM %) is only in some
    # toolchains; the Berkeley format is supported everywhere.
    add_custom_command(TARGET ${target} POST_BUILD
      COMMAND ${AVR_SIZE} -B $<TARGET_FILE:${target}>
      COMMENT "Firmware size for ${AVR_MCU}"
      VERBATIM)
  endif()
endfunction()
