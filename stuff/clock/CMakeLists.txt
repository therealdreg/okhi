cmake_minimum_required(VERSION 3.12)

# Pull in SDK (must be before project)
include(pico_sdk_import.cmake)

include(pico_extras_import_optional.cmake)

project(pico_examples C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

if (PICO_SDK_VERSION_STRING VERSION_LESS "1.5.1")
    message(FATAL_ERROR "Raspberry Pi Pico SDK version 1.5.1 (or later) required. Your version is ${PICO_SDK_VERSION_STRING}")
endif()

set(PICO_EXAMPLES_PATH ${PROJECT_SOURCE_DIR})

set(PICO_DEOPTIMIZED_DEBUG 1)

# Initialize the SDK
pico_sdk_init()

include(example_auto_set_url.cmake)

add_compile_options(-Wall
        -Wno-format          # int != int32_t as far as the compiler is concerned because gcc has int32_t as long int
        -Wno-unused-function # we have some for the docs that aren't called
        )
if (CMAKE_C_COMPILER_ID STREQUAL "GNU")
    add_compile_options(-Wno-maybe-uninitialized)
endif()

add_executable(clock
        clock.c
        )

# pull in common dependencies
target_link_libraries(clock
    pico_stdlib
    pico_multicore
    pico_stdio_uart
    hardware_uart
    hardware_pio
    hardware_spi
    hardware_flash
    hardware_sync)

pico_generate_pio_header(clock ${CMAKE_CURRENT_LIST_DIR}/clock.pio)

pico_set_binary_type(clock copy_to_ram)

pico_enable_stdio_uart(clock 0)
pico_enable_stdio_usb(clock 1)

# create map/bin/hex file etc.
pico_add_extra_outputs(clock)

# add url via pico_set_program_url
example_auto_set_url(clock)

string(APPEND CMAKE_EXE_LINKER_FLAGS "-Wl,--print-memory-usage")

target_compile_definitions(clock PRIVATE
    PICO_FLASH_SIZE_BYTES=16777216
	)
