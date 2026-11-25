# PicoSing - karaoke for the MSX PicoVerse 2350
cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Core2350B (RP2350B, 48 GPIOs) - same board definition as the PicoVerse firmware
set(PICO_BOARD pimoroni_pga2350 CACHE STRING "Board type")
# No RP2350-A2 "absolute" UF2 block (it would write the last page of the flash)
set(PICO_RP2350_A2_SUPPORTED 0 CACHE BOOL "" FORCE)

include(pico_sdk_import.cmake)
include(pico_extras_import.cmake)      # pico_audio_i2s

project(picosing C CXX ASM)
pico_sdk_init()

# Libraries (as the PicoVerse firmware has them in lib/)
set(FATFS_SD_PATH ${CMAKE_CURRENT_LIST_DIR}/lib/no-OS-FatFS-SD-SDIO-SPI-RPi-Pico CACHE PATH "carlk3 no-OS-FatFS-SD-SDIO-SPI-RPi-Pico")
set(PICOMP3LIB_PATH ${CMAKE_CURRENT_LIST_DIR}/lib/picomp3lib CACHE PATH "ikjordan picomp3lib")
add_subdirectory(${FATFS_SD_PATH}/src build_sd_lib)
add_subdirectory(${PICOMP3LIB_PATH}/src picomp3lib_build)

# The card is on SPI: leave out the SD library's SDIO driver and crash recorder
# (with clang they clash with the SDK's intrinsics); sd_stubs.c stands in.
get_target_property(_sd_srcs no-OS-FatFS-SD-SDIO-SPI-RPi-Pico INTERFACE_SOURCES)
list(FILTER _sd_srcs EXCLUDE REGEX "SDIO/rp2040_sdio\\.c$|src/crash\\.c$")
set_target_properties(no-OS-FatFS-SD-SDIO-SPI-RPi-Pico PROPERTIES INTERFACE_SOURCES "${_sd_srcs}")
set_source_files_properties(sd_stubs.c PROPERTIES INCLUDE_DIRECTORIES ${FATFS_SD_PATH}/src/sd_driver/SDIO)

add_executable(picosing
    main.c
    plat_pico.c
    hw_config.c
    sd_stubs.c
    singstar_uac.c
    uac_desc.c
    pitch.c
    game/game.c
    game/song.c
    game/fb.c
    game/dlist.c
    game/library.c
    game/screen_link.c
    game/menus.c
    game/title.c
    game/singers.c
    game/cover.c
    game/id3.c
    game/settings.c
    game/scratch.c
    game/music.c
    game/pclink.c
    game/usdb.c
    net/esp.c
    net/web.c
    game/synth.c
    game/tjpgd/tjpgd.c
    pva/pva_decode.c
    audio/audio_hal.c
    audio/mp3_alloc_dispatch.c
)

pico_generate_pio_header(picosing ${CMAKE_CURRENT_LIST_DIR}/msx_bus.pio)

pico_set_program_name(picosing "picosing")
pico_set_program_version(picosing "0.7")
pico_enable_stdio_uart(picosing 0)
pico_enable_stdio_usb(picosing 0)

target_link_libraries(picosing
    pico_stdlib
    pico_multicore
    pico_audio_i2s
    hardware_pio
    hardware_dma
    tinyusb_host
    tinyusb_board
    no-OS-FatFS-SD-SDIO-SPI-RPi-Pico
    picomp3lib
)

# The MP3 decoder's state comes from the arena (as in PicoAmp)
set_source_files_properties(${PICOMP3LIB_PATH}/src/buffers.c PROPERTIES
    COMPILE_DEFINITIONS "MPDEC_ALLOCATOR=pv_mp3_alloc"
    COMPILE_OPTIONS "-include;${CMAKE_CURRENT_LIST_DIR}/audio/mp3_alloc_dispatch.h")

target_compile_definitions(picosing PRIVATE
    PICO_AUDIO_I2S_PIO=0
)
target_include_directories(picosing PRIVATE ${CMAKE_CURRENT_LIST_DIR} ${CMAKE_CURRENT_LIST_DIR}/pva ${CMAKE_CURRENT_LIST_DIR}/game ${CMAKE_CURRENT_LIST_DIR}/game/tjpgd)
target_compile_options(picosing PRIVATE -Wall)

pico_add_extra_outputs(picosing)
