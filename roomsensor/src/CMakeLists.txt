# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.5)

# ESP-ADF provides audio playback. The docker/esp-idf image sets ADF_PATH.
if(NOT DEFINED ENV{ADF_PATH})
    message(FATAL_ERROR "ADF_PATH is not set. Build with docker/esp-idf/idf, or install ESP-ADF v2.7 and export ADF_PATH.")
endif()
include($ENV{ADF_PATH}/CMakeLists.txt)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# The app description's version (esp_app_get_description()) is the short git hash at configure
# time. OTA uses the version and time build_info.sh stamps on every build (main/CMakeLists.txt).
execute_process(
    COMMAND git rev-parse --short HEAD
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_SHORT_HASH
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)

if (GIT_SHORT_HASH)
    set(PROJECT_VER "${GIT_SHORT_HASH}")
else()
    set(PROJECT_VER "unknown")
endif()

project(sensorv2)
