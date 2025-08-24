# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.5)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Set PROJECT_VER: git hash for clean builds, revYYYYMMDDHHMMSS-shortHash-dirty for dirty builds
# Refresh git index first to ensure dirty detection works correctly
execute_process(
    COMMAND git update-index --refresh
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    ERROR_QUIET
)
execute_process(
    COMMAND git describe --tags --always --dirty
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE RAW_GIT_DESCRIBE
    OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(
    COMMAND git rev-parse --short HEAD
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_SHORT_HASH
    OUTPUT_STRIP_TRAILING_WHITESPACE)

if (RAW_GIT_DESCRIBE)
    # Check if dirty and format accordingly
    if("${RAW_GIT_DESCRIBE}" MATCHES ".*-dirty$")
        # Dirty builds: use uniform timestamp format
        string(TIMESTAMP BUILD_TIMESTAMP_UNIFORM "%Y%m%d%H%M%S" UTC)
        set(PROJECT_VER "rev${BUILD_TIMESTAMP_UNIFORM}-${GIT_SHORT_HASH}-dirty")
    else()
        # Clean builds: use just the git hash
        set(PROJECT_VER "${GIT_SHORT_HASH}")
    endif()
endif()

project(sensorv2)

# Create a script that generates webapp.json with current git status at build time
file(MAKE_DIRECTORY ${CMAKE_SOURCE_DIR}/fsdata)
set(UPDATE_VERSION_SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/update_version.cmake")
file(WRITE "${UPDATE_VERSION_SCRIPT}" "
# Refresh git index and get current status
execute_process(COMMAND git update-index --refresh WORKING_DIRECTORY \"${CMAKE_SOURCE_DIR}\" ERROR_QUIET)
execute_process(COMMAND git describe --tags --always --dirty WORKING_DIRECTORY \"${CMAKE_SOURCE_DIR}\" OUTPUT_VARIABLE GIT_DESCRIBE OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(COMMAND git rev-parse --short HEAD WORKING_DIRECTORY \"${CMAKE_SOURCE_DIR}\" OUTPUT_VARIABLE GIT_HASH OUTPUT_STRIP_TRAILING_WHITESPACE)

# Format version based on dirty status
if(GIT_DESCRIBE MATCHES \".*-dirty$\")
    string(TIMESTAMP BUILD_TIME \"%Y%m%d%H%M%S\" UTC)
    set(VERSION_STRING \"rev\${BUILD_TIME}-\${GIT_HASH}-dirty\")
else()
    set(VERSION_STRING \"\${GIT_HASH}\")
endif()

# Generate current timestamp
string(TIMESTAMP NOW_EPOCH \"%s\" UTC)
string(TIMESTAMP NOW_ISO \"%Y-%m-%dT%H:%M:%SZ\" UTC)

# Write both webapp.json and firmware_version.txt
file(WRITE \"${CMAKE_SOURCE_DIR}/fsdata/webapp.json\" \"{
  \\\"local_version\\\": \\\"\${VERSION_STRING}\\\",
  \\\"local_git_describe\\\": \\\"\${VERSION_STRING}\\\",
  \\\"local_build_timestamp\\\": \\\"\${NOW_ISO}\\\",
  \\\"local_build_timestamp_epoch\\\": \${NOW_EPOCH}
}
\")
file(WRITE \"${CMAKE_SOURCE_DIR}/fsdata/firmware_version.txt\" \"\${VERSION_STRING}\")
")

# Add custom target that runs the version update script on every build
add_custom_target(update_git_version ALL
    COMMAND ${CMAKE_COMMAND} -P "${UPDATE_VERSION_SCRIPT}"
    COMMENT "Updating git version information"
    VERBATIM
)

# Initial webapp.json generation (will be updated by custom target)
string(TIMESTAMP NOW_EPOCH "%s" UTC)
string(TIMESTAMP NOW_ISO "%Y-%m-%dT%H:%M:%SZ" UTC)
if("${RAW_GIT_DESCRIBE}" MATCHES ".*-dirty$")
    set(WEB_GIT_DESCRIBE "rev${BUILD_TIMESTAMP_UNIFORM}-${GIT_SHORT_HASH}-dirty")
else()
    set(WEB_GIT_DESCRIBE "${GIT_SHORT_HASH}")
endif()
file(WRITE ${CMAKE_SOURCE_DIR}/fsdata/webapp.json
"{\n"
"  \"local_version\": \"${WEB_GIT_DESCRIBE}\",\n"
"  \"local_git_describe\": \"${WEB_GIT_DESCRIBE}\",\n"
"  \"local_build_timestamp\": \"${NOW_ISO}\",\n"
"  \"local_build_timestamp_epoch\": ${NOW_EPOCH}\n"
"}\n"
)

# Create LittleFS image for the 'storage' partition from directory 'fsdata'.
include($ENV{IDF_PATH}/components/partition_table/littlefs.cmake OPTIONAL)
if (COMMAND littlefs_create_partition_image)
    littlefs_create_partition_image(storage fsdata FLASH_IN_PROJECT)
else()
    message(STATUS "littlefs_create_partition_image not available; relying on update_js.sh")
endif()