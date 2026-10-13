# keypad

Portable, allocation-free **matrix keypad** driver for microcontrollers.

- **Baremetal-first**: depends only on the freestanding `<stdint.h>` — no heap, no
  exceptions, no RTTI, no C++ standard library. Compiles with `avr-gcc` (8-bit
  AVR), `arm-none-eabi-gcc` (ARM Cortex-M), and a desktop compiler for tests,
  all from the *same* source.
- Zero dynamic allocation; all state is bounded at compile time.
- Hardware-agnostic via an injected, ABI-stable function-pointer HAL.
- Any matrix geometry up to 8×8 (≤ 64 keys).
- Non-blocking, time-driven debounce **state machine** for real firmware, plus a
  blocking `getKey()` convenience for demos.

## API at a glance

```cpp
#include "keypad/keypad.hpp"

static constexpr uint8_t rowPins[4] = {2, 3, 4, 5};
static constexpr uint8_t colPins[4] = {6, 7, 8, 9};
static constexpr char    keymap[16] = {
    '1','2','3','A',
    '4','5','6','B',
    '7','8','9','C',
    '*','0','#','D',
};

kp::Keypad::Config cfg{rowPins, colPins, keymap, 4, 4, /*debounceMs*/20};
kp::Keypad::GpioHal hal{ setOut, setIn, write, read, delay };

kp::Keypad keypad(cfg, hal);
keypad.init();

// Non-blocking loop (recommended):
if (keypad.update(now_ms) && keypad.event() == kp::Keypad::Event::Pressed) {
    handle(keypad.key());
}
```

## Porting

Implement five thin wrappers around your platform's GPIO and pass them via
`GpioHal`. Three examples ship with the library:

- [`examples/avr_atmega328p.cpp`](examples/avr_atmega328p.cpp) — **true baremetal
  firmware** for the ATmega328P. It includes **no AVR-libc** at all: no
  `<avr/io.h>`, `<util/delay.h>`, `<util/atomic.h>`, nor the `ISR()`/`sei()`
  macros. Registers are memory-mapped from the datasheet, `sei`/`cli` and the
  millisecond busy-wait are inline `asm`, and the Timer0 1 ms tick is hooked by
  defining the raw interrupt vector (`__vector_14`). Direct silicon, zero library
  abstraction.
- [`examples/stm32f103_bluepill.cpp`](examples/stm32f103_bluepill.cpp) — the same
  driver on an **ARM Cortex-M3** (STM32F103), again with **no vendor HAL**.
  Because Cortex-M has no vendor startup, it also ships its own
  [vector table / reset handler](examples/stm32/startup_stm32f103.cpp) and
  [linker script](examples/stm32/stm32f103.ld). GPIO via CRL/CRH/BSRR, clock via
  SysTick, output over USART1.
- [`examples/basic_scan.cpp`](examples/basic_scan.cpp) — host-buildable stub with
  ESP-IDF / Arduino mapping notes, used by CI.

| HAL hook        | Meaning                                  |
| --------------- | ---------------------------------------- |
| `setPinOutput`  | configure pin as push-pull output        |
| `setPinInput`   | configure pin as input **with pull-up**  |
| `writePin`      | drive pin HIGH/LOW                       |
| `readPin`       | read pin level (true = HIGH)            |
| `delayMs`       | busy-wait (only used by `getKey()`)     |

## Building

### Prerequisites

| Build | Toolchain | Notes |
| ----- | --------- | ----- |
| Host (tests) | any C++17 compiler (`gcc`/`clang`/MSVC) + CMake ≥ 3.20 | |
| Baremetal AVR | `avr-gcc` (+ `avr-binutils`) | |
| Baremetal ARM | `arm-none-eabi-gcc` (+ `arm-none-eabi-binutils`) | |

> **Windows / PATH:** some installers (e.g. the *Arm GNU Toolchain*) do **not**
> add their `bin/` directory to `PATH`. If `arm-none-eabi-gcc` isn't found,
> either restart your shell after install or prepend it for the session, e.g.
> `export PATH="/c/Program Files (x86)/Arm GNU Toolchain arm-none-eabi/14.2 rel1/bin:$PATH"`
> (PowerShell: `$env:PATH = "C:\Program Files (x86)\Arm GNU Toolchain arm-none-eabi\14.2 rel1\bin;$env:PATH"`).

### Baremetal AVR (avr-gcc)

The repo ships a CMake toolchain file. From the repository root:

```sh
cmake -B build-avr -S . -G Ninja \
      -DCMAKE_TOOLCHAIN_FILE=cmake/avr-toolchain.cmake \
      -DAVR_MCU=atmega328p -DAVR_F_CPU=16000000
cmake --build build-avr
```

This produces `keypad_avr_atmega328p.elf`, a flashable `keypad_avr_atmega328p.hex`,
and prints the firmware size (a 4×4 build is ~1.8 KB flash / 4 B SRAM). Flash it
with avrdude, e.g.:

```sh
avrdude -c arduino -p atmega328p -P COM3 -b 115200 \
        -U flash:w:build-avr/libs/embedded/keypad/keypad_avr_atmega328p.hex:i
```

Target a different chip by overriding `-DAVR_MCU=` / `-DAVR_F_CPU=`.

### Baremetal ARM Cortex-M (arm-none-eabi-gcc)

```sh
cmake -B build-arm -S . -G Ninja \
      -DCMAKE_TOOLCHAIN_FILE=cmake/arm-toolchain.cmake \
      -DARM_CPU=cortex-m3
cmake --build build-arm
```

This produces `keypad_stm32f103.elf` + `.hex` for the STM32F103 "Blue Pill".
Flash it over SWD (ST-Link) or USART bootloader, e.g. with st-flash:

```sh
st-flash --format ihex write \
         build-arm/libs/embedded/keypad/keypad_stm32f103.hex
```

### Host (tests + example)

```sh
cmake -B build -S .
cmake --build build
ctest --test-dir build --output-on-failure
```

## Electrical model

Rows are inputs with pull-ups (idle HIGH); columns are outputs (idle HIGH). To
sample, a column is driven LOW and any pressed key on that column pulls its row
LOW. This is the standard diodeless matrix; for guaranteed N-key rollover, add
per-key diodes.

## Tests

Built with the top-level CMake project and run via CTest. The suite uses a
dependency-free mock matrix HAL — see [`tests/keypad_test.cpp`](tests/keypad_test.cpp).
