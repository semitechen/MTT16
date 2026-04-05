cmake_minimum_required(VERSION 3.13)

include(pico_sdk_import.cmake)

project(motherboard C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

pico_sdk_init()

find_package(Git REQUIRED)

set(SD_PATCH_FILE "${CMAKE_CURRENT_SOURCE_DIR}/src/sd_config/fix_pio_offset.patch")
set(SD_LIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/lib/no-OS-FatFS")

add_custom_target(apply_sd_patch
	COMMAND ${GIT_EXECUTABLE} checkout .
	COMMAND ${GIT_EXECUTABLE} apply ${SD_PATCH_FILE}
	WORKING_DIRECTORY ${SD_LIB_DIR}
	VERBATIM
)

add_subdirectory(lib/no-OS-FatFS/src build/no-OS-FatFS)

add_dependencies(no-OS-FatFS-SD-SDIO-SPI-RPi-Pico apply_sd_patch)

add_executable(motherboard
	src/main.c
	src/display.c
	src/sequencer.c
	src/keyboard.c
	src/storage.c
	src/sd_config/sd_driver_config.c
	src/shared.c
)

pico_generate_pio_header(motherboard ${CMAKE_CURRENT_LIST_DIR}/src/midi_tx.pio)

target_include_directories(motherboard PRIVATE
	src
	src/sd_config
)

target_link_libraries(motherboard
	pico_stdlib
	hardware_spi
	hardware_dma
	hardware_pio
	hardware_irq
	hardware_clocks
	no-OS-FatFS-SD-SDIO-SPI-RPi-Pico
)

add_custom_target(flash
	DEPENDS motherboard
	COMMAND picotool load -f -x motherboard.uf2
	WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
	COMMENT "Forcing BOOTSEL reboot and flashing motherboard.uf2..."
)

pico_enable_stdio_usb(motherboard 1)
pico_enable_stdio_uart(motherboard 0)

pico_add_extra_outputs(motherboard)
