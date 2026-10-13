// SPDX-License-Identifier: MIT
#include "keypad/keypad.hpp"

namespace kp {

Keypad::Keypad(const Config& config, const GpioHal& hal) noexcept
    : _cfg(config), _hal(hal) {}

bool Keypad::valid() const noexcept {
    if (_cfg.rowPins == nullptr || _cfg.colPins == nullptr ||
        _cfg.keymap == nullptr) {
        return false;
    }
    if (_cfg.rows == 0 || _cfg.cols == 0 || _cfg.rows > kMaxRows ||
        _cfg.cols > kMaxCols) {
        return false;
    }
    if (static_cast<uint16_t>(_cfg.rows) * _cfg.cols > kMaxKeys) {
        return false;
    }
    return _hal.setPinOutput != nullptr && _hal.setPinInput != nullptr &&
           _hal.writePin != nullptr && _hal.readPin != nullptr;
}

void Keypad::init() const noexcept {
    if (!valid()) {
        return;
    }
    // Rows: input + pull-up (idle HIGH).
    for (uint8_t r = 0; r < _cfg.rows; ++r) {
        _hal.setPinInput(_cfg.rowPins[r]);
    }
    // Columns: output, idle HIGH.
    for (uint8_t c = 0; c < _cfg.cols; ++c) {
        _hal.setPinOutput(_cfg.colPins[c]);
        _hal.writePin(_cfg.colPins[c], true);
    }
}

char Keypad::keyAt(uint8_t row, uint8_t col) const noexcept {
    return _cfg.keymap[static_cast<uint16_t>(row) * _cfg.cols + col];
}

uint64_t Keypad::readMatrix() const noexcept {
    uint64_t mask = 0;
    for (uint8_t c = 0; c < _cfg.cols; ++c) {
        _hal.writePin(_cfg.colPins[c], false);  // drive this column LOW
        for (uint8_t r = 0; r < _cfg.rows; ++r) {
            if (!_hal.readPin(_cfg.rowPins[r])) {  // pressed -> row pulled LOW
                const uint8_t bit =
                    static_cast<uint8_t>(r * _cfg.cols + c);
                mask |= (uint64_t{1} << bit);
            }
        }
        _hal.writePin(_cfg.colPins[c], true);  // restore idle HIGH
    }
    return mask;
}

char Keypad::scan() const noexcept {
    if (!valid()) {
        return kNoKey;
    }
    const uint64_t mask = readMatrix();
    if (mask == 0) {
        return kNoKey;
    }
    // Return the lowest-indexed pressed key.
    uint8_t idx = 0;
    uint64_t m = mask;
    while ((m & 1u) == 0) {
        m >>= 1;
        ++idx;
    }
    return keyAt(static_cast<uint8_t>(idx / _cfg.cols),
                 static_cast<uint8_t>(idx % _cfg.cols));
}

char Keypad::getKey() const noexcept {
    if (!valid() || _hal.delayMs == nullptr) {
        return kNoKey;
    }
    for (;;) {
        const char k = scan();
        if (k == kNoKey) {
            continue;
        }
        _hal.delayMs(_cfg.debounceMs);  // wait out the bounce
        if (scan() != k) {
            continue;  // false trigger
        }
        // Confirmed press; wait for release (debounced poll, not a tight spin).
        while (scan() != kNoKey) {
            _hal.delayMs(_cfg.debounceMs);
        }
        _hal.delayMs(_cfg.debounceMs);  // debounce the release too
        return k;
    }
}

bool Keypad::update(uint32_t now_ms) noexcept {
    _event = Event::None;
    _eventKey = kNoKey;
    if (!valid()) {
        return false;
    }

    const uint64_t raw = readMatrix();

    // Restart the stability window whenever the raw sample changes.
    if (raw != _candidate) {
        _candidate = raw;
        _changedAtMs = now_ms;
        return false;
    }
    // Wait for the candidate to hold steady for the debounce window.
    if (static_cast<uint32_t>(now_ms - _changedAtMs) < _cfg.debounceMs) {
        return false;
    }

    // Candidate is stable. Commit one changed key per call so that callers
    // observing event()/key() never miss a transition.
    const uint64_t diff = _stable ^ _candidate;
    if (diff == 0) {
        return false;
    }

    uint8_t idx = 0;
    uint64_t m = diff;
    while ((m & 1u) == 0) {
        m >>= 1;
        ++idx;
    }
    const uint64_t bit = (uint64_t{1} << idx);
    const bool pressed = (_candidate & bit) != 0;
    _stable ^= bit;  // commit this single transition

    _event = pressed ? Event::Pressed : Event::Released;
    _eventKey = keyAt(static_cast<uint8_t>(idx / _cfg.cols),
                      static_cast<uint8_t>(idx % _cfg.cols));
    return true;
}

}  // namespace kp
