# Architecture & engineering standards

This document captures the *why* behind the structure so the firmware scales
without losing coherence as new drivers and target MCUs come online.

## Principles

1. **Zero-overhead abstractions.** Abstractions exist to manage complexity, not
   to add runtime cost. On embedded paths we prefer compile-time-bounded state
   and avoid the heap, exceptions, and RTTI.
2. **Inject dependencies at the boundary.** Hardware and OS services enter
   through small, explicit seams (e.g. the keypad `GpioHal`). This makes core
   logic portable and unit-testable on a host.
3. **Determinism.** Real-time code must have predictable timing and memory.
   Debounce, scheduling, and state machines are time-driven and non-blocking.
4. **Tests are part of the design.** If something is hard to test, the design is
   usually wrong. Tests are fast, deterministic, and dependency-free.
5. **No vendor lock-in.** Where it matters, drivers run true bare-metal: direct
   memory-mapped registers, raw interrupt vectors, and inline assembly for the
   few CPU instructions with no C expression — instead of vendor libraries
   (Arduino, AVR-libc, CMSIS).

## Why true bare-metal

Leaning on a vendor HAL or Arduino hides what the silicon is actually doing and
bloats the footprint. Programming to the datasheet — memory-mapped registers,
the raw vector table, inline `asm` for CPU instructions — keeps firmware tiny,
deterministic, and portable across toolchains, and demonstrates a real
understanding of the hardware. The `keypad` driver is the reference: the *same*
portable logic runs as a ~1.8 KB / 4 B-SRAM ATmega328P firmware with **zero
AVR-libc**, and as an STM32F103 (Cortex-M3) firmware that hand-writes even its
own startup vector table and linker script — **no CMSIS, no vendor HAL**.

## Driver map

| Driver  | Path                       | Status | Focus                              |
| ------- | -------------------------- | ------ | ---------------------------------- |
| keypad  | `libs/embedded/keypad/`    | active | Matrix keypad, non-blocking debounce |

Ports (same driver source, different board glue):

| Target     | Core         | Toolchain          | Notes                                   |
| ---------- | ------------ | ------------------ | --------------------------------------- |
| ATmega328P | AVR 8-bit    | `avr-gcc`          | toolchain-provided crt/vector table     |
| STM32F103  | Cortex-M3    | `arm-none-eabi-gcc`| **own** startup, vector table, linker script |
| host       | x86-64/arm64 | `gcc`/`clang`      | stub HAL for unit tests                 |

## Embedded HAL pattern

Drivers never call hardware directly. They accept a small struct of function
pointers (or, later, a policy type) that the integrator wires to their platform
(an MCU's registers, a vendor HAL, a simulator, or a test mock). Benefits:

- The same driver compiles for any MCU and for the host.
- Tests substitute a software model of the peripheral.
- The ABI stays stable and the footprint stays tiny.

The `keypad` driver is the reference implementation of this pattern — its AVR and
ARM Cortex-M ports share **identical** driver source, differing only in the thin
board file that implements the HAL hooks.

## Roadmap

GPIO/SPI/I²C abstractions, a non-blocking cooperative scheduler, lock-free ring
buffers, and additional input/display drivers — all sharing the HAL pattern and
the no-vendor-library discipline, building on the existing AVR and ARM Cortex-M
ports.

Each new component lands with: a CMake target, a README, tests, and CI coverage.
