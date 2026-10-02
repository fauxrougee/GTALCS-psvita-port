# Included after the game target. No prebuilt game or old VPK is required.
set(INTRO_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/boot/intro.mp4" CACHE FILEPATH "Edited intro video")
if(NOT EXISTS "${INTRO_SOURCE}")
    message(FATAL_ERROR "Intro video missing: ${INTRO_SOURCE}. Restore it or set VITA_WITH_INTRO=OFF.")
endif()
add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/intro.vtm" "${CMAKE_CURRENT_BINARY_DIR}/intro.json"
    COMMAND "${Python3_EXECUTABLE}" "${ROOT}/tools/vita/pack-boot-movie.py"
        --source "${INTRO_SOURCE}" --output "${CMAKE_CURRENT_BINARY_DIR}/intro.vtm"
    DEPENDS "${INTRO_SOURCE}" "${ROOT}/tools/vita/pack-boot-movie.py"
    VERBATIM)
add_executable(relcs_intro launcher/main.cpp launcher/movie.cpp launcher/intro_log.cpp
    launcher/movie_file.cpp launcher/lz4/lz4.c updater/core.cpp updater/platform.cpp updater/ui.cpp)
target_compile_definitions(relcs_intro PRIVATE PSP2)
target_compile_options(relcs_intro PRIVATE -mcpu=cortex-a9 -mfpu=neon -mtune=cortex-a9
    -O2 -Wall -Wextra -ffunction-sections -fdata-sections
    $<$<COMPILE_LANGUAGE:CXX>:-std=c++17;-fno-exceptions;-fno-rtti>)
target_link_options(relcs_intro PRIVATE "-Wl,-T,${CMAKE_CURRENT_SOURCE_DIR}/sce-metadata-gap.ld"
    -Wl,--gc-sections)
set_property(TARGET relcs_intro APPEND PROPERTY LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/sce-metadata-gap.ld")
target_link_libraries(relcs_intro z SceAppMgr_stub SceAudio_stub ScePower_stub
    SceAppUtil_stub ScePromoterUtil_stub SceSysmodule_stub SceCtrl_stub SceDisplay_stub SceLibKernel_stub)
vita_create_self(intro.bin relcs_intro UNSAFE)
