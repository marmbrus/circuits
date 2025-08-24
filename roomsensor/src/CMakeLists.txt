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

# Create a simple shell script that generates version files at build time
file(MAKE_DIRECTORY ${CMAKE_SOURCE_DIR}/fsdata)
set(UPDATE_VERSION_SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/update_version.sh")
file(WRITE "${UPDATE_VERSION_SCRIPT}" "#!/bin/bash
cd \"${CMAKE_SOURCE_DIR}\"
git update-index --refresh 2>/dev/null || true
GIT_DESCRIBE=\$(git describe --tags --always --dirty 2>/dev/null || echo \"unknown\")
GIT_HASH=\$(git rev-parse --short HEAD 2>/dev/null || echo \"unknown\")

if [[ \"\$GIT_DESCRIBE\" == *-dirty ]]; then
    BUILD_TIME=\$(date -u +\"%Y%m%d%H%M%S\")
    VERSION_STRING=\"rev\${BUILD_TIME}-\${GIT_HASH}-dirty\"
else
    VERSION_STRING=\"\${GIT_HASH}\"
fi

NOW_EPOCH=\$(date -u +%s)
NOW_ISO=\$(date -u +\"%Y-%m-%dT%H:%M:%SZ\")

cat > \"${CMAKE_SOURCE_DIR}/fsdata/webapp.json\" << EOF
{
  \"local_version\": \"\$VERSION_STRING\",
  \"local_git_describe\": \"\$VERSION_STRING\",
  \"local_build_timestamp\": \"\$NOW_ISO\",
  \"local_build_timestamp_epoch\": \$NOW_EPOCH
}
EOF

echo -n \"\$VERSION_STRING\" > \"${CMAKE_SOURCE_DIR}/fsdata/firmware_version.txt\"
echo \"Updated version to: \$VERSION_STRING\"
")

# Make the script executable and add custom target that runs it on every build
file(CHMOD "${UPDATE_VERSION_SCRIPT}" PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE)
add_custom_target(update_git_version ALL
    COMMAND bash "${UPDATE_VERSION_SCRIPT}"
    COMMENT "Updating git version information"
    VERBATIM
)



# Create LittleFS image for the 'storage' partition from directory 'fsdata'.
include($ENV{IDF_PATH}/components/partition_table/littlefs.cmake OPTIONAL)
if (COMMAND littlefs_create_partition_image)
    littlefs_create_partition_image(storage fsdata FLASH_IN_PROJECT)
    # Make LittleFS image depend on our version update target (if storage target exists)
    if(TARGET storage)
        add_dependencies(storage update_git_version)
    endif()
else()
    message(STATUS "littlefs_create_partition_image not available; relying on update_js.sh")
endif()