cmake_minimum_required(VERSION 3.13)
include(pico_sdk_import.cmake)

project(motherboard_project C CXX ASM)

pico_sdk_init()

add_executable(motherboard
    src/main.c
    src/display.c
    src/midi.c
    src/sd_card.c
    src/keyboard.c
)

target_link_libraries(motherboard
    pico_stdlib
    # add more hardware libs if needed:
    # hardware_spi hardware_i2c hardware_pwm hardware_adc hardware_dma
)

# Let CMake know about your include directory
target_include_directories(motherboard PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src
)

pico_enable_stdio_usb(motherboard 1)
pico_enable_stdio_uart(motherboard 0)

pico_add_extra_outputs(motherboard)
