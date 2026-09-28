
cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type")
set(PICO_BOARD pico2b CACHE STRING "RP2350B target board")
list(APPEND PICO_BOARD_HEADER_DIRS "${CMAKE_CURRENT_LIST_DIR}/platform/boards")
set(PICO_DEFAULT_BOOT_STAGE2_FILE
    "${CMAKE_CURRENT_LIST_DIR}/platform/boot2_xtx_quad.S"
    CACHE STRING "XTX/Puya-compatible quad boot stage" FORCE)
add_compile_definitions(PICO_FLASH_SPI_CLKDIV=6)

include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)
set(PICO_PIO_USB_PATH "${PICO_SDK_PATH}/../Pico-PIO-USB" CACHE PATH "Existing PIO USB source")
project(lgames_port C CXX ASM)
pico_sdk_init()
option(RP_LGAMES_CONTAINER "Build one selector plus isolated game slots" OFF)

add_subdirectory(platform/vendor/pico_hdmi)
set(LWMEM_OPTS_FILE "${CMAKE_CURRENT_LIST_DIR}/platform/lwmem_opts.h")
add_subdirectory(platform/vendor/lwmem)
add_subdirectory(platform/vendor/pico_psram)
add_subdirectory(platform/vendor/pico_fatfs)

add_library(lgames_platform STATIC
    platform/rp_platform.cpp
    platform/usb_pad.cpp
    platform/mini_mixer.cpp
    platform/pcm5102_audio.c
    platform/system_menu.cpp
    platform/system_launch.cpp
    platform/fatfs_io.c
    platform/vendor/nespad.cpp
    platform/vendor/lodepng/lodepng.c
)
pico_generate_pio_header(lgames_platform
    ${CMAKE_CURRENT_LIST_DIR}/platform/pcm5102_audio.pio)
if(RP_LGAMES_CONTAINER)
    target_compile_definitions(lgames_platform PUBLIC RP_LGAMES_CONTAINER=1)
endif()
target_include_directories(lgames_platform PUBLIC
    platform
    platform/vendor
    platform/vendor/lodepng
)
target_compile_definitions(lgames_platform PUBLIC
    PSRAM_CS_PIN=47
    PICO_PSRAM_CS_PIN=47 PICO_PSRAM_SIZE_BYTES=0x800000
    HAVE_HARDWARE_PSRAM=1
    GPIOHSTXD0=16 GPIOHSTXD1=14 GPIOHSTXD2=12 GPIOHSTXCK=18
    GPIOHSTXINVERTED=0
    NOHSTXCLOCK=1
    RP_NATIVE_RGB332=1 DI_RING_BUFFER_SIZE=256
    PICO_STACK_SIZE=4096
)
target_link_libraries(lgames_platform PUBLIC
    pico_stdlib pico_multicore cmsis_core hardware_clocks hardware_dma hardware_pio
    hardware_spi hardware_watchdog hardware_psram
    pico_hdmi pico_psram pico_fatfs lwmem tinyusb_host tinyusb_pico_pio_usb
)

