// SPDX-License-Identifier: MIT
//
// Dependency-free unit tests for kp::Keypad.
//
// We avoid an external test framework on purpose: a tiny, self-contained
// harness keeps CI fast and mirrors the constraints of embedded targets.
// The mock HAL simulates a 4x4 matrix. Because GpioHal uses plain function
// pointers (which cannot capture state), the mock lives in a single global
// instance that the free functions below operate on.

#include <cstdio>

#include "keypad/keypad.hpp"

namespace {

// ---- Minimal test harness ---------------------------------------------------
int g_failures = 0;
int g_checks = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        ++g_checks;                                                        \
        if (!(cond)) {                                                     \
            std::printf("FAIL %s:%d: CHECK(%s)\n", __FILE__, __LINE__,     \
                        #cond);                                            \
            ++g_failures;                                                  \
        }                                                                  \
    } while (0)

#define CHECK_EQ(actual, expected)                                            \
    do {                                                                      \
        ++g_checks;                                                           \
        const auto a_ = (actual);                                             \
        const auto e_ = (expected);                                           \
        if (!(a_ == e_)) {                                                    \
            std::printf("FAIL %s:%d: CHECK_EQ(%s, %s)\n", __FILE__, __LINE__, \
                        #actual, #expected);                                  \
            ++g_failures;                                                     \
        }                                                                     \
    } while (0)

// ---- Mock 4x4 matrix --------------------------------------------------------
// Row pins are 0..3, column pins are 4..7, so pin <-> index mapping is trivial.
constexpr uint8_t kRowPins[4] = {0, 1, 2, 3};
constexpr uint8_t kColPins[4] = {4, 5, 6, 7};
constexpr char kKeymap[16] = {
    '1', '2', '3', 'A',  //
    '4', '5', '6', 'B',  //
    '7', '8', '9', 'C',  //
    '*', '0', '#', 'D',  //
};

struct Mock {
    bool pressed[4][4] = {};   // pressed[row][col]
    bool colLow[16] = {};      // indexed by pin number
    uint32_t now = 0;          // virtual clock advanced by delayMs
    int releaseAfterDelays = -1;  // <0 disables auto-release

    void press(uint8_t row, uint8_t col) { pressed[row][col] = true; }
    void releaseAll() {
        for (auto& row : pressed)
            for (auto& cell : row) cell = false;
    }
};

Mock* g = nullptr;

void mSetOutput(uint8_t) {}
void mSetInput(uint8_t) {}
void mWrite(uint8_t pin, bool high) { g->colLow[pin] = !high; }

bool mRead(uint8_t pin) {
    // pin is a row pin (0..3). Idle HIGH unless a pressed key on this row sits
    // on a column currently driven LOW.
    const uint8_t row = pin;
    for (uint8_t c = 0; c < 4; ++c) {
        if (g->colLow[kColPins[c]] && g->pressed[row][c]) {
            return false;  // pulled LOW
        }
    }
    return true;
}

void mDelay(uint32_t ms) {
    g->now += ms;
    if (g->releaseAfterDelays > 0) {
        if (--g->releaseAfterDelays == 0) {
            g->releaseAll();
        }
    }
}

kp::Keypad::GpioHal makeHal() {
    kp::Keypad::GpioHal hal;
    hal.setPinOutput = &mSetOutput;
    hal.setPinInput = &mSetInput;
    hal.writePin = &mWrite;
    hal.readPin = &mRead;
    hal.delayMs = &mDelay;
    return hal;
}

kp::Keypad::Config makeConfig() {
    kp::Keypad::Config cfg;
    cfg.rowPins = kRowPins;
    cfg.colPins = kColPins;
    cfg.keymap = kKeymap;
    cfg.rows = 4;
    cfg.cols = 4;
    cfg.debounceMs = 20;
    return cfg;
}

// ---- Tests ------------------------------------------------------------------
void test_valid_and_invalid_config() {
    Mock mock;
    g = &mock;

    kp::Keypad good(makeConfig(), makeHal());
    CHECK(good.valid());

    kp::Keypad::Config bad = makeConfig();
    bad.rows = 0;
    kp::Keypad badKeypad(bad, makeHal());
    CHECK(!badKeypad.valid());

    kp::Keypad::Config tooBig = makeConfig();
    tooBig.rows = 9;  // exceeds kMaxRows
    kp::Keypad bigKeypad(tooBig, makeHal());
    CHECK(!bigKeypad.valid());
}

void test_scan_returns_pressed_key() {
    Mock mock;
    g = &mock;
    kp::Keypad pad(makeConfig(), makeHal());
    pad.init();

    CHECK_EQ(pad.scan(), kp::Keypad::kNoKey);

    mock.press(0, 0);
    CHECK_EQ(pad.scan(), '1');

    mock.releaseAll();
    mock.press(3, 2);
    CHECK_EQ(pad.scan(), '#');

    mock.releaseAll();
    CHECK_EQ(pad.scan(), kp::Keypad::kNoKey);
}

void test_update_press_and_release_events() {
    Mock mock;
    g = &mock;
    kp::Keypad pad(makeConfig(), makeHal());
    pad.init();

    mock.press(1, 2);  // '6'

    CHECK(!pad.update(0));   // first sample: candidate changes, no commit
    CHECK(!pad.update(10));  // still inside debounce window
    CHECK(pad.update(25));   // stable -> Pressed
    CHECK(pad.event() == kp::Keypad::Event::Pressed);
    CHECK_EQ(pad.key(), '6');

    // No further change while held.
    CHECK(!pad.update(40));

    mock.releaseAll();
    CHECK(!pad.update(45));  // candidate changes back to 0
    CHECK(pad.update(70));   // stable -> Released
    CHECK(pad.event() == kp::Keypad::Event::Released);
    CHECK_EQ(pad.key(), '6');
}

void test_update_reports_one_transition_per_call() {
    Mock mock;
    g = &mock;
    kp::Keypad pad(makeConfig(), makeHal());
    pad.init();

    // Two keys appear simultaneously.
    mock.press(0, 0);  // '1'
    mock.press(0, 1);  // '2'

    CHECK(!pad.update(0));
    CHECK(pad.update(25));  // first transition
    CHECK(pad.event() == kp::Keypad::Event::Pressed);
    const char first = pad.key();

    CHECK(pad.update(26));  // second transition, same stable candidate
    CHECK(pad.event() == kp::Keypad::Event::Pressed);
    const char second = pad.key();

    CHECK((first == '1' && second == '2') || (first == '2' && second == '1'));
    CHECK(!pad.update(27));  // nothing left to commit
}

void test_getkey_blocking() {
    Mock mock;
    g = &mock;
    kp::Keypad pad(makeConfig(), makeHal());
    pad.init();

    mock.press(3, 3);          // 'D'
    mock.releaseAfterDelays = 2;  // auto-release after a couple of debounce waits
    CHECK_EQ(pad.getKey(), 'D');
}

}  // namespace

int main() {
    test_valid_and_invalid_config();
    test_scan_returns_pressed_key();
    test_update_press_and_release_events();
    test_update_reports_one_transition_per_call();
    test_getkey_blocking();

    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
