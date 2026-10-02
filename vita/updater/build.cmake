# Internal SELF launched only by the game's LiveArea UPDATE action.
add_executable(relcs_update updater/main.cpp updater/core.cpp updater/network.cpp
    updater/platform.cpp updater/ui.cpp)
target_compile_definitions(relcs_update PRIVATE PSP2 CURL_STATICLIB)
target_compile_options(relcs_update PRIVATE ${VITA_CPU_FLAGS} -O2 -Wall -Wextra
    -ffunction-sections -fdata-sections
    $<$<COMPILE_LANGUAGE:CXX>:-std=c++17;-fno-exceptions;-fno-rtti>)
target_link_options(relcs_update PRIVATE "-Wl,-T,${CMAKE_CURRENT_SOURCE_DIR}/sce-metadata-gap.ld"
    -Wl,--gc-sections)
set_property(TARGET relcs_update APPEND PROPERTY LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/sce-metadata-gap.ld")
target_link_libraries(relcs_update curl mbedtls mbedx509 mbedcrypto zstd z
    -Wl,--whole-archive pthread -Wl,--no-whole-archive
    SceNet_stub SceNetCtl_stub SceRtc_stub SceSysmodule_stub ScePromoterUtil_stub
    SceAppMgr_stub SceCtrl_stub SceDisplay_stub ScePower_stub SceLibKernel_stub)
vita_create_self(update.bin relcs_update UNSAFE)
list(APPEND PACKAGE_DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/update.bin")
file(GLOB UPDATE_RESOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/updater/licenses/*")
list(APPEND PACKAGE_DEPENDS ${UPDATE_RESOURCES} "${CMAKE_CURRENT_SOURCE_DIR}/updater/ca-bundle.pem")
list(APPEND PACKAGE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/updater/template.xml")
