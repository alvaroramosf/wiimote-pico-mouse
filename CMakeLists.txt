cmake_minimum_required(VERSION 3.13)

# Default to Raspberry Pi Pico 2 W (RP2350 + CYW43439); override with -DPICO_BOARD=pico_w
set(PICO_BOARD pico2_w CACHE STRING "Board type (pico2_w or pico_w)")

include(pico_sdk_import.cmake)

# Single source of truth for the firmware version: the VERSION file at the repo root
# (e.g. "0.1.0" or "0.1.0-beta"). The release workflow verifies it matches the git tag.
file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/VERSION" FIRMWARE_VERSION_FULL LIMIT_COUNT 1)
string(REGEX MATCH "^[0-9]+\\.[0-9]+\\.[0-9]+" FIRMWARE_VERSION_NUM "${FIRMWARE_VERSION_FULL}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/VERSION")

project(wiimote_pico_mouse VERSION ${FIRMWARE_VERSION_NUM} LANGUAGES C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(wiimote_pico_mouse
    src/main.c
    src/usb_descriptors.c
    src/usb_hid.c
    src/wiimote_bt.c
    src/diamond_tracker.c
    src/one_euro_filter.c
    src/flash_storage.c
    src/ir_leds.c
)

target_include_directories(wiimote_pico_mouse PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src
)

target_compile_definitions(wiimote_pico_mouse PRIVATE
    FIRMWARE_VERSION_STRING="${FIRMWARE_VERSION_FULL}"
)

target_compile_options(wiimote_pico_mouse PRIVATE
    -Wall
    -Wextra
)

target_link_libraries(wiimote_pico_mouse
    pico_stdlib
    pico_cyw43_arch_none
    pico_btstack_classic
    pico_btstack_cyw43
    pico_flash
    hardware_flash
    hardware_sync
    hardware_gpio
    tinyusb_device
    tinyusb_board
    m
)

# USB port is dedicated to TinyUSB Composite HID Mouse + Keyboard + Consumer Control.
# Debug logs are routed to UART (GP0 TX / GP1 RX) at 115200 baud.
pico_enable_stdio_usb(wiimote_pico_mouse 0)
pico_enable_stdio_uart(wiimote_pico_mouse 1)

pico_add_extra_outputs(wiimote_pico_mouse)
