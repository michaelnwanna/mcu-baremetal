// SPDX-License-Identifier: MIT
//
// basic_scan.cpp — Minimal, host-buildable example.
//
// This compiles and runs on a desktop using a no-op stub HAL so CI can verify
// the example stays in sync with the API. The inline comments show exactly
// where to drop in your MCU's GPIO calls (STM32 HAL, ESP-IDF, Arduino, etc.).

#include <cstdio>

#include "keypad/keypad.hpp"

namespace {

// ---- Replace the bodies below with your platform's GPIO + timing ------------
void setPinOutput(uint8_t /*pin*/) {
    // e.g. pinMode(pin, OUTPUT);  /  HAL_GPIO config push-pull
}
void setPinInput(uint8_t /*pin*/) {
    // e.g. pinMode(pin, INPUT_PULLUP);  /  HAL_GPIO input + pull-up
}
void writePin(uint8_t /*pin*/, bool /*high*/) {
    // e.g. digitalWrite(pin, high);
}
bool readPin(uint8_t /*pin*/) {
    return true;  // stub: report idle HIGH (no key)
}
void delayMs(uint32_t /*ms*/) {
    // e.g. delay(ms);  /  HAL_Delay(ms);
}

}  // namespace

int main() {
    // Pin assignments are just identifiers your HAL understands.
    static constexpr uint8_t rowPins[4] = {2, 3, 4, 5};
    static constexpr uint8_t colPins[4] = {6, 7, 8, 9};
    static constexpr char keymap[16] = {
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
    if (!keypad.valid()) {
        std::printf("keypad: invalid configuration\n");
        return 1;
    }
    keypad.init();

    std::printf("keypad: initialised. Non-blocking event loop pattern:\n\n");
    std::printf("  std::uint32_t now = millis();\n");
    std::printf("  if (keypad.update(now)) {\n");
    std::printf("    if (keypad.event() == kp::Keypad::Event::Pressed)\n");
    std::printf("      handle(keypad.key());\n");
    std::printf("  }\n");

    // Demonstrate one non-blocking tick with the stub HAL (no key pressed).
    if (keypad.update(0) && keypad.event() == kp::Keypad::Event::Pressed) {
        std::printf("pressed: %c\n", keypad.key());
    }
    return 0;
}
