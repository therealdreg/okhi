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
set(PICO_BOARD pico CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(okhi C CXX ASM)

set(PICO_DEOPTIMIZED_DEBUG 1)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

add_compile_options(-Wall
        -Wno-format          # int != int32_t as far as the compiler is concerned because gcc has int32_t as long int
        -Wno-unused-function # we have some for the docs that aren't called
        )
if (CMAKE_C_COMPILER_ID STREQUAL "GNU")
    add_compile_options(-Wno-maybe-uninitialized)
endif()

# Add executable. Default name is the project name, version 0.1

if (TARGET tinyusb_device)
    add_executable(okhi
            okhi.c
            )

    # pull in common dependencies
    target_link_libraries(okhi 
    pico_stdlib 
    pico_multicore 
    pico_stdio_uart
    hardware_uart 
    hardware_pio 
    hardware_spi 
    hardware_flash 
    hardware_sync
    hardware_adc)

pico_generate_pio_header(okhi ${CMAKE_CURRENT_LIST_DIR}/okhi.pio)

pico_set_binary_type(okhi copy_to_ram)

pico_enable_stdio_uart(okhi 1)
pico_enable_stdio_usb(okhi 0)

# create map/bin/hex file etc.
pico_add_extra_outputs(okhi)

# add url via pico_set_program_url
pico_set_program_url(okhi "https://github.com/therealdreg/okhi")

string(APPEND CMAKE_EXE_LINKER_FLAGS "-Wl,--print-memory-usage")

target_compile_definitions(okhi PRIVATE
    PICO_DEFAULT_UART=1
	PICO_DEFAULT_UART_TX_PIN=4
	PICO_DEFAULT_UART_RX_PIN=5
    PICO_DEFAULT_UART_BAUD_RATE=921600
    PICO_FLASH_SIZE_BYTES=16777216
	)


    # add url via pico_set_program_url
elseif(PICO_ON_DEVICE)
    message("Skipping hello_usb because TinyUSB submodule is not initialized in the SDK")
endif()
