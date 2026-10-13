// SPDX-License-Identifier: MIT
//
// keypad.hpp — Portable, allocation-free matrix keypad driver.
//
// Design goals (production-level embedded C++):
//   * Zero dynamic allocation. All state is bounded at compile time.
//   * Hardware-agnostic via an injected, ABI-stable function-pointer HAL.
//   * Configurable matrix geometry (any R x C up to kMaxRows x kMaxCols).
//   * Two usage models:
//       - update(now_ms): non-blocking, time-driven debounce state machine
//         (the right tool for real firmware / cooperative schedulers).
//       - getKey():       blocking convenience for demos and simple sketches.
//
// The driver references caller-owned configuration arrays (pin maps + keymap)
// rather than copying them, so those arrays MUST outlive the Keypad instance.
// Use static / global storage for them.

#ifndef KP_KEYPAD_HPP
#define KP_KEYPAD_HPP

// Freestanding C header: <stdint.h> is guaranteed available even on baremetal
// toolchains that ship no C++ standard library (e.g. avr-gcc/AVR-libc), whereas
// the hosted <cstdint> is not. We therefore use the global-namespace fixed-width
// types so the driver compiles identically on a desktop and on an MCU.
#include <stdint.h>

namespace kp {

/// Matrix keypad scanner.
///
/// Electrical model (classic, diodeless matrix):
///   * Rows are inputs with internal pull-ups (idle HIGH).
///   * Columns are outputs (idle HIGH).
///   * To sample, a column is driven LOW; any pressed key on that column pulls
///     its row LOW, which we detect.
class Keypad {
public:
    /// Upper bounds on matrix geometry. They size the internal, heap-free
    /// debounce state (a single 64-bit bitmask), so kMaxRows * kMaxCols <= 64.
    static constexpr uint8_t kMaxRows = 8;
    static constexpr uint8_t kMaxCols = 8;
    static constexpr uint8_t kMaxKeys = kMaxRows * kMaxCols;

    /// Sentinel returned when no key is pressed.
    static constexpr char kNoKey = '\0';

    /// Hardware abstraction layer.
    ///
    /// Plain function pointers keep the ABI stable, add zero heap usage, and
    /// keep code size tiny. Provide thin wrappers around your platform's GPIO.
    struct GpioHal {
        /// Configure @p pin as a push-pull output.
        void (*setPinOutput)(uint8_t pin) = nullptr;
        /// Configure @p pin as an input with an internal pull-up.
        void (*setPinInput)(uint8_t pin) = nullptr;
        /// Drive @p pin HIGH (true) or LOW (false).
        void (*writePin)(uint8_t pin, bool high) = nullptr;
        /// Read @p pin; returns true for HIGH.
        bool (*readPin)(uint8_t pin) = nullptr;
        /// Busy-wait for @p ms milliseconds. Used only by the blocking getKey().
        void (*delayMs)(uint32_t ms) = nullptr;
    };

    /// Edge events emitted by the non-blocking update() state machine.
    enum class Event : uint8_t { None, Pressed, Released };

    /// Static driver configuration.
    ///
    /// @note rowPins, colPins and keymap are referenced, not copied. They must
    ///       outlive the Keypad. keymap is row-major with rows*cols entries:
    ///       keymap[row * cols + col].
    struct Config {
        const uint8_t* rowPins = nullptr;       ///< rows entries
        const uint8_t* colPins = nullptr;       ///< cols entries
        const char* keymap = nullptr;           ///< row-major, rows*cols entries
        uint8_t rows = 0;
        uint8_t cols = 0;
        uint16_t debounceMs = 20;               ///< stable window before commit
    };

    Keypad(const Config& config, const GpioHal& hal) noexcept;

    /// Validate the configuration and HAL wiring.
    /// @return true when the driver is safe to use.
    [[nodiscard]] bool valid() const noexcept;

    /// Set GPIO directions and idle levels. Call once after construction.
    void init() const noexcept;

    /// Immediate, un-debounced scan.
    /// @return the first detected key, or kNoKey if none is pressed.
    [[nodiscard]] char scan() const noexcept;

    /// Blocking, debounced single-key read.
    /// Spins until a key is pressed and released. Requires GpioHal::delayMs.
    /// Prefer update() in real firmware.
    [[nodiscard]] char getKey() const noexcept;

    /// Non-blocking debounce state machine.
    ///
    /// Call frequently from your main loop, passing a monotonically increasing
    /// millisecond timestamp. At most one key transition is reported per call;
    /// simultaneous transitions are emitted across successive calls.
    ///
    /// @param now_ms current time in milliseconds.
    /// @return true when a new event is available via event() / key().
    bool update(uint32_t now_ms) noexcept;

    /// Last event produced by update().
    [[nodiscard]] Event event() const noexcept { return _event; }
    /// Key associated with the last event(), or kNoKey.
    [[nodiscard]] char key() const noexcept { return _eventKey; }

private:
    [[nodiscard]] char keyAt(uint8_t row, uint8_t col) const noexcept;
    [[nodiscard]] uint64_t readMatrix() const noexcept;

    Config _cfg;
    GpioHal _hal;

    // Debounce state (bounded, heap-free).
    uint64_t _stable = 0;      ///< committed pressed-key bitmask
    uint64_t _candidate = 0;   ///< last raw sample under observation
    uint32_t _changedAtMs = 0; ///< when _candidate last changed

    Event _event = Event::None;
    char _eventKey = kNoKey;
};

}  // namespace kp

#endif  // KP_KEYPAD_HPP
