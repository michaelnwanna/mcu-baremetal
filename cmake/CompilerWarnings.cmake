# SPDX-License-Identifier: MIT
# Shared, opt-in compiler warning configuration.
#
# Usage:  kp_set_warnings(<target>)

function(kp_set_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /permissive-)
  else()
    target_compile_options(${target} PRIVATE
      -Wall
      -Wextra
      -Wpedantic
      -Wshadow
      -Wconversion
      -Wsign-conversion
      -Wnon-virtual-dtor
      -Wold-style-cast
      -Wcast-align
      -Wunused
      -Wnull-dereference
      -Wdouble-promotion
    )
  endif()
endfunction()
