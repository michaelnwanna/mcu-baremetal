// SPDX-License-Identifier: MIT
//
// stm32f103_bluepill.cpp — TRUE baremetal keypad firmware for the STM32F103
// ("Blue Pill", Cortex-M3). The portability companion to avr_atmega328p.cpp:
// the *exact same* kp::Keypad driver, wired to a completely different silicon
// with nothing but datasheet register addresses.
//
// No HAL/LL, no CubeMX, no RTOS, no libc startup: bring-up (vector table, .data
// copy, .bss zero) is in startup_stm32f103.cpp; the memory map is in
// stm32f103.ld. This file is just the board glue: GPIO, a SysTick millisecond
// clock, and USART1 for output.
//
//   * The GpioHal hooks read/write GPIOB through CRL/CRH/IDR/BSRR/BRR.
//   * SysTick (1 ms) provides the monotonic clock the debounce machine needs.
//   * Each accepted key press is sent over USART1 (PA9, 9600 8N1) and toggles
//     the on-board LED (PC13, active-low) as a liveness heartbeat.
//
// Default wiring for a 4x4 matrix, all on GPIOB (rows = inputs w/ pull-ups,
// cols = outputs; avoids PB3/PB4 = JTAG and PB2 = BOOT1):
//
//        COL0  COL1  COL2  COL3
//        PB9   PB10  PB11  PB12
//   ROW0 PB5 ─┐
//   ROW1 PB6  ├─ 16 keys, diodeless matrix
//   ROW2 PB7  │
//   ROW3 PB8 ─┘
//
// Runs from the 8 MHz HSI (reset default); no PLL/flash-latency setup needed.
// Build with the ARM toolchain (see the repo README / cmake/arm-toolchain.cmake).

#include <stdint.h>  // freestanding only — fixed-width integer types

#include "keypad/keypad.hpp"

