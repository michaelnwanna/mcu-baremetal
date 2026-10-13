// SPDX-License-Identifier: MIT
//
// startup_stm32f103.cpp — Bare-metal C++ startup for the STM32F103 (Cortex-M3).
//
// On AVR the toolchain hands you a ready-made reset/vector table via its device
// crt. ARM Cortex-M does not: the *first* thing in flash must be a vector table
// whose first word is the initial stack pointer and whose second word is the
// reset handler. So we own the whole bring-up here — and that, rather than being
// a chore, is exactly the bare-metal story this repo is about.
//
// Reset_Handler does the C runtime's job by hand:
//   1. copy initialised data (.data) from its flash image into RAM,
//   2. zero the .bss section,
//   3. call main().
//
// The linker script stm32f103.ld provides the boundary symbols we reference.

#include <stdint.h>

// ---- Symbols provided by the linker script ----------------------------------
extern uint32_t _sidata;  // flash address of the .data initialiser image
extern uint32_t _sdata;   // start of .data in RAM
extern uint32_t _edata;   // end of .data in RAM
extern uint32_t _sbss;    // start of .bss in RAM
extern uint32_t _ebss;    // end of .bss in RAM
extern uint32_t _estack;  // top of stack (end of RAM)

int main();

// ---- Reset handler: the hand-written C runtime ------------------------------
extern "C" [[noreturn]] void Reset_Handler() {
    // 1. Copy .data (initialised globals) from flash into RAM.
    const uint32_t* src = &_sidata;
    for (uint32_t* dst = &_sdata; dst < &_edata;) {
        *dst++ = *src++;
    }

    // 2. Zero-initialise .bss.
    for (uint32_t* dst = &_sbss; dst < &_ebss;) {
        *dst++ = 0;
    }

    // 3. Hand control to the application.
    // ISO C++ forbids a program from calling main() itself (-Wpedantic), but a
    // freestanding reset handler is exactly the one place that legitimately must
    // — there is no C runtime to do it for us. Suppress the warning locally.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
    main();
#pragma GCC diagnostic pop

    // main() should never return on an MCU; trap if it does.
    for (;;) {
    }
}

// A default handler for any interrupt we do not explicitly implement: just spin
// so a stray exception is observable under a debugger rather than silent.
extern "C" void Default_Handler() {
    for (;;) {
    }
}

// Core exception handlers. Declared weak + aliased to Default_Handler so a
// strong definition elsewhere (e.g. SysTick_Handler in the example) overrides
// them, while everything else falls back to the trap loop.
#define KP_WEAK_ALIAS __attribute__((weak, alias("Default_Handler")))

extern "C" void NMI_Handler() KP_WEAK_ALIAS;
extern "C" void HardFault_Handler() KP_WEAK_ALIAS;
extern "C" void MemManage_Handler() KP_WEAK_ALIAS;
extern "C" void BusFault_Handler() KP_WEAK_ALIAS;
extern "C" void UsageFault_Handler() KP_WEAK_ALIAS;
extern "C" void SVC_Handler() KP_WEAK_ALIAS;
extern "C" void DebugMon_Handler() KP_WEAK_ALIAS;
extern "C" void PendSV_Handler() KP_WEAK_ALIAS;
extern "C" void SysTick_Handler() KP_WEAK_ALIAS;

// ---- The vector table --------------------------------------------------------
// Placed at the very start of flash by the linker script's KEEP(*(.isr_vector)).
// Entry 0 is the initial stack pointer; entry 1 is the reset handler; entries
// 2..15 are the Cortex-M core exceptions. We store everything as uintptr_t so
// the stack-pointer value and the function pointers can share one array.
using Handler = void (*)();

__attribute__((section(".isr_vector"), used))
const uintptr_t g_vectorTable[] = {
    reinterpret_cast<uintptr_t>(&_estack),                  //  0: initial SP
    reinterpret_cast<uintptr_t>(Reset_Handler),             //  1: reset
    reinterpret_cast<uintptr_t>(NMI_Handler),               //  2
    reinterpret_cast<uintptr_t>(HardFault_Handler),         //  3
    reinterpret_cast<uintptr_t>(MemManage_Handler),         //  4
    reinterpret_cast<uintptr_t>(BusFault_Handler),          //  5
    reinterpret_cast<uintptr_t>(UsageFault_Handler),        //  6
    0, 0, 0, 0,                                             //  7-10: reserved
    reinterpret_cast<uintptr_t>(SVC_Handler),               // 11
    reinterpret_cast<uintptr_t>(DebugMon_Handler),          // 12
    0,                                                      // 13: reserved
    reinterpret_cast<uintptr_t>(PendSV_Handler),            // 14
    reinterpret_cast<uintptr_t>(SysTick_Handler),           // 15: 1 ms tick
};
