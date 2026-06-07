# Wrap the shared, portable C++ core as an ESP-IDF component so the firmware links
# the exact same domain/services/sync code the simulator uses.
file(GLOB SAPIENS_CORE_SRCS ${CMAKE_CURRENT_SOURCE_DIR}/../../../core/src/*.cpp)

idf_component_register(
    SRCS ${SAPIENS_CORE_SRCS}
    INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/../../../core/include
)

# The core is C++23.
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_23)
