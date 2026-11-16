# Host tests for the node logic. Build and run on a PC:
#
#   cmake -S tests -B build-tests && cmake --build build-tests && ctest --test-dir build-tests
#
cmake_minimum_required(VERSION 3.22)
project(blinky_tests C)

set(CMAKE_C_STANDARD 11)
set(FW ${CMAKE_CURRENT_SOURCE_DIR}/../firmware)

add_executable(test_blinky
    test_blinky.c
    ${FW}/blinky.c
    ${FW}/persist.c
    ${FW}/vscp-can.c
    ${FW}/third-party/vscp-firmware/src/vscp-firmware.c
    ${FW}/third-party/vscp-firmware/src/vscp-firmware-helper.c
)
# The stubs come first so they replace main.h and can.h of the firmware
target_include_directories(test_blinky PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs
    ${FW}
    ${FW}/third-party/vscp-firmware/src
)
target_compile_options(test_blinky PRIVATE -Wall -Wextra)
target_link_libraries(test_blinky PRIVATE m)

enable_testing()
add_test(NAME blinky COMMAND test_blinky)
