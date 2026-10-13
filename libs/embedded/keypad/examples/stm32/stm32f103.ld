/* SPDX-License-Identifier: MIT
 *
 * stm32f103.ld — Minimal linker script for the STM32F103C8 ("Blue Pill").
 *
 * Cortex-M3, 64 KB flash @ 0x0800_0000, 20 KB SRAM @ 0x2000_0000.
 *
 * This script is deliberately small and self-contained: it defines the memory
 * map, places the vector table first in flash, and exports the symbols the
 * reset handler in startup_stm32f103.cpp uses to copy .data and zero .bss.
 */

MEMORY
{
    FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 64K
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 20K
}

/* Top of stack = end of RAM (the stack grows downward on Cortex-M). */
_estack = ORIGIN(RAM) + LENGTH(RAM);

ENTRY(Reset_Handler)

SECTIONS
{
    /* Vector table + code + read-only data, all in flash. */
    .text :
    {
        KEEP(*(.isr_vector))    /* vector table must come first */
        *(.text)
        *(.text*)
        *(.rodata)
        *(.rodata*)
        . = ALIGN(4);
        _etext = .;
    } > FLASH

    /* Source address in flash of the initialised-data image. */
    _sidata = LOADADDR(.data);

    /* Initialised variables: live in RAM, copied from flash at startup. */
    .data :
    {
        . = ALIGN(4);
        _sdata = .;
        *(.data)
        *(.data*)
        . = ALIGN(4);
        _edata = .;
    } > RAM AT > FLASH

    /* Zero-initialised variables: reserved in RAM, cleared at startup. */
    .bss :
    {
        . = ALIGN(4);
        _sbss = .;
        *(.bss)
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
    } > RAM

    /DISCARD/ : { *(.ARM.exidx*) }
}
