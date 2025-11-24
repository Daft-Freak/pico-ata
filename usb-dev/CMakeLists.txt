add_executable(pico-ata-usb
    pico-ata-usb.cpp
    usb_descriptors.c
)

pico_set_program_name(pico-ata-usb "pico-ata-usb")
pico_set_program_version(pico-ata-usb "0.1")

pico_enable_stdio_uart(pico-ata-usb 1)
pico_enable_stdio_usb(pico-ata-usb 1)

# Add the libraries to the build
target_link_libraries(pico-ata-usb
    pico-ata
    pico_stdlib
    pico_unique_id
    tinyusb_device
)

target_include_directories(pico-ata-usb PRIVATE ${CMAKE_CURRENT_LIST_DIR})

target_compile_definitions(pico-ata-usb PUBLIC
    PICO_DEFAULT_UART=0
    PICO_DEFAULT_UART_TX_PIN=32
    PICO_DEFAULT_UART_RX_PIN=33
    PICO_DEFAULT_LED_PIN=31
    PICO_DEFAULT_LED_PIN_INVERTED=1
)

target_compile_options(pico-ata-usb PRIVATE -Wall)

pico_add_extra_outputs(pico-ata-usb)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/pico-ata-usb.uf2
    DESTINATION bin
)
