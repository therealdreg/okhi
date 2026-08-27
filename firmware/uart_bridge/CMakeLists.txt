# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Initialise pico_sdk from installed location
# (note this can come from environment, CMake cache etc)

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.3.0)
set(toolchainVersion 15_2_Rel1)
set(picotoolVersion 2.3.0)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
list(APPEND PICO_BOARD_HEADER_DIRS ${CMAKE_CURRENT_LIST_DIR}/boards)
set(PICO_BOARD okhi CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(uart_bridge C CXX ASM)

# TinyUSB turns CFG_TUSB_DEBUG on by itself in Debug builds, which links its
# logging into the USB task. LOG is the knob its family.cmake honours first.
set(LOG 0)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

add_compile_options(-Wall)

# Add executable. Default name is the project name, version 0.1

if (TARGET tinyusb_device)
    add_executable(uart_bridge
            uart-bridge.c
            usb-descriptors.c
            )

    # tusb_config.h lives next to the sources and TinyUSB includes it by name
    target_include_directories(uart_bridge PUBLIC
            ${CMAKE_CURRENT_LIST_DIR}
            )

    # pull in common dependencies
    target_link_libraries(uart_bridge
    pico_stdlib
    pico_multicore
    hardware_flash
    tinyusb_device)

pico_set_binary_type(uart_bridge copy_to_ram)

# the bridge drives the ESP32 flashing at 921600 baud with the UART FIFOs off,
# so it is never built with PICO_DEOPTIMIZED_DEBUG
pico_enable_stdio_uart(uart_bridge 0)
pico_enable_stdio_usb(uart_bridge 0)

# create map/bin/hex file etc.
pico_add_extra_outputs(uart_bridge)

# add url via pico_set_program_url
pico_set_program_url(uart_bridge "https://github.com/therealdreg/okhi")

string(APPEND CMAKE_EXE_LINKER_FLAGS "-Wl,--print-memory-usage")

elseif(PICO_ON_DEVICE)
    message("Skipping uart_bridge because TinyUSB submodule is not initialized in the SDK")
endif()
