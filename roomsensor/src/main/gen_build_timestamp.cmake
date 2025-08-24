if(NOT DEFINED OUT)
    message(FATAL_ERROR "OUT not set")
endif()

# Generate current UTC epoch seconds
string(TIMESTAMP NOW_EPOCH "%s" UTC)

# Write header with newline separation
file(WRITE "${OUT}" "#pragma once\n#define BUILD_TIMESTAMP ${NOW_EPOCH}\n")


