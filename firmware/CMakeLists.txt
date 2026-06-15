# ESP-IDF top-level project. Requires ESP-IDF v5.x (idf.py).
cmake_minimum_required(VERSION 3.16)

# Pull the shared core in as an extra component directory.
set(EXTRA_COMPONENT_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/components)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# The shared Renderer.h uses fixed snprintf buffers that GCC flags as possibly
# truncated for theoretical max values (time/index are bounded in practice).
# ESP-IDF promotes these to errors; keep them as warnings for the firmware build.
add_compile_options(-Wno-error=format-truncation -Wno-error=format-overflow)

project(sapiensnote)