file(GLOB LTRIS_SOURCES CONFIGURE_DEPENDS upstream/ltris/src/*.c)
file(GLOB LBREAKOUT_CLIENT CONFIGURE_DEPENDS upstream/lbreakout2/client/*.c)
file(GLOB LBREAKOUT_GAME CONFIGURE_DEPENDS upstream/lbreakout2/game/*.c)
file(GLOB LBREAKOUT_GUI CONFIGURE_DEPENDS upstream/lbreakout2/gui/*.c)
set(LBREAKOUT_COMMON
    upstream/lbreakout2/common/parser.c
    upstream/lbreakout2/common/tools.c
)
set_source_files_properties(${LBREAKOUT_CLIENT} ${LBREAKOUT_GAME}
    ${LBREAKOUT_GUI} ${LBREAKOUT_COMMON} PROPERTIES
    COMPILE_OPTIONS "-include;${CMAKE_CURRENT_LIST_DIR}/platform/game_alloc.h")
set_source_files_properties(${LTRIS_SOURCES} PROPERTIES
    COMPILE_OPTIONS "-include;${CMAKE_CURRENT_LIST_DIR}/platform/game_alloc.h")

function(configure_lgame target title game_define)
    target_sources(${target} PRIVATE platform/mini_sdl.cpp)
    pico_set_program_name(${target} "${title}")
    pico_set_program_version(${target} "0.1-rp2350")
    pico_enable_stdio_uart(${target} 1)
    pico_enable_stdio_usb(${target} 0)
    target_compile_definitions(${target} PRIVATE
        LGAMES_PORT=1 DISABLE_INSTALL=1 ${game_define}=1
        VERSION="1.3.3-rp2350" PACKAGE="${target}"
        SRC_DIR="." SRCDIR="." HI_DIR="." LOCALEDIR="."
    )
    target_compile_options(${target} PRIVATE
        -O3 -ffunction-sections -fdata-sections
        -Wno-unused-parameter -Wno-unused-function -Wno-sign-compare
        -Wno-implicit-function-declaration -Wno-int-conversion
    )
    target_link_options(${target} PRIVATE -Wl,--gc-sections -Wl,--print-memory-usage)
    target_link_libraries(${target} PRIVATE lgames_platform m)
    include(platform/BootPartition.cmake)
    if(RP_LGAMES_CONTAINER AND target STREQUAL "ltris")
        _frens_relink_flash(${target} 0x10180000 0x100000)
    elseif(RP_LGAMES_CONTAINER AND target STREQUAL "lbreakout2")
        _frens_relink_flash(${target} 0x10280000 0x100000)
    elseif(RP_LGAMES_CONTAINER)
        _frens_relink_flash(${target} 0x10080000 0x100000)
    else()
        frens_offset_for_bootloader(${target})
    endif()
    set_target_properties(${target} PROPERTIES
        PICOTOOL_EXTRA_UF2_ARGS "--platform;rp2350")
    pico_add_extra_outputs(${target})
endfunction()

add_executable(ltris apps/ltris_main.cpp ${LTRIS_SOURCES})
set_source_files_properties(upstream/ltris/src/main.c PROPERTIES
    COMPILE_DEFINITIONS "main=ltris_game_main")
target_include_directories(ltris PRIVATE platform upstream/ltris/src)
configure_lgame(ltris "LTris" RP_GAME_LTRIS)
target_compile_definitions(ltris PRIVATE SOUND=1)
add_custom_command(TARGET ltris POST_BUILD
    COMMAND ${CMAKE_COMMAND} -DMAP=${CMAKE_CURRENT_BINARY_DIR}/ltris.elf.map
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_ltris_sram.cmake
    VERBATIM)

add_executable(lbreakout2 apps/lbreakout2_main.cpp
    ${LBREAKOUT_CLIENT} ${LBREAKOUT_GAME} ${LBREAKOUT_GUI} ${LBREAKOUT_COMMON})
set_source_files_properties(upstream/lbreakout2/client/main.c PROPERTIES
    COMPILE_DEFINITIONS "main=lbreakout2_game_main")
target_include_directories(lbreakout2 PRIVATE
    platform upstream/lbreakout2/client upstream/lbreakout2/game
    upstream/lbreakout2/gui upstream/lbreakout2/common)
configure_lgame(lbreakout2 "LBreakout2" RP_GAME_LBREAKOUT2)
target_compile_definitions(lbreakout2 PRIVATE AUDIO_ENABLED=1)

if(RP_LGAMES_CONTAINER)
    add_executable(lgames apps/lgames_main.cpp)
    configure_lgame(lgames "LGames" RP_GAME_SELECTOR)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_custom_target(lgames_bundle ALL
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/package_lgames.py
            --selector ${CMAKE_CURRENT_BINARY_DIR}/lgames.uf2
            --ltris ${CMAKE_CURRENT_BINARY_DIR}/ltris.uf2
            --lbreakout2 ${CMAKE_CURRENT_BINARY_DIR}/lbreakout2.uf2
            --output ${CMAKE_CURRENT_BINARY_DIR}/lgames-bundle.uf2
        DEPENDS lgames ltris lbreakout2
        VERBATIM)
endif()
