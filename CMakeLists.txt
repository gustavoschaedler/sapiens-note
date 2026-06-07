cmake_minimum_required(VERSION 3.20)
project(SapiensNote LANGUAGES CXX VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

add_compile_options(-Wall -Wextra -Wpedantic)

# --- Build version header (generated from PROJECT_VERSION + git) --------------
# Produces <build>/generated/sapiens/Version.h with SAPIENS_VERSION / _GIT_HASH /
# _BUILD_DATE / _BUILD_VERSION, included as "sapiens/Version.h" by every target.
execute_process(
  COMMAND git -C ${CMAKE_SOURCE_DIR} rev-parse --short=7 HEAD
  OUTPUT_VARIABLE SAPIENS_GIT_HASH
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET
  RESULT_VARIABLE _sapiens_git_res)
if(NOT _sapiens_git_res EQUAL 0)
  set(SAPIENS_GIT_HASH "nogit")
endif()
string(TIMESTAMP SAPIENS_BUILD_DATE "%Y-%m-%d")
configure_file(
  ${CMAKE_SOURCE_DIR}/cmake/Version.h.in
  ${CMAKE_BINARY_DIR}/generated/sapiens/Version.h
  @ONLY)
include_directories(${CMAKE_BINARY_DIR}/generated)
message(STATUS "SapiensNote version ${PROJECT_VERSION}+${SAPIENS_GIT_HASH} (${SAPIENS_BUILD_DATE})")

# Code-coverage instrumentation (gcov/llvm-cov): cmake -B build-cov -DSAPIENS_COVERAGE=ON
option(SAPIENS_COVERAGE "Build with coverage instrumentation" OFF)
if(SAPIENS_COVERAGE)
  add_compile_options(--coverage -O0 -g)
  add_link_options(--coverage)
endif()

enable_testing()

# Shared, portable C++ core (no platform/UI dependencies).
add_subdirectory(core)

# SQLite-backed persistence (depends on sqlite3).
add_subdirectory(storage)

# Unit tests for the core.
add_subdirectory(tests)

# Headless CLI that drives the core end-to-end (no graphics).
add_subdirectory(cli)

# Optional local web dashboard (fetches cpp-httplib). Off by default to keep the
# standard/Docker build dependency-light: cmake -B build -S . -DSAPIENS_WITH_WEB=ON
option(SAPIENS_WITH_WEB "Build the local web dashboard 'sapiens-web' (fetches cpp-httplib)" OFF)
if(SAPIENS_WITH_WEB)
  add_subdirectory(webtool)
  message(STATUS "Web dashboard enabled -> building 'sapiens-web'")
endif()

# Desktop simulator (SDL2). Built only when SDL2 is available.
find_package(SDL2 QUIET)
if(SDL2_FOUND)
  add_subdirectory(simulator)
  message(STATUS "SDL2 found (${SDL2_VERSION}) -> building 'sapiens-sim'")

  # Optional LVGL-based simulator (fetches LVGL). Off by default to keep the
  # standard build fast: cmake -B build -S . -DSAPIENS_WITH_LVGL=ON
  option(SAPIENS_WITH_LVGL "Build the LVGL-based simulator (fetches LVGL)" OFF)
  if(SAPIENS_WITH_LVGL)
    add_subdirectory(simulator_lvgl)
    message(STATUS "LVGL simulator enabled -> building 'sapiens-sim-lvgl'")
  endif()
else()
  message(STATUS "SDL2 not found -> skipping simulator (core, tests, cli still build)")
endif()
