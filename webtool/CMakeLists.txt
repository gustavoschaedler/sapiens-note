# Local web dashboard (opt-in: -DSAPIENS_WITH_WEB=ON). Fetches cpp-httplib (a single
# header) and builds 'sapiens-web', which reuses the shared core + SQLite repo and
# the pluggable transcription providers. Kept separate so the default build stays
# dependency-light.
include(FetchContent)

FetchContent_Declare(
    httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG v0.46.1
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(httplib)

add_executable(sapiens-web main.cpp)
target_include_directories(sapiens-web PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(sapiens-web PRIVATE sapienscore sapiensstore httplib::httplib)

find_package(Threads REQUIRED)
target_link_libraries(sapiens-web PRIVATE Threads::Threads)

# Optional cloud engine (OpenAI Whisper API) — only if libcurl is available.
find_package(CURL QUIET)
if(CURL_FOUND)
    target_link_libraries(sapiens-web PRIVATE CURL::libcurl)
    target_compile_definitions(sapiens-web PRIVATE SAPIENS_HAVE_CURL=1)
    message(STATUS "sapiens-web: libcurl found -> OpenAI engine enabled")
else()
    message(STATUS "sapiens-web: libcurl NOT found -> only the whisper.cpp engine")
endif()