namespace {

// ---- Memory-mapped I/O layer ------------------------------------------------
// STM32F103 peripheral addresses (reference manual RM0008). We touch the
// silicon directly rather than through ST's HAL/LL headers.
namespace reg {
constexpr uint32_t kRccBase = 0x40021000;
constexpr uint32_t kRccApb2Enr = kRccBase + 0x18;

constexpr uint32_t kGpioA = 0x40010800;
constexpr uint32_t kGpioB = 0x40010C00;
constexpr uint32_t kGpioC = 0x40011000;
constexpr uint32_t kGpioCrl = 0x00, kGpioCrh = 0x04;
constexpr uint32_t kGpioIdr = 0x08, kGpioBsrr = 0x10, kGpioBrr = 0x14;

constexpr uint32_t kUsart1 = 0x40013800;
constexpr uint32_t kUsartSr = 0x00, kUsartDr = 0x04;
constexpr uint32_t kUsartBrr = 0x08, kUsartCr1 = 0x0C;

constexpr uint32_t kSysTickCsr = 0xE000E010;
constexpr uint32_t kSysTickRvr = 0xE000E014;
constexpr uint32_t kSysTickCvr = 0xE000E018;

/// Reference a 32-bit hardware register by absolute address.
[[nodiscard]] inline volatile uint32_t& r32(uint32_t addr) noexcept {
    return *reinterpret_cast<volatile uint32_t*>(addr);
}

/// 32-bit mask with only bit @p n set.
[[nodiscard]] constexpr uint32_t mask(uint32_t n) noexcept { return 1u << n; }

/// Read-modify-write: set the @p bits of register @p addr to 1.
inline void setBits(uint32_t addr, uint32_t bits) noexcept {
    r32(addr) = r32(addr) | bits;
}
}  // namespace reg

constexpr uint32_t kHsiHz = 8'000'000;  // reset-default core clock

// ---- Logical pin table ------------------------------------------------------
// The HAL passes a single uint8_t "pin id"; we index this table to resolve the
// concrete GPIO port base and bit position. All pins live on GPIOB here.
struct PinMap {
    uint32_t gpio;  ///< port base address
    uint8_t bit;    ///< pin number 0..15
};

constexpr PinMap kPins[] = {
    {reg::kGpioB, 5},   // 0: ROW0 = PB5
    {reg::kGpioB, 6},   // 1: ROW1 = PB6
    {reg::kGpioB, 7},   // 2: ROW2 = PB7
    {reg::kGpioB, 8},   // 3: ROW3 = PB8
    {reg::kGpioB, 9},   // 4: COL0 = PB9
    {reg::kGpioB, 10},  // 5: COL1 = PB10
    {reg::kGpioB, 11},  // 6: COL2 = PB11
    {reg::kGpioB, 12},  // 7: COL3 = PB12
};

// ---- GPIO pin configuration -------------------------------------------------
// On the F1, each pin is configured by a 4-bit CNF[1:0]+MODE[1:0] field, split
// across CRL (pins 0..7) and CRH (pins 8..15).
constexpr uint8_t kCfgOutputPushPull2MHz = 0x2;  // MODE=10, CNF=00
constexpr uint8_t kCfgInputPullUpDown = 0x8;     // MODE=00, CNF=10

void configPin(uint32_t gpio, uint8_t bit, uint8_t cfg4) {
    const uint32_t cr = (bit < 8) ? gpio + reg::kGpioCrl : gpio + reg::kGpioCrh;
    const uint32_t shift = static_cast<uint32_t>(bit & 7u) * 4u;
    uint32_t v = reg::r32(cr);
    v &= ~(0xFu << shift);                      // clear the 4-bit field
    v |= static_cast<uint32_t>(cfg4) << shift;  // write the new config
    reg::r32(cr) = v;
}

// ---- GpioHal implementation (direct register access) ------------------------
void setPinOutput(uint8_t id) {
    const PinMap& p = kPins[id];
    configPin(p.gpio, p.bit, kCfgOutputPushPull2MHz);
}

void setPinInput(uint8_t id) {
    const PinMap& p = kPins[id];
    configPin(p.gpio, p.bit, kCfgInputPullUpDown);
    reg::r32(p.gpio + reg::kGpioBsrr) = reg::mask(p.bit);  // ODR=1 -> pull-up
}

void writePin(uint8_t id, bool high) {
    const PinMap& p = kPins[id];
    // BSRR sets a bit, BRR clears it — atomic, no read-modify-write needed.
    const uint32_t target = high ? p.gpio + reg::kGpioBsrr : p.gpio + reg::kGpioBrr;
    reg::r32(target) = reg::mask(p.bit);
}

bool readPin(uint8_t id) {
    const PinMap& p = kPins[id];
    return (reg::r32(p.gpio + reg::kGpioIdr) & reg::mask(p.bit)) != 0;
}

// ---- SysTick: 1 ms monotonic clock ------------------------------------------
volatile uint32_t g_millis = 0;

constexpr uint32_t kSysTickEnable = 0;     // CSR bit 0
constexpr uint32_t kSysTickTickInt = 1;    // CSR bit 1
constexpr uint32_t kSysTickClkSource = 2;  // CSR bit 2 (1 = core clock)

void clockInit() {
    reg::r32(reg::kSysTickRvr) = kHsiHz / 1000u - 1u;  // 1 ms reload
    reg::r32(reg::kSysTickCvr) = 0;                     // clear current value
    reg::r32(reg::kSysTickCsr) = reg::mask(kSysTickEnable) |
                                 reg::mask(kSysTickTickInt) |
                                 reg::mask(kSysTickClkSource);
}

uint32_t millis() {
    // A 32-bit, word-aligned load is atomic on Cortex-M, so unlike the 8-bit AVR
    // build we need no interrupt-masking critical section here.
    return g_millis;
}

void delayMs(uint32_t ms) {
    const uint32_t start = millis();
    while (millis() - start < ms) {
    }
}

// ---- USART1: 9600 8N1, transmit only (PA9 = TX) -----------------------------
constexpr uint32_t kUsartCr1Te = 3;    // transmitter enable
constexpr uint32_t kUsartCr1Ue = 13;   // USART enable
constexpr uint32_t kUsartSrTxe = 7;    // TX data register empty
constexpr uint8_t kCfgAltPushPull50MHz = 0xB;  // MODE=11, CNF=10 (alt-fn out)

void usartInit() {
    configPin(reg::kGpioA, 9, kCfgAltPushPull50MHz);  // PA9 -> USART1_TX
    reg::r32(reg::kUsart1 + reg::kUsartBrr) = kHsiHz / 9600u;  // 8 MHz / 9600
    reg::r32(reg::kUsart1 + reg::kUsartCr1) =
        reg::mask(kUsartCr1Ue) | reg::mask(kUsartCr1Te);
}

void usartPut(char c) {
    const uint32_t sr = reg::kUsart1 + reg::kUsartSr;
    while ((reg::r32(sr) & reg::mask(kUsartSrTxe)) == 0) {  // wait: TX empty
    }
    reg::r32(reg::kUsart1 + reg::kUsartDr) = static_cast<uint32_t>(c) & 0xFFu;
}

// ---- On-board LED (PC13, active-low) ----------------------------------------
constexpr uint8_t kLedPin = 13;
void ledInit() { configPin(reg::kGpioC, kLedPin, kCfgOutputPushPull2MHz); }
void ledToggle() {
    // Toggle by reading ODR and writing the inverted bit through BSRR/BRR.
    const bool on = (reg::r32(reg::kGpioC + reg::kGpioIdr) & reg::mask(kLedPin)) == 0;
    const uint32_t target =
        on ? reg::kGpioC + reg::kGpioBsrr : reg::kGpioC + reg::kGpioBrr;
    reg::r32(target) = reg::mask(kLedPin);
}

// ---- Peripheral clocks ------------------------------------------------------
constexpr uint32_t kRccAfioEn = 0;     // APB2ENR bit 0
constexpr uint32_t kRccIopaEn = 2;     // GPIOA
constexpr uint32_t kRccIopbEn = 3;     // GPIOB
constexpr uint32_t kRccIopcEn = 4;     // GPIOC
constexpr uint32_t kRccUsart1En = 14;  // USART1

void enablePeripheralClocks() {
    reg::setBits(reg::kRccApb2Enr,
                 reg::mask(kRccAfioEn) | reg::mask(kRccIopaEn) |
                     reg::mask(kRccIopbEn) | reg::mask(kRccIopcEn) |
                     reg::mask(kRccUsart1En));
}

}  // namespace

