// SPDX-License-Identifier: MIT
//
// avr_atmega328p.cpp — TRUE baremetal keypad firmware for the ATmega328P.
//
// No Arduino. No RTOS. No AVR-libc: this file deliberately includes *none* of
// <avr/io.h>, <util/delay.h>, <util/atomic.h>, nor the ISR()/sei() macros. It
// talks straight to the silicon through memory-mapped registers, defined here
// from the datasheet, and uses inline assembly for the few things that have no
// C expression (the sei/cli instructions and the cycle-counting busy-wait). The
// only thing borrowed from the toolchain is the chip's reset/vector table
// (device bring-up), which the linker's startup object provides; our timer ISR
// is hooked by defining the raw vector symbol __vector_14.
//
// This is the unique selling point: portable, allocation-free driver logic in
// kp::Keypad, wired to bare registers with zero library abstraction.
//
//   * The five GpioHal hooks are thin wrappers over DDRx / PORTx / PINx.
//   * Timer0 (CTC, 1 ms tick) provides the monotonic millisecond clock that the
//     non-blocking debounce state machine needs.
//   * Each accepted key press is sent over USART0 (9600 8N1) and toggles the
//     on-board LED (PB5 / Arduino "D13") as a liveness heartbeat.
//
// Default wiring for a 4x4 matrix (avoiding PD0/PD1 = USART and PB5 = LED):
//
//        COL0  COL1  COL2  COL3
//        PD6   PD7   PB0   PB1
//   ROW0 PD2 ─┐
//   ROW1 PD3  ├─ 16 keys, diodeless matrix (rows = inputs w/ pull-ups)
//   ROW2 PD4  │
//   ROW3 PD5 ─┘
//
// Build with the AVR toolchain (see the repo README /
// cmake/avr-toolchain.cmake).

#include <stdint.h>  // freestanding only — fixed-width integer types

#include "keypad/keypad.hpp"

