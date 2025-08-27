# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.5)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Optionally include ESP-ADF components when ADF_PATH is provided
if(DEFINED ENV{ADF_PATH})
    # Make ESP-ADF components available to the project. Append so we don't clobber existing settings.
    if(EXISTS "$ENV{ADF_PATH}/components")
        list(APPEND EXTRA_COMPONENT_DIRS "$ENV{ADF_PATH}/components")
        message(STATUS "Including ESP-ADF components from: $ENV{ADF_PATH}/components")
    else()
        message(WARNING "ADF components directory not found at: $ENV{ADF_PATH}/components")
    endif()

    # Some ADF versions provide 'esp_codec' as a separate component under esp-adf-libs.
    if(EXISTS "$ENV{ADF_PATH}/esp-adf-libs/esp_codec")
        list(APPEND EXTRA_COMPONENT_DIRS "$ENV{ADF_PATH}/esp-adf-libs/esp_codec")
        message(STATUS "Including ESP-ADF codec component from: $ENV{ADF_PATH}/esp-adf-libs/esp_codec")
    endif()
else()
    message(STATUS "ADF_PATH not set; skipping ESP-ADF integration")
endif()

# Set PROJECT_VER to a placeholder - actual version comes from build-time generated files
# The firmware will read the real version from /storage/firmware_version.txt
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
    # Make LittleFS image depend on our version update target
    # The actual target name is littlefs_storage_bin, not storage
    add_dependencies(littlefs_storage_bin update_git_version)
else()
    message(STATUS "littlefs_create_partition_image not available; relying on update_js.sh")
endif()