// SysTick exception (vector 15). Overrides the weak default in the startup file.
extern "C" void SysTick_Handler() { ++g_millis; }

int main() {
    // 4x4 keypad geometry, in static storage so it outlives the Keypad instance.
    static const uint8_t rowPins[4] = {0, 1, 2, 3};
    static const uint8_t colPins[4] = {4, 5, 6, 7};
    static const char keymap[16] = {
        '1', '2', '3', 'A',  //
        '4', '5', '6', 'B',  //
        '7', '8', '9', 'C',  //
        '*', '0', '#', 'D',  //
    };

    kp::Keypad::Config cfg;
    cfg.rowPins = rowPins;
    cfg.colPins = colPins;
    cfg.keymap = keymap;
    cfg.rows = 4;
    cfg.cols = 4;
    cfg.debounceMs = 20;

    kp::Keypad::GpioHal hal;
    hal.setPinOutput = &setPinOutput;
    hal.setPinInput = &setPinInput;
    hal.writePin = &writePin;
    hal.readPin = &readPin;
    hal.delayMs = &delayMs;

    kp::Keypad keypad(cfg, hal);

    enablePeripheralClocks();  // must precede any GPIO/USART register access
    ledInit();
    usartInit();
    clockInit();
    keypad.init();

    for (;;) {
        if (keypad.update(millis()) &&
            keypad.event() == kp::Keypad::Event::Pressed) {
            usartPut(keypad.key());
            ledToggle();
        }
    }
}
