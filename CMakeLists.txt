cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(pico-ata C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# Add executable. Default name is the project name, version 0.1

add_executable(pico-ata-test
    ata.cpp
    atapi.cpp
    pico-ata-test.cpp
)

pico_generate_pio_header(pico-ata-test ${CMAKE_CURRENT_LIST_DIR}/ata.pio)

pico_set_program_name(pico-ata-test "pico-ata-test")
pico_set_program_version(pico-ata-test "0.1")

pico_enable_stdio_uart(pico-ata-test 1)
pico_enable_stdio_usb(pico-ata-test 1)

# Add the standard library to the build
target_link_libraries(pico-ata-test
    pico_stdlib
)

# Add any user requested libraries
target_link_libraries(pico-ata-test
    hardware_dma
    hardware_pio
)

target_include_directories(pico-ata-test PRIVATE ${CMAKE_CURRENT_LIST_DIR})

target_compile_definitions(pico-ata-test PUBLIC
    PICO_DEFAULT_UART=0
    PICO_DEFAULT_UART_TX_PIN=32
    PICO_DEFAULT_UART_RX_PIN=33
)

target_compile_options(pico-ata-test PRIVATE -Wall)

pico_add_extra_outputs(pico-ata-test)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/pico-ata-test.uf2
    DESTINATION bin
)