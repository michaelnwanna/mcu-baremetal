# mcu-baremetal

> Production-grade **bare-metal microcontroller** firmware in modern C++ — no
> Arduino, no RTOS, and (where it counts) **no vendor libraries**. Just portable,
> allocation-free driver logic wired straight to the silicon.

[![CI](https://github.com/michaelnwanna/mcu-baremetal/actions/workflows/ci.yml/badge.svg)](../../actions/workflows/ci.yml)
![C++17](https://img.shields.io/badge/C%2B%2B-17-blue)
![AVR](https://img.shields.io/badge/target-AVR%20(avr--gcc)-orange)
![ARM](https://img.shields.io/badge/target-ARM%20Cortex--M%20(arm--none--eabi)-teal)
![License: MIT](https://img.shields.io/badge/License-MIT-green)

This repository is held to a **production-level** bar: zero-overhead
abstractions, hardware injected at the boundary, deterministic and
allocation-free code paths, a fast dependency-free test suite, and CI that gates
every change on build + test + formatting — for both the host and real MCU
targets (AVR **and** ARM Cortex-M).

## New to bare-metal? Start here 👋

If you've blinked an LED with Arduino, you already know enough to follow this
repo — you're just about to see what `digitalWrite()` *actually does* underneath.

**"Bare-metal"** simply means your code talks to the chip **directly**, with no
framework (Arduino), no operating system, and no vendor library in between. You
write to the chip's hardware *registers* — special memory addresses that control
real pins, timers, and serial ports — using values straight from the chip's
datasheet.

Why bother, when Arduino already works?

- 🪶 **Tiny & fast.** This whole keypad firmware is **~1.8 KB** and uses **4
  bytes of RAM**. No hidden framework overhead.
- 🔍 **You see everything.** Nothing happens by magic; every line maps to a
  hardware action you can point to in the datasheet.
- 🎓 **It's the real skill.** Understanding registers, interrupts, and memory is
  what separates "I can wire a sketch" from "I can write firmware."
- 🔁 **It's portable.** The same driver here runs on an 8-bit AVR *and* a 32-bit
  ARM chip (more on that below).

### Arduino → bare-metal, side by side

The *idea* is identical; only the wording changes. Here's "set pin high":

```cpp
// Arduino                          // Bare-metal (this repo, AVR)
digitalWrite(13, HIGH);            PORTB |= (1 << 5);   // pin 13 = PB5
```

A quick translation table for the concepts you already know:

| You know it in Arduino as… | Bare-metal calls it…                | What it really is                          |
| -------------------------- | ----------------------------------- | ------------------------------------------ |
| `pinMode(p, OUTPUT)`       | writing the **DDRx** register       | a bit that picks input vs output           |
| `digitalWrite(p, HIGH)`    | writing the **PORTx** register      | a bit that drives the pin's voltage        |
| `digitalRead(p)`           | reading the **PINx** register       | a bit that reflects the pin's voltage      |
| `delay(ms)`                | a counted busy-loop / hardware timer| the CPU literally waiting                  |
| `millis()`                 | a **timer interrupt** counting ms   | hardware ticking a counter for you         |
| `attachInterrupt()`        | defining an **interrupt vector**    | a function the chip jumps to on an event   |
| the Arduino core library   | the **datasheet**                   | the chip's official "what each bit does"   |

Don't worry if a few terms are new — every example in this repo is **heavily
commented**, explaining each register write as it happens. A great first read is
the fully-annotated AVR keypad example:
[`avr_atmega328p.cpp`](libs/embedded/keypad/examples/avr_atmega328p.cpp).

> 🌱 **Brand new to this?** Start with the companion repo
> **[mcu-blink](https://github.com/michaelnwanna/mcu-blink)** — the bare-metal
> "Hello, World!" that blinks an LED in ~20 lines with no libraries at all. It's
> the gentlest possible on-ramp; this repo is where you go next.

## Why this repo is different

Most "embedded" demos lean on Arduino or vendor HALs. The reference firmware here
is **true bare-metal**: the [`keypad` driver](libs/embedded/keypad/) ships an
ATmega328P example that includes **no AVR-libc at all** —

- registers are **memory-mapped from the datasheet** (no `<avr/io.h>`) — we read
  and write the chip's control bits ourselves,
- `sei`/`cli` (turning interrupts on/off) are written as **inline assembly** —
  one raw CPU instruction each,
- the millisecond clock is hooked by defining the **raw interrupt vector**
  (`__vector_14`) — the address the chip jumps to on a timer tick — instead of
  Arduino's `ISR()` macro,
- delays are a **hand-written cycle-counting busy-wait in inline assembly**, not
  `<util/delay.h>` (and not even a compiler delay intrinsic).

The result is a 4×4 keypad firmware that fits in **~1.8 KB of flash and 4 bytes
of SRAM**, with the *same* driver source unit-tested on a desktop.

## Portable by design: same driver, two architectures

The driver logic carries **zero** hardware dependencies, so the identical
`kp::Keypad` source drives two completely different MCUs — each with its own
bare-metal board file and **no vendor library**:

| Target | Core | Board file | Bring-up |
| --- | --- | --- | --- |
| ATmega328P (Arduino Uno/Nano) | AVR 8-bit | [`avr_atmega328p.cpp`](libs/embedded/keypad/examples/avr_atmega328p.cpp) | toolchain crt vector table |
| STM32F103 ("Blue Pill") | ARM Cortex-M3 | [`stm32f103_bluepill.cpp`](libs/embedded/keypad/examples/stm32f103_bluepill.cpp) | **our own** [startup](libs/embedded/keypad/examples/stm32/startup_stm32f103.cpp) + [linker script](libs/embedded/keypad/examples/stm32/stm32f103.ld) |

The STM32 port goes a step further than AVR: ARM Cortex-M has no vendor-supplied
startup, so this repo hand-writes the **vector table, reset handler (.data copy /
.bss zero), and memory map** too. Different silicon (CRL/CRH/BSRR vs DDRx/PORTx,
SysTick vs Timer0), the *same* `kp::Keypad`.

## Featured: the `keypad` driver

A portable, allocation-free **matrix keypad** driver with a non-blocking
debounce state machine and an ABI-stable function-pointer HAL.

- Zero dynamic allocation; all state bounded at compile time.
- Hardware-agnostic — port to any MCU by implementing five tiny GPIO hooks.
- Non-blocking, time-driven debounce (the right model for real firmware), plus a
  blocking `getKey()` for demos.
- Builds three ways from one source: **bare-metal AVR firmware**, **bare-metal
  ARM Cortex-M firmware**, and a **host test build**.

→ [`libs/embedded/keypad/`](libs/embedded/keypad/README.md)

## Repository layout

```text
.
├── libs/
│   └── embedded/
│       └── keypad/          # Matrix keypad driver (reference implementation)
│           ├── include/     # Public, self-contained header (freestanding)
│           ├── src/         # Portable driver logic
│           ├── examples/    # avr_atmega328p.cpp + stm32f103_bluepill.cpp
│           │   └── stm32/   #   STM32 startup (vector table) + linker script
│           └── tests/       # Dependency-free unit tests
├── cmake/                   # AVR + ARM toolchain files, firmware & warning helpers
├── docs/                    # Architecture & engineering standards
└── .github/workflows/       # CI: host build + test, AVR & ARM firmware builds
```

## Build & test

### Bare-metal AVR firmware (avr-gcc)

```bash
cmake -B build-avr -S . -G Ninja \
      -DCMAKE_TOOLCHAIN_FILE=cmake/avr-toolchain.cmake \
      -DAVR_MCU=atmega328p -DAVR_F_CPU=16000000
cmake --build build-avr
```

Produces a `.elf`, a flashable `.hex`, and a size report. Target another chip by
overriding `-DAVR_MCU=` / `-DAVR_F_CPU=`.

### Bare-metal ARM Cortex-M firmware (arm-none-eabi-gcc)

```bash
cmake -B build-arm -S . -G Ninja \
      -DCMAKE_TOOLCHAIN_FILE=cmake/arm-toolchain.cmake \
      -DARM_CPU=cortex-m3
cmake --build build-arm
```

Builds the STM32F103 "Blue Pill" firmware — our own startup/vector table and
linker script, no vendor HAL — into a `.elf` + `.hex` with a size report.

### Host build + tests

```bash
cmake -B build -S . -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
```

## Engineering standards

- **C++17**, warnings-as-signal (`-Wall -Wextra -Wpedantic -Wshadow
  -Wconversion …`).
- **Freestanding**: the driver depends only on `<stdint.h>` — it compiles even
  on toolchains that ship no C++ standard library.
- **No heap, no exceptions, no RTTI** on device paths; bounded compile-time
  state.
- **Hardware injected at the boundary**, so logic is unit-testable on a host.
- **Tests are mandatory** and run on every push/PR; the AVR and ARM firmware are
  built in CI too.
- **Formatting enforced** via `clang-format` (Google base, see `.clang-format`).

See [`docs/architecture.md`](docs/architecture.md) for the full rationale and the
embedded roadmap.

## Glossary (for newcomers)

| Term | Plain-English meaning |
| ---- | --------------------- |
| **Register** | A special memory address wired to hardware. Writing a bit flips a real pin, starts a timer, sends a byte, etc. |
| **Datasheet** | The chip maker's manual listing every register and what each bit does. The "source of truth" we program against. |
| **GPIO** | General-Purpose Input/Output — the chip's pins you can read or drive. |
| **Interrupt** | A hardware event (timer tick, pin change) that pauses your code to run a small handler, then resumes. |
| **Vector table** | A list at the start of memory telling the chip *which function* to jump to for each interrupt/reset. |
| **HAL** | Hardware Abstraction Layer — a thin seam (here, 5 function pointers) so the same logic runs on any chip. |
| **Debounce** | Filtering out the electrical "chatter" a mechanical key makes so one press = one event. |
| **Freestanding** | C++ with no operating system or standard library — only the core language plus `<stdint.h>`. |
| **Flash / SRAM** | Flash = non-volatile program storage; SRAM = small fast volatile working memory. |
| **Toolchain** | The compiler + linker + tools that turn your C++ into a chip-specific binary (e.g. `avr-gcc`). |

## Roadmap

GPIO/SPI/I²C abstractions, a non-blocking cooperative scheduler, lock-free ring
buffers, and additional input/display drivers — all sharing the HAL pattern and
the no-vendor-library discipline, building on the existing AVR and ARM Cortex-M
ports.

## License

MIT — see [`LICENSE`](LICENSE).
