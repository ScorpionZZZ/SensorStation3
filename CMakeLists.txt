# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.22)

# ── App version ────────────────────────────────────────────────────────────
# Change MAJOR or MINOR manually; BUILD is auto-incremented on every build
# and resets to 1 whenever MAJOR or MINOR changes.
set(APP_VERSION_MAJOR 1)
set(APP_VERSION_MINOR 3)

# Read the stored build number at configure time so PROJECT_VER is populated.
set(_bn_file "${CMAKE_CURRENT_SOURCE_DIR}/build_number.txt")
if(EXISTS "${_bn_file}")
    file(READ "${_bn_file}" _bn_content)
    string(REPLACE "\n" ";" _bn_lines "${_bn_content}")
    list(GET _bn_lines 2 _bn)
    string(STRIP "${_bn}" _bn)
    if(NOT _bn MATCHES "^[0-9]+$")
        set(_bn 0)
    endif()
else()
    set(_bn 0)
endif()
set(PROJECT_VER "${APP_VERSION_MAJOR}.${APP_VERSION_MINOR}.${_bn}")
# ── End version ────────────────────────────────────────────────────────────

# Extra component directories searched by ESP-IDF build system.
# All subdirectories of components/ that contain a CMakeLists.txt are auto-discovered.
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/components")

# Must be set before components are processed so the LVGL managed component
# finds our config file instead of trying to read Kconfig.
set(LV_CONF_PATH
    "${CMAKE_CURRENT_SOURCE_DIR}/components/display/include/lv_conf.h"
    CACHE STRING "" FORCE)
set(LV_KCONFIG_IGNORE 1 CACHE STRING "" FORCE)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(SensorStation3)

# Custom target: increments build_number.txt and regenerates build/config/build_info.h
# on every build, before the firmware is linked.
add_custom_target(increment_build ALL
    COMMAND ${CMAKE_COMMAND}
        -DSRC_DIR=${CMAKE_CURRENT_SOURCE_DIR}
        -DBIN_DIR=${CMAKE_BINARY_DIR}
        -DAPP_MAJOR=${APP_VERSION_MAJOR}
        -DAPP_MINOR=${APP_VERSION_MINOR}
        -DPROJECT_NAME=${CMAKE_PROJECT_NAME}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/increment_build.cmake
    COMMENT "Updating build number"
    VERBATIM
)
add_dependencies(${CMAKE_PROJECT_NAME}.elf increment_build)