namespace {

// ---- Memory-mapped I/O layer ------------------------------------------------
// ATmega328P data-space addresses (datasheet "Register Summary"). We access the
// silicon directly rather than via <avr/io.h>'s SFR macros.
namespace reg {
constexpr uint16_t PINB = 0x23, DDRB = 0x24, PORTB = 0x25;
constexpr uint16_t PIND = 0x29, DDRD = 0x2A, PORTD = 0x2B;
constexpr uint16_t SREG = 0x5F;
constexpr uint16_t TCCR0A = 0x44, TCCR0B = 0x45, OCR0A = 0x47, TIMSK0 = 0x6E;
constexpr uint16_t UCSR0A = 0xC0, UCSR0B = 0xC1, UCSR0C = 0xC2;
constexpr uint16_t UBRR0L = 0xC4, UBRR0H = 0xC5, UDR0 = 0xC6;

/// Reference an 8-bit hardware register by absolute data-space address.
[[nodiscard]] inline volatile uint8_t& r8(uint16_t addr) noexcept {
    return *reinterpret_cast<volatile uint8_t*>(addr);
}

/// 8-bit mask with only bit @p n set, e.g. mask(3) == 0b0000'1000.
[[nodiscard]] constexpr uint8_t mask(uint8_t n) noexcept {
    return static_cast<uint8_t>(1u << n);
}

/// Read-modify-write: set the @p bits of register @p addr to 1.
inline void setBits(uint16_t addr, uint8_t bits) noexcept {
    r8(addr) = static_cast<uint8_t>(r8(addr) | bits);
}

/// Read-modify-write: clear the @p bits of register @p addr to 0.
inline void clearBits(uint16_t addr, uint8_t bits) noexcept {
    r8(addr) = static_cast<uint8_t>(r8(addr) & ~bits);
}
}  // namespace reg

// ---- CPU intrinsics (no library, raw instructions) --------------------------
inline void enableInterrupts() noexcept {
    asm volatile("sei" ::: "memory");
}

/// Busy-wait one millisecond, hand-written in inline assembly — no compiler
/// delay intrinsic and no library. The inner loop is exactly 4 CPU cycles per
/// iteration (`sbiw` = 2, `brne` = 2), so at F_CPU we iterate F_CPU/1000/4
/// times to burn ~1 ms. The "+w" constraint pins the counter to an upper
/// register pair (r24:r25) so `sbiw` is legal.
inline void delayOneMs() noexcept {
    uint16_t count = static_cast<uint16_t>(F_CPU / 1000UL / 4UL);
    asm volatile(
        "1: sbiw %0, 1 \n\t"  // 2 cycles: decrement 16-bit counter
        "   brne 1b    \n\t"  // 2 cycles: loop until it reaches zero
        : "+w"(count)
        :
        : "memory");
}

// ---- Logical pin table ------------------------------------------------------
// The HAL passes a single uint8_t "pin id"; we use it to index this table,
// which resolves to the concrete port register addresses and bit position.
struct PinMap {
    uint16_t ddr;
    uint16_t port;
    uint16_t pin;
    uint8_t bit;
};

constexpr PinMap kPins[] = {
    {reg::DDRD, reg::PORTD, reg::PIND, 2},  // 0: ROW0 = PD2
    {reg::DDRD, reg::PORTD, reg::PIND, 3},  // 1: ROW1 = PD3
    {reg::DDRD, reg::PORTD, reg::PIND, 4},  // 2: ROW2 = PD4
    {reg::DDRD, reg::PORTD, reg::PIND, 5},  // 3: ROW3 = PD5
    {reg::DDRD, reg::PORTD, reg::PIND, 6},  // 4: COL0 = PD6
    {reg::DDRD, reg::PORTD, reg::PIND, 7},  // 5: COL1 = PD7
    {reg::DDRB, reg::PORTB, reg::PINB, 0},  // 6: COL2 = PB0
    {reg::DDRB, reg::PORTB, reg::PINB, 1},  // 7: COL3 = PB1
};

// ---- GpioHal implementation (direct register access) ------------------------
void setPinOutput(uint8_t id) {
    const PinMap& p = kPins[id];
    reg::setBits(p.ddr, reg::mask(p.bit));  // DDRx bit = 1 -> output
}

void setPinInput(uint8_t id) {
    const PinMap& p = kPins[id];
    reg::clearBits(p.ddr, reg::mask(p.bit));  // DDRx bit = 0 -> input
    reg::setBits(p.port, reg::mask(p.bit));   // PORTx bit = 1 -> pull-up on
}

void writePin(uint8_t id, bool high) {
    const PinMap& p = kPins[id];
    if (high) {
        reg::setBits(p.port, reg::mask(p.bit));
    } else {
        reg::clearBits(p.port, reg::mask(p.bit));
    }
}

bool readPin(uint8_t id) {
    const PinMap& p = kPins[id];
    return (reg::r8(p.pin) & reg::mask(p.bit)) != 0;
}

void delayMs(uint32_t ms) {
    while (ms-- > 0) {
        delayOneMs();
    }
}

// ---- Timer0: 1 ms monotonic clock ------------------------------------------
volatile uint32_t g_millis = 0;

// Bit positions (datasheet).
constexpr uint8_t WGM01 = 1, CS01 = 1, CS00 = 0, OCIE0A = 1;

void clockInit() {
    reg::r8(reg::TCCR0A) = reg::mask(WGM01);  // CTC (clear on compare match)
    reg::r8(reg::OCR0A) =
        static_cast<uint8_t>(F_CPU / 64UL / 1000UL - 1UL);     // 1 ms
    reg::r8(reg::TCCR0B) = reg::mask(CS01) | reg::mask(CS00);  // /64
    reg::r8(reg::TIMSK0) = reg::mask(OCIE0A);  // compare-match A interrupt
}

uint32_t millis() {
    // A 32-bit read on an 8-bit core is not atomic, and the ISR writes
    // g_millis. Guard it by saving SREG, disabling interrupts, reading, then
    // restoring the I-flag — all without <util/atomic.h>.
    const uint8_t saved = reg::r8(reg::SREG);
    asm volatile("cli" ::: "memory");
    const uint32_t ms = g_millis;
    reg::r8(reg::SREG) = saved;  // restores the global interrupt-enable flag
    asm volatile("" ::: "memory");
    return ms;
}

// ---- USART0: 9600 8N1, transmit only ---------------------------------------
constexpr uint8_t TXEN0 = 3, UCSZ01 = 2, UCSZ00 = 1, UDRE0 = 5;

void usartInit() {
    const uint16_t ubrr = static_cast<uint16_t>(F_CPU / 16UL / 9600UL - 1UL);
    reg::r8(reg::UBRR0H) = static_cast<uint8_t>(ubrr >> 8);
    reg::r8(reg::UBRR0L) = static_cast<uint8_t>(ubrr);
    reg::r8(reg::UCSR0B) = reg::mask(TXEN0);  // enable transmitter
    reg::r8(reg::UCSR0C) = reg::mask(UCSZ01) |
                           reg::mask(UCSZ00);  // 8 data bits, 1 stop, no parity
}

void usartPut(char c) {
    while ((reg::r8(reg::UCSR0A) & reg::mask(UDRE0)) == 0) {  // wait: TX empty
    }
    reg::r8(reg::UDR0) = static_cast<uint8_t>(c);
}

// ---- On-board LED (PB5) -----------------------------------------------------
constexpr uint8_t PB5 = 5;
void ledInit() {
    reg::setBits(reg::DDRB, reg::mask(PB5));  // drive PB5 as output
}
void ledToggle() {
    reg::r8(reg::PINB) =
        reg::mask(PB5);  // writing 1 to PINx toggles the output
}

}  // namespace

// Timer0 compare-match A interrupt. On the ATmega328P this is vector 14; the
// chip's startup vector table (provided by the toolchain's device crt) jumps
// here. The `signal` attribute makes GCC emit a full register-saving ISR ending
// in `reti`; `used` keeps the symbol alive through LTO.
extern "C" void __vector_14() __attribute__((signal, used));
extern "C" void __vector_14() {
    ++g_millis;
}

int main() {
    // 4x4 keypad geometry. Stored in flash-resident static storage so it
    // outlives the Keypad instance, as the API requires.
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

    ledInit();
    usartInit();
    clockInit();
    keypad.init();
    enableInterrupts();  // raw `sei` -> Timer0 starts ticking millis()

    for (;;) {
        if (keypad.update(millis()) &&
            keypad.event() == kp::Keypad::Event::Pressed) {
            usartPut(keypad.key());
            ledToggle();
        }
    }
}
