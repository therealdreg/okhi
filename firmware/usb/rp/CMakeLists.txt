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

# A configure that passes no build type gets RelWithDebInfo, the type make_release.py
# builds, and the code is -O2 whatever the build type (target_compile_options below),
# the level it is tested at. CMake Tools configures its default variant, Debug, which
# leaves NDEBUG undefined. The SDK default, Release, is -O3: in this copy_to_ram image
# the extra code takes RAM, 17456 B more with SDK 2.3.1 and GCC 15.2.1, and the image
# then overflows the 256 KB RAM region and does not link.
if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(okhi C CXX ASM)

pico_sdk_init()

add_compile_options(-Wall
        -Wno-format          # arm-none-eabi-gcc's int32_t is long int, so %d with an int32_t warns
        -Wno-unused-function # the com/ headers define static functions this image never calls
        )
if (CMAKE_C_COMPILER_ID STREQUAL "GNU")
    add_compile_options(-Wno-maybe-uninitialized)
endif()

if (TARGET tinyusb_device)
    add_executable(okhi
            okhi.c
            )

    target_compile_options(okhi PRIVATE -O2)

    target_link_libraries(okhi 
    pico_stdlib 
    pico_multicore 
    pico_stdio_uart
    hardware_uart 
    hardware_pio 
    hardware_spi 
    hardware_flash 
    hardware_sync
    hardware_adc
    hardware_pwm)

pico_generate_pio_header(okhi ${CMAKE_CURRENT_LIST_DIR}/okhi.pio)

pico_set_binary_type(okhi copy_to_ram)

# The SDK's W25Q080 boot stage 2 with the ring oscillator sped up while crt0 copies this image to
# RAM, so the capture starts sooner after a reset (boot2_okhi.S says why).
pico_define_boot_stage2(okhi_boot2 ${CMAKE_CURRENT_LIST_DIR}/boot2_okhi.S)
pico_set_boot_stage2(okhi okhi_boot2)

pico_enable_stdio_uart(okhi 1)
pico_enable_stdio_usb(okhi 0)

pico_add_extra_outputs(okhi)

pico_set_program_url(okhi "https://github.com/therealdreg/okhi")

string(APPEND CMAKE_EXE_LINKER_FLAGS "-Wl,--print-memory-usage")

target_compile_definitions(okhi PRIVATE
    PICO_DEFAULT_UART=1
	PICO_DEFAULT_UART_TX_PIN=4
	PICO_DEFAULT_UART_RX_PIN=5
    PICO_DEFAULT_UART_BAUD_RATE=921600
	)


elseif(PICO_ON_DEVICE)
    message("Skipping hello_usb because TinyUSB submodule is not initialized in the SDK")
endif()
