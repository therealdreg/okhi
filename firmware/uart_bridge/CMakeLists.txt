cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.3.1)
set(toolchainVersion 15_2_Rel1)
set(picotoolVersion 2.3.1)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
list(APPEND PICO_BOARD_HEADER_DIRS ${CMAKE_CURRENT_LIST_DIR}/boards)
set(PICO_BOARD okhi CACHE STRING "Board type")

# Builds default to RelWithDebInfo, and the code is -O2 whatever the build type
# (target_compile_options below), like the okhi images. The SDK default, Release,
# is -O3. The bridge takes one interrupt per UART byte (FIFOs off) while esptool
# flashes the ESP32 at 921600 baud, so it is never built with -O0.
if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(uart_bridge C CXX ASM)

# TinyUSB turns CFG_TUSB_DEBUG on by itself in Debug builds, which links its
# logging into the USB task. LOG is the knob its family.cmake honours first.
set(LOG 0)

pico_sdk_init()

add_compile_options(-Wall)

if (TARGET tinyusb_device)
    add_executable(uart_bridge
            uart-bridge.c
            usb-descriptors.c
            )

    target_compile_options(uart_bridge PRIVATE -O2)

    # tusb_config.h lives next to the sources and TinyUSB includes it by name
    target_include_directories(uart_bridge PUBLIC
            ${CMAKE_CURRENT_LIST_DIR}
            )

    target_link_libraries(uart_bridge
    pico_stdlib
    pico_multicore
    hardware_flash
    tinyusb_device)

pico_set_binary_type(uart_bridge copy_to_ram)

pico_enable_stdio_uart(uart_bridge 0)
pico_enable_stdio_usb(uart_bridge 0)

pico_add_extra_outputs(uart_bridge)

pico_set_program_url(uart_bridge "https://github.com/therealdreg/okhi")

string(APPEND CMAKE_EXE_LINKER_FLAGS "-Wl,--print-memory-usage")

elseif(PICO_ON_DEVICE)
    message("Skipping uart_bridge because TinyUSB submodule is not initialized in the SDK")
endif()
