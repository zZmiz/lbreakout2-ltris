cmake_minimum_required(VERSION 3.16)
project(lgames_host_smoke C CXX)
get_filename_component(ROOT ../.. ABSOLUTE)
set(CMAKE_CXX_STANDARD 17)
add_compile_options(-Wno-implicit-function-declaration -Wno-int-conversion -Wno-incompatible-pointer-types -Wno-deprecated-non-prototype)
# System dirent.h must precede the embedded filesystem shim.
add_compile_options(-idirafter ${ROOT}/platform)
add_compile_definitions(LGAMES_PORT=1 DISABLE_INSTALL=1 VERSION="test" PACKAGE="test" SRC_DIR="." SRCDIR="." HI_DIR="." LOCALEDIR=".")
file(GLOB TRIS ${ROOT}/upstream/ltris/src/*.c)
file(GLOB BREAKOUT ${ROOT}/upstream/lbreakout2/client/*.c ${ROOT}/upstream/lbreakout2/game/*.c ${ROOT}/upstream/lbreakout2/gui/*.c)
add_executable(ltris_host ${TRIS} platform.cpp ${ROOT}/platform/mini_sdl.cpp)
add_executable(lbreakout2_host ${BREAKOUT} ${ROOT}/upstream/lbreakout2/common/parser.c ${ROOT}/upstream/lbreakout2/common/tools.c platform.cpp ${ROOT}/platform/mini_sdl.cpp)
set_source_files_properties(${BREAKOUT} ${ROOT}/upstream/lbreakout2/common/parser.c
    ${ROOT}/upstream/lbreakout2/common/tools.c PROPERTIES
    COMPILE_OPTIONS "-include;${ROOT}/platform/game_alloc.h")
set_source_files_properties(${TRIS} PROPERTIES
    COMPILE_OPTIONS "-include;${ROOT}/platform/game_alloc.h")
target_include_directories(ltris_host PRIVATE . ${ROOT}/upstream/ltris/src)
target_include_directories(lbreakout2_host PRIVATE . ${ROOT}/upstream/lbreakout2/client ${ROOT}/upstream/lbreakout2/game ${ROOT}/upstream/lbreakout2/gui ${ROOT}/upstream/lbreakout2/common)
target_compile_definitions(ltris_host PRIVATE RP_GAME_LTRIS=1)
target_compile_definitions(lbreakout2_host PRIVATE RP_GAME_LBREAKOUT2=1 RP_HOST_EXHAUSTIVE=1)
target_link_libraries(ltris_host PRIVATE m)
target_link_libraries(lbreakout2_host PRIVATE m)

foreach(menu_test IN ITEMS system_menu_test system_menu_lb_test)
    add_executable(${menu_test} system_menu_test.cpp ${ROOT}/platform/system_menu.cpp
        ${ROOT}/platform/usb_pad.cpp ${ROOT}/platform/mini_sdl.cpp)
    target_include_directories(${menu_test} BEFORE PRIVATE menu_stubs)
    target_compile_definitions(${menu_test} PRIVATE RP_LGAMES_CONTAINER=1 RP_SYSTEM_SETTINGS_PATH="/tmp/lgames-menu-test.settings")
    target_compile_options(${menu_test} PRIVATE -UNDEBUG -ffunction-sections -fdata-sections)
    target_link_options(${menu_test} PRIVATE -Wl,--gc-sections)
endforeach()
target_compile_definitions(system_menu_test PRIVATE RP_GAME_LTRIS=1)
target_compile_definitions(system_menu_lb_test PRIVATE RP_GAME_LBREAKOUT2=1)
add_executable(loader_bundle_test loader_bundle_test.c loader_reference/program_name.c)
target_compile_options(loader_bundle_test PRIVATE -UNDEBUG)

add_executable(ltris_render_test ltris_render_test.cpp platform.cpp
    ${ROOT}/platform/mini_sdl.cpp ${ROOT}/upstream/ltris/src/bowl.c
    ${ROOT}/upstream/ltris/src/sdl.c ${ROOT}/upstream/ltris/src/shrapnells.c
    ${ROOT}/upstream/ltris/src/list.c)
target_include_directories(ltris_render_test PRIVATE . ${ROOT}/upstream/ltris/src)
target_compile_definitions(ltris_render_test PRIVATE RP_GAME_LTRIS=1)
target_compile_options(ltris_render_test PRIVATE -UNDEBUG -ffunction-sections -fdata-sections)
target_link_options(ltris_render_test PRIVATE -Wl,--gc-sections)
target_link_libraries(ltris_render_test PRIVATE m)

get_target_property(RENDER_SOURCES ltris_render_test SOURCES)
add_executable(ltris_sram_test ${RENDER_SOURCES} sram_budget.cpp)
target_include_directories(ltris_sram_test PRIVATE . ${ROOT}/upstream/ltris/src)
target_compile_definitions(ltris_sram_test PRIVATE RP_GAME_LTRIS=1 RP_HOST_SRAM_BUDGET=1)
target_compile_options(ltris_sram_test PRIVATE -UNDEBUG -ffunction-sections -fdata-sections)
target_link_options(ltris_sram_test PRIVATE -Wl,--gc-sections
    -Wl,--wrap=malloc -Wl,--wrap=calloc -Wl,--wrap=realloc -Wl,--wrap=free)
target_link_libraries(ltris_sram_test PRIVATE m)
