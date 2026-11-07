cmake_minimum_required(VERSION 3.15)
set(CMAKE_SYSTEM_NAME Windows)
project(CoD4X-client VERSION 1.0.0 LANGUAGES ASM_NASM C CXX)

option(OFFICIAL_BUILD "Build official release" OFF)
option(RMLUI "Build the optional RmlUi diagnostic host" OFF)
option(RMLUI_TESTS "Build standalone RmlUi render and lifecycle checks" OFF)
if(RMLUI_TESTS)
    if(NOT RMLUI)
        message(FATAL_ERROR "RMLUI_TESTS requires RMLUI")
    endif()
    enable_testing()
endif()
set(COD4X_APPDATA_SUBDIR "CallofDuty4MW" CACHE STRING "Single LocalAppData folder used by the client and its bootstrap")
if(NOT COD4X_APPDATA_SUBDIR MATCHES "^[A-Za-z0-9_-]+$")
    message(FATAL_ERROR "COD4X_APPDATA_SUBDIR must be one ASCII folder name containing only letters, digits, underscore or hyphen")
endif()

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

if(RMLUI)
    add_subdirectory(external/freetype)
    add_subdirectory(external/rmlui)
    add_subdirectory(src/rml)
endif()

set(CMAKE_SHARED_LIBRARY_PREFIX "")
add_library(cod4x SHARED
    src/r_init.c
    src/stringed_hooks.c
    src/bg_items.c
    src/rb_backend.c
    src/cl_main.c
    src/cl_tickrate.c
    src/cl_tickrate_core.c
    src/cl_recoil.c
    src/pmove.c
    src/xasset_loader.c
    src/xassets/font.c
    src/xassets/material.c
    src/gfxcmds.c
    src/developer_utils.c
    src/cg_weapons.c
    src/files.c
    src/cl_rank.c
    src/com_stringtable.c
    src/phys.c
    src/com_findxassets.c
    src/win_gqos.c
    src/net_reliabletransport.c
    src/cl_console.c
    src/cl_keys.c
    src/r_main.c
    src/xassets.c
    src/cg_servercmds.c
    src/cg_main.c
    src/cg_font.c
    src/cg_scoreboard.c
    src/win_main.c
    src/sys_net.c
    src/httpftp.c
    src/snd_system.c
    src/cl_ui.c
    src/ui_main.c
    src/qshared.c
    src/bg_shared.c
    src/q_math.c
    src/server.c
    src/other.c
    src/cvar.c
    src/cl_netchan.c
    src/cmd.c
    src/common.c
    src/msg.c
    src/huffman.c
    src/g_team.c
    src/net_chan.c
    src/sys_patch.c
    src/sv_main.c
    src/sv_game.c
    src/sv_world.c
    src/win_input.c
    src/win_wndproc.c
    src/win_syscon.c
    src/win_sys.c
    src/punkbuster_interface.c
    src/md5.c
    src/null.c
    src/crc.c
    src/dobj.c
    src/cod4x_unzip_utils.c

    src/callbacks.asm
    src/client_callbacks.asm
    src/fsdword.asm
)

if(RMLUI)
    target_sources(cod4x PRIVATE src/cl_rml.c)
    target_compile_definitions(cod4x PRIVATE COD4X_RMLUI=1)
    target_link_libraries(cod4x PRIVATE cod4x_rml)
endif()

option(VOLUMETRIC_SMOKE "Build the optional volumetric smoke renderer bridge" OFF)
option(TICKRATE_TESTS "Build client history and packet pacing regression tests" OFF)
option(RECOIL_TESTS "Build shared recoil model/contract regression tests" OFF)
if(RECOIL_TESTS)
    enable_testing()
    add_executable(test_recoil tests/test_recoil.c)
    target_include_directories(test_recoil PRIVATE src)
    target_compile_options(test_recoil PRIVATE -m32 -std=c11 -Wall -Wextra -Werror -UNDEBUG)
    target_link_options(test_recoil PRIVATE -m32 -static)
    add_test(NAME recoil_core COMMAND test_recoil ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/recoil-contracts.tsv)
    set(RECOIL_SERVER_SOURCE "" CACHE PATH "Optional dedicated-server source tree for shared-core drift checks")
    if(RECOIL_SERVER_SOURCE)
        find_package(Python3 COMPONENTS Interpreter REQUIRED)
        add_test(NAME recoil_sync COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/sync-recoil.py
            --check --server ${RECOIL_SERVER_SOURCE})
    endif()
endif()
if(TICKRATE_TESTS)
    enable_testing()
    add_executable(test_cl_tickrate tests/test_cl_tickrate.c src/cl_tickrate_core.c)
    # tickrate_replay is loaded by tests/replay_client_tickrate.py (unicorn replay of the
    # native decoder); it relies on MinGW exporting every non-static function.
    add_library(tickrate_replay SHARED src/cl_tickrate_core.c)
    foreach(target test_cl_tickrate tickrate_replay)
        target_include_directories(${target} PRIVATE src)
        target_compile_options(${target} PRIVATE -m32 -O2 -std=c11 -Wall -Wextra)
        target_link_options(${target} PRIVATE -m32 -static)
    endforeach()
    add_test(NAME client_tickrate COMMAND test_cl_tickrate)
