# SPDX-License-Identifier: MIT
add_executable(keypad_tests keypad_test.cpp)
target_link_libraries(keypad_tests PRIVATE kp::keypad)
kp_set_warnings(keypad_tests)

add_test(NAME keypad_tests COMMAND keypad_tests)
