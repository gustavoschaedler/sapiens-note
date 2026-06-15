# Wrap the shared, portable C++ core as an ESP-IDF component so the firmware links
# the exact same domain/services/sync code the simulator uses.
file(GLOB SAPIENS_CORE_SRCS ${CMAKE_CURRENT_SOURCE_DIR}/../../../core/src/*.cpp)

# Generate sapiens/Version.h. The desktop build does this in the top-level CMake;
# the ESP-IDF build must do it too (Renderer.h includes "sapiens/Version.h").
# Use CMAKE_CURRENT_LIST_DIR (always this file's dir) and skip ESP-IDF's early
# requirement-expansion pass, where the source/binary dirs aren't set yet.
if(NOT CMAKE_BUILD_EARLY_EXPANSION)
    set(SAPIENS_REPO ${CMAKE_CURRENT_LIST_DIR}/../../..)
    execute_process(
        COMMAND git -C ${SAPIENS_REPO} rev-parse --short=7 HEAD
        OUTPUT_VARIABLE SAPIENS_GIT_HASH OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET RESULT_VARIABLE _sapiens_git_res)
    if(NOT _sapiens_git_res EQUAL 0)
        set(SAPIENS_GIT_HASH "nogit")
    endif()
    string(TIMESTAMP SAPIENS_BUILD_DATE "%Y-%m-%d")
    set(PROJECT_VERSION "0.1.0")   # keep in sync with the top-level project() VERSION
    configure_file(
        ${SAPIENS_REPO}/cmake/Version.h.in
        ${CMAKE_CURRENT_BINARY_DIR}/generated/sapiens/Version.h
        @ONLY)
endif()

idf_component_register(
    SRCS ${SAPIENS_CORE_SRCS}
    INCLUDE_DIRS
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../core/include
        ${CMAKE_CURRENT_BINARY_DIR}/generated
)

# The core is C++23.
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_23)