endif()
option(TEST_ISOLATION "Test builds only: app data from COD4X_APPDATA_DIR, no Steam, Discord or registry associations" OFF)
if(TEST_ISOLATION)
    target_compile_definitions(cod4x PRIVATE COD4X_TEST_ISOLATION=1)
endif()
set(VOLUMETRIC_SMOKE_RENDERER_SHA256 "" CACHE STRING "Only load a smoke renderer with this SHA-256 (empty: any)")
if(VOLUMETRIC_SMOKE)
    target_sources(cod4x PRIVATE src/cl_smoke.c src/cl_smoke_core.c src/cl_smoke_hooks.asm)
    target_compile_definitions(cod4x PRIVATE COD4X_VOLUMETRIC_SMOKE=1)
    if(VOLUMETRIC_SMOKE_RENDERER_SHA256)
        target_compile_definitions(cod4x PRIVATE $<$<COMPILE_LANGUAGE:C>:VSMOKE_RENDERER_SHA256="${VOLUMETRIC_SMOKE_RENDERER_SHA256}">)
    endif()
    option(VOLUMETRIC_SMOKE_TESTS "Build the smoke bridge tests (no game needed)" OFF)
    if(VOLUMETRIC_SMOKE_TESTS)
        add_library(vsmoke_stub SHARED smoke/tests/stub_renderer.c)
        add_library(vsmoke_stub_noexport SHARED smoke/tests/stub_renderer.c)
        target_compile_definitions(vsmoke_stub_noexport PRIVATE STUB_NO_EXPORT=1)
        add_executable(test_cl_smoke_core smoke/tests/test_cl_smoke_core.c src/cl_smoke_core.c)
        add_executable(test_smoke_technique smoke/tests/test_smoke_technique.c smoke/tests/test_smoke_technique.asm src/cl_smoke_hooks.asm)
        target_compile_options(test_smoke_technique PRIVATE $<$<COMPILE_LANGUAGE:ASM_NASM>:-f;win32;--prefix;_;-Ox>)
        target_compile_definitions(test_smoke_technique PRIVATE SMOKE_HOOK_TEST=1)
        foreach(target vsmoke_stub vsmoke_stub_noexport test_cl_smoke_core)
            target_include_directories(${target} PRIVATE src)
            target_compile_options(${target} PRIVATE -m32 -O2 -std=c11 -Wall -Wextra)
            target_link_options(${target} PRIVATE -m32 -static)
        endforeach()
        target_link_libraries(test_cl_smoke_core PRIVATE tomcrypt)
    endif()
endif()

if(OFFICIAL_BUILD)
    add_subdirectory(external/client-auth)
    target_compile_definitions(cod4x PRIVATE OFFICIAL=1)
endif()

target_include_directories(cod4x PRIVATE src)
target_compile_definitions(cod4x PRIVATE "COD4X_APPDATA_SUBDIR=L\"${COD4X_APPDATA_SUBDIR}\"")
set_target_properties(cod4x PROPERTIES OUTPUT_NAME "cod4x_021")

add_subdirectory(external/discord-rpc)
add_subdirectory(external/mbedtls)
add_subdirectory(external/steam_api)
add_subdirectory(external/tomcrypt)
add_subdirectory(external/unzip)

target_link_libraries(cod4x PRIVATE
    discord-rpc
    mbedtls
    steam-api
    tomcrypt
    unzip

    version
    kernel32
    advapi32
    ole32
    oleaut32
    uuid
    wsock32
    ws2_32
    dnsapi
    winmm
    shell32
    user32
    gdi32
    crypt32
    d3dx9_34
    psapi
    msvcr100
)

set(C_FLAGS -m32 -O3 -std=c11)
set(CXX_FLAGS -m32 -O3 -std=c++17)
set(NASM_FLAGS -f win32 --prefix _ -Ox)

target_compile_options(cod4x PRIVATE
    $<$<COMPILE_LANGUAGE:C>:${C_FLAGS}>
    $<$<COMPILE_LANGUAGE:CXX>:${CXX_FLAGS}>
    $<$<COMPILE_LANGUAGE:ASM_NASM>:${NASM_FLAGS}>
)

set_source_files_properties(src/sys_net.c PROPERTIES COMPILE_FLAGS -O1) # FIXME: awaiting connection on `-O` higher than -O1
set_source_files_properties(src/cl_ui.c PROPERTIES COMPILE_FLAGS -O0) # FIXME: glitching server refresh on `-O` higher than -O0

target_link_options(cod4x PRIVATE -m32 -static -s -Wl,--exclude-libs,msvcrt.a,-fPic,--stack,8388608)

target_compile_definitions(cod4x PRIVATE
    MAX_LOCAL_CLIENTS=1
)